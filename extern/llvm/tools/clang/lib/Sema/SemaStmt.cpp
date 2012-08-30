//===--- SemaStmt.cpp - Semantic Analysis for Statements ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for statements.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
using namespace clang;
using namespace sema;

StmtResult Sema::ActOnExprStmt(FullExprArg expr) {
  Expr *E = expr.get();
  if (!E) // FIXME: FullExprArg has no error state?
    return StmtError();

  // C99 6.8.3p2: The expression in an expression statement is evaluated as a
  // void expression for its side effects.  Conversion to void allows any
  // operand, even incomplete types.

  // Same thing in for stmt first clause (when expr) and third clause.
  return Owned(static_cast<Stmt*>(E));
}


StmtResult Sema::ActOnNullStmt(SourceLocation SemiLoc,
                               bool HasLeadingEmptyMacro) {
  return Owned(new (Context) NullStmt(SemiLoc, HasLeadingEmptyMacro));
}

StmtResult Sema::ActOnDeclStmt(DeclGroupPtrTy dg, SourceLocation StartLoc,
                               SourceLocation EndLoc) {
  DeclGroupRef DG = dg.getAsVal<DeclGroupRef>();

  // If we have an invalid decl, just return an error.
  if (DG.isNull()) return StmtError();

  return Owned(new (Context) DeclStmt(DG, StartLoc, EndLoc));
}

void Sema::ActOnForEachDeclStmt(DeclGroupPtrTy dg) {
  DeclGroupRef DG = dg.getAsVal<DeclGroupRef>();

  // If we have an invalid decl, just return.
  if (DG.isNull() || !DG.isSingleDecl()) return;
  VarDecl *var = cast<VarDecl>(DG.getSingleDecl());

  // suppress any potential 'unused variable' warning.
  var->setUsed();

  // foreach variables are never actually initialized in the way that
  // the parser came up with.
  var->setInit(0);

  // In ARC, we don't need to retain the iteration variable of a fast
  // enumeration loop.  Rather than actually trying to catch that
  // during declaration processing, we remove the consequences here.
  if (getLangOpts().ObjCAutoRefCount) {
    QualType type = var->getType();

    // Only do this if we inferred the lifetime.  Inferred lifetime
    // will show up as a local qualifier because explicit lifetime
    // should have shown up as an AttributedType instead.
    if (type.getLocalQualifiers().getObjCLifetime() == Qualifiers::OCL_Strong) {
      // Add 'const' and mark the variable as pseudo-strong.
      var->setType(type.withConst());
      var->setARCPseudoStrong(true);
    }
  }
}

/// \brief Diagnose unused '==' and '!=' as likely typos for '=' or '|='.
///
/// Adding a cast to void (or other expression wrappers) will prevent the
/// warning from firing.
static bool DiagnoseUnusedComparison(Sema &S, const Expr *E) {
  SourceLocation Loc;
  bool IsNotEqual, CanAssign;

  if (const BinaryOperator *Op = dyn_cast<BinaryOperator>(E)) {
    if (Op->getOpcode() != BO_EQ && Op->getOpcode() != BO_NE)
      return false;

    Loc = Op->getOperatorLoc();
    IsNotEqual = Op->getOpcode() == BO_NE;
    CanAssign = Op->getLHS()->IgnoreParenImpCasts()->isLValue();
  } else if (const CXXOperatorCallExpr *Op = dyn_cast<CXXOperatorCallExpr>(E)) {
    if (Op->getOperator() != OO_EqualEqual &&
        Op->getOperator() != OO_ExclaimEqual)
      return false;

    Loc = Op->getOperatorLoc();
    IsNotEqual = Op->getOperator() == OO_ExclaimEqual;
    CanAssign = Op->getArg(0)->IgnoreParenImpCasts()->isLValue();
  } else {
    // Not a typo-prone comparison.
    return false;
  }

  // Suppress warnings when the operator, suspicious as it may be, comes from
  // a macro expansion.
  if (Loc.isMacroID())
    return false;

  S.Diag(Loc, diag::warn_unused_comparison)
    << (unsigned)IsNotEqual << E->getSourceRange();

  // If the LHS is a plausible entity to assign to, provide a fixit hint to
  // correct common typos.
  if (CanAssign) {
    if (IsNotEqual)
      S.Diag(Loc, diag::note_inequality_comparison_to_or_assign)
        << FixItHint::CreateReplacement(Loc, "|=");
    else
      S.Diag(Loc, diag::note_equality_comparison_to_assign)
        << FixItHint::CreateReplacement(Loc, "=");
  }

  return true;
}

void Sema::DiagnoseUnusedExprResult(const Stmt *S) {
  if (const LabelStmt *Label = dyn_cast_or_null<LabelStmt>(S))
    return DiagnoseUnusedExprResult(Label->getSubStmt());

  const Expr *E = dyn_cast_or_null<Expr>(S);
  if (!E)
    return;

  SourceLocation Loc;
  SourceRange R1, R2;
  if (SourceMgr.isInSystemMacro(E->getExprLoc()) ||
      !E->isUnusedResultAWarning(Loc, R1, R2, Context))
    return;

  // Okay, we have an unused result.  Depending on what the base expression is,
  // we might want to make a more specific diagnostic.  Check for one of these
  // cases now.
  unsigned DiagID = diag::warn_unused_expr;
  if (const ExprWithCleanups *Temps = dyn_cast<ExprWithCleanups>(E))
    E = Temps->getSubExpr();
  if (const CXXBindTemporaryExpr *TempExpr = dyn_cast<CXXBindTemporaryExpr>(E))
    E = TempExpr->getSubExpr();

  if (DiagnoseUnusedComparison(*this, E))
    return;

  E = E->IgnoreParenImpCasts();
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (E->getType()->isVoidType())
      return;

    // If the callee has attribute pure, const, or warn_unused_result, warn with
    // a more specific message to make it clear what is happening.
    if (const Decl *FD = CE->getCalleeDecl()) {
      if (FD->getAttr<WarnUnusedResultAttr>()) {
        Diag(Loc, diag::warn_unused_result) << R1 << R2;
        return;
      }
      if (FD->getAttr<PureAttr>()) {
        Diag(Loc, diag::warn_unused_call) << R1 << R2 << "pure";
        return;
      }
      if (FD->getAttr<ConstAttr>()) {
        Diag(Loc, diag::warn_unused_call) << R1 << R2 << "const";
        return;
      }
    }
  } else if (const ObjCMessageExpr *ME = dyn_cast<ObjCMessageExpr>(E)) {
    if (getLangOpts().ObjCAutoRefCount && ME->isDelegateInitCall()) {
      Diag(Loc, diag::err_arc_unused_init_message) << R1;
      return;
    }
    const ObjCMethodDecl *MD = ME->getMethodDecl();
    if (MD && MD->getAttr<WarnUnusedResultAttr>()) {
      Diag(Loc, diag::warn_unused_result) << R1 << R2;
      return;
    }
  } else if (const PseudoObjectExpr *POE = dyn_cast<PseudoObjectExpr>(E)) {
    const Expr *Source = POE->getSyntacticForm();
    if (isa<ObjCSubscriptRefExpr>(Source))
      DiagID = diag::warn_unused_container_subscript_expr;
    else
      DiagID = diag::warn_unused_property_expr;
  } else if (const CXXFunctionalCastExpr *FC
                                       = dyn_cast<CXXFunctionalCastExpr>(E)) {
    if (isa<CXXConstructExpr>(FC->getSubExpr()) ||
        isa<CXXTemporaryObjectExpr>(FC->getSubExpr()))
      return;
  }
  // Diagnose "(void*) blah" as a typo for "(void) blah".
  else if (const CStyleCastExpr *CE = dyn_cast<CStyleCastExpr>(E)) {
    TypeSourceInfo *TI = CE->getTypeInfoAsWritten();
    QualType T = TI->getType();

    // We really do want to use the non-canonical type here.
    if (T == Context.VoidPtrTy) {
      PointerTypeLoc TL = cast<PointerTypeLoc>(TI->getTypeLoc());

      Diag(Loc, diag::warn_unused_voidptr)
        << FixItHint::CreateRemoval(TL.getStarLoc());
      return;
    }
  }

  DiagRuntimeBehavior(Loc, 0, PDiag(DiagID) << R1 << R2);
}

void Sema::ActOnStartOfCompoundStmt() {
  PushCompoundScope();
}

void Sema::ActOnFinishOfCompoundStmt() {
  PopCompoundScope();
}

sema::CompoundScopeInfo &Sema::getCurCompoundScope() const {
  return getCurFunction()->CompoundScopes.back();
}

StmtResult
Sema::ActOnCompoundStmt(SourceLocation L, SourceLocation R,
                        MultiStmtArg elts, bool isStmtExpr) {
  unsigned NumElts = elts.size();
  Stmt **Elts = reinterpret_cast<Stmt**>(elts.release());
  // If we're in C89 mode, check that we don't have any decls after stmts.  If
  // so, emit an extension diagnostic.
  if (!getLangOpts().C99 && !getLangOpts().CPlusPlus) {
    // Note that __extension__ can be around a decl.
    unsigned i = 0;
    // Skip over all declarations.
    for (; i != NumElts && isa<DeclStmt>(Elts[i]); ++i)
      /*empty*/;

    // We found the end of the list or a statement.  Scan for another declstmt.
    for (; i != NumElts && !isa<DeclStmt>(Elts[i]); ++i)
      /*empty*/;

    if (i != NumElts) {
      Decl *D = *cast<DeclStmt>(Elts[i])->decl_begin();
      Diag(D->getLocation(), diag::ext_mixed_decls_code);
    }
  }
  // Warn about unused expressions in statements.
  for (unsigned i = 0; i != NumElts; ++i) {
    // Ignore statements that are last in a statement expression.
    if (isStmtExpr && i == NumElts - 1)
      continue;

    DiagnoseUnusedExprResult(Elts[i]);
  }

  // Check for suspicious empty body (null statement) in `for' and `while'
  // statements.  Don't do anything for template instantiations, this just adds
  // noise.
  if (NumElts != 0 && !CurrentInstantiationScope &&
      getCurCompoundScope().HasEmptyLoopBodies) {
    for (unsigned i = 0; i != NumElts - 1; ++i)
      DiagnoseEmptyLoopBody(Elts[i], Elts[i + 1]);
  }

  return Owned(new (Context) CompoundStmt(Context, Elts, NumElts, L, R));
}

StmtResult
Sema::ActOnCaseStmt(SourceLocation CaseLoc, Expr *LHSVal,
                    SourceLocation DotDotDotLoc, Expr *RHSVal,
                    SourceLocation ColonLoc) {
  assert((LHSVal != 0) && "missing expression in case statement");

  if (getCurFunction()->SwitchStack.empty()) {
    Diag(CaseLoc, diag::err_case_not_in_switch);
    return StmtError();
  }

  if (!getLangOpts().CPlusPlus0x) {
    // C99 6.8.4.2p3: The expression shall be an integer constant.
    // However, GCC allows any evaluatable integer expression.
    if (!LHSVal->isTypeDependent() && !LHSVal->isValueDependent()) {
      LHSVal = VerifyIntegerConstantExpression(LHSVal).take();
      if (!LHSVal)
        return StmtError();
    }

    // GCC extension: The expression shall be an integer constant.

    if (RHSVal && !RHSVal->isTypeDependent() && !RHSVal->isValueDependent()) {
      RHSVal = VerifyIntegerConstantExpression(RHSVal).take();
      // Recover from an error by just forgetting about it.
    }
  }

  CaseStmt *CS = new (Context) CaseStmt(LHSVal, RHSVal, CaseLoc, DotDotDotLoc,
                                        ColonLoc);
  getCurFunction()->SwitchStack.back()->addSwitchCase(CS);
  return Owned(CS);
}

/// ActOnCaseStmtBody - This installs a statement as the body of a case.
void Sema::ActOnCaseStmtBody(Stmt *caseStmt, Stmt *SubStmt) {
  DiagnoseUnusedExprResult(SubStmt);

  CaseStmt *CS = static_cast<CaseStmt*>(caseStmt);
  CS->setSubStmt(SubStmt);
}

StmtResult
Sema::ActOnDefaultStmt(SourceLocation DefaultLoc, SourceLocation ColonLoc,
                       Stmt *SubStmt, Scope *CurScope) {
  DiagnoseUnusedExprResult(SubStmt);

  if (getCurFunction()->SwitchStack.empty()) {
    Diag(DefaultLoc, diag::err_default_not_in_switch);
    return Owned(SubStmt);
  }

  DefaultStmt *DS = new (Context) DefaultStmt(DefaultLoc, ColonLoc, SubStmt);
  getCurFunction()->SwitchStack.back()->addSwitchCase(DS);
  return Owned(DS);
}

StmtResult
Sema::ActOnLabelStmt(SourceLocation IdentLoc, LabelDecl *TheDecl,
                     SourceLocation ColonLoc, Stmt *SubStmt) {
  // If the label was multiply defined, reject it now.
  if (TheDecl->getStmt()) {
    Diag(IdentLoc, diag::err_redefinition_of_label) << TheDecl->getDeclName();
    Diag(TheDecl->getLocation(), diag::note_previous_definition);
    return Owned(SubStmt);
  }

  // Otherwise, things are good.  Fill in the declaration and return it.
  LabelStmt *LS = new (Context) LabelStmt(IdentLoc, TheDecl, SubStmt);
  TheDecl->setStmt(LS);
  if (!TheDecl->isGnuLocal())
    TheDecl->setLocation(IdentLoc);
  return Owned(LS);
}

StmtResult Sema::ActOnAttributedStmt(SourceLocation AttrLoc,
                                     const AttrVec &Attrs,
                                     Stmt *SubStmt) {
  // Fill in the declaration and return it. Variable length will require to
  // change this to AttributedStmt::Create(Context, ....);
  // and probably using ArrayRef
  AttributedStmt *LS = new (Context) AttributedStmt(AttrLoc, Attrs, SubStmt);
  return Owned(LS);
}

StmtResult
Sema::ActOnIfStmt(SourceLocation IfLoc, FullExprArg CondVal, Decl *CondVar,
                  Stmt *thenStmt, SourceLocation ElseLoc,
                  Stmt *elseStmt) {
  ExprResult CondResult(CondVal.release());

  VarDecl *ConditionVar = 0;
  if (CondVar) {
    ConditionVar = cast<VarDecl>(CondVar);
    CondResult = CheckConditionVariable(ConditionVar, IfLoc, true);
    if (CondResult.isInvalid())
      return StmtError();
  }
  Expr *ConditionExpr = CondResult.takeAs<Expr>();
  if (!ConditionExpr)
    return StmtError();

  DiagnoseUnusedExprResult(thenStmt);

  if (!elseStmt) {
    DiagnoseEmptyStmtBody(ConditionExpr->getLocEnd(), thenStmt,
                          diag::warn_empty_if_body);
  }

  DiagnoseUnusedExprResult(elseStmt);

  return Owned(new (Context) IfStmt(Context, IfLoc, ConditionVar, ConditionExpr,
                                    thenStmt, ElseLoc, elseStmt));
}

/// ConvertIntegerToTypeWarnOnOverflow - Convert the specified APInt to have
/// the specified width and sign.  If an overflow occurs, detect it and emit
/// the specified diagnostic.
void Sema::ConvertIntegerToTypeWarnOnOverflow(llvm::APSInt &Val,
                                              unsigned NewWidth, bool NewSign,
                                              SourceLocation Loc,
                                              unsigned DiagID) {
  // Perform a conversion to the promoted condition type if needed.
  if (NewWidth > Val.getBitWidth()) {
    // If this is an extension, just do it.
    Val = Val.extend(NewWidth);
    Val.setIsSigned(NewSign);

    // If the input was signed and negative and the output is
    // unsigned, don't bother to warn: this is implementation-defined
    // behavior.
    // FIXME: Introduce a second, default-ignored warning for this case?
  } else if (NewWidth < Val.getBitWidth()) {
    // If this is a truncation, check for overflow.
    llvm::APSInt ConvVal(Val);
    ConvVal = ConvVal.trunc(NewWidth);
    ConvVal.setIsSigned(NewSign);
    ConvVal = ConvVal.extend(Val.getBitWidth());
    ConvVal.setIsSigned(Val.isSigned());
    if (ConvVal != Val)
      Diag(Loc, DiagID) << Val.toString(10) << ConvVal.toString(10);

    // Regardless of whether a diagnostic was emitted, really do the
    // truncation.
    Val = Val.trunc(NewWidth);
    Val.setIsSigned(NewSign);
  } else if (NewSign != Val.isSigned()) {
    // Convert the sign to match the sign of the condition.  This can cause
    // overflow as well: unsigned(INTMIN)
    // We don't diagnose this overflow, because it is implementation-defined
    // behavior.
    // FIXME: Introduce a second, default-ignored warning for this case?
    llvm::APSInt OldVal(Val);
    Val.setIsSigned(NewSign);
  }
}

namespace {
  struct CaseCompareFunctor {
    bool operator()(const std::pair<llvm::APSInt, CaseStmt*> &LHS,
                    const llvm::APSInt &RHS) {
      return LHS.first < RHS;
    }
    bool operator()(const std::pair<llvm::APSInt, CaseStmt*> &LHS,
                    const std::pair<llvm::APSInt, CaseStmt*> &RHS) {
      return LHS.first < RHS.first;
    }
    bool operator()(const llvm::APSInt &LHS,
                    const std::pair<llvm::APSInt, CaseStmt*> &RHS) {
      return LHS < RHS.first;
    }
  };
}

/// CmpCaseVals - Comparison predicate for sorting case values.
///
static bool CmpCaseVals(const std::pair<llvm::APSInt, CaseStmt*>& lhs,
                        const std::pair<llvm::APSInt, CaseStmt*>& rhs) {
  if (lhs.first < rhs.first)
    return true;

  if (lhs.first == rhs.first &&
      lhs.second->getCaseLoc().getRawEncoding()
       < rhs.second->getCaseLoc().getRawEncoding())
    return true;
  return false;
}

/// CmpEnumVals - Comparison predicate for sorting enumeration values.
///
static bool CmpEnumVals(const std::pair<llvm::APSInt, EnumConstantDecl*>& lhs,
                        const std::pair<llvm::APSInt, EnumConstantDecl*>& rhs)
{
  return lhs.first < rhs.first;
}

/// EqEnumVals - Comparison preficate for uniqing enumeration values.
///
static bool EqEnumVals(const std::pair<llvm::APSInt, EnumConstantDecl*>& lhs,
                       const std::pair<llvm::APSInt, EnumConstantDecl*>& rhs)
{
  return lhs.first == rhs.first;
}

/// GetTypeBeforeIntegralPromotion - Returns the pre-promotion type of
/// potentially integral-promoted expression @p expr.
static QualType GetTypeBeforeIntegralPromotion(Expr *&expr) {
  if (ExprWithCleanups *cleanups = dyn_cast<ExprWithCleanups>(expr))
    expr = cleanups->getSubExpr();
  while (ImplicitCastExpr *impcast = dyn_cast<ImplicitCastExpr>(expr)) {
    if (impcast->getCastKind() != CK_IntegralCast) break;
    expr = impcast->getSubExpr();
  }
  return expr->getType();
}

StmtResult
Sema::ActOnStartOfSwitchStmt(SourceLocation SwitchLoc, Expr *Cond,
                             Decl *CondVar) {
  ExprResult CondResult;

  VarDecl *ConditionVar = 0;
  if (CondVar) {
    ConditionVar = cast<VarDecl>(CondVar);
    CondResult = CheckConditionVariable(ConditionVar, SourceLocation(), false);
    if (CondResult.isInvalid())
      return StmtError();

    Cond = CondResult.release();
  }

  if (!Cond)
    return StmtError();

  CondResult
    = ConvertToIntegralOrEnumerationType(SwitchLoc, Cond,
                          PDiag(diag::err_typecheck_statement_requires_integer),
                                   PDiag(diag::err_switch_incomplete_class_type)
                                     << Cond->getSourceRange(),
                                   PDiag(diag::err_switch_explicit_conversion),
                                         PDiag(diag::note_switch_conversion),
                                   PDiag(diag::err_switch_multiple_conversions),
                                         PDiag(diag::note_switch_conversion),
                                         PDiag(0),
                                         /*AllowScopedEnumerations*/ true);
  if (CondResult.isInvalid()) return StmtError();
  Cond = CondResult.take();

  // C99 6.8.4.2p5 - Integer promotions are performed on the controlling expr.
  CondResult = UsualUnaryConversions(Cond);
  if (CondResult.isInvalid()) return StmtError();
  Cond = CondResult.take();

  if (!CondVar) {
    CheckImplicitConversions(Cond, SwitchLoc);
    CondResult = MaybeCreateExprWithCleanups(Cond);
    if (CondResult.isInvalid())
      return StmtError();
    Cond = CondResult.take();
  }

  getCurFunction()->setHasBranchIntoScope();

  SwitchStmt *SS = new (Context) SwitchStmt(Context, ConditionVar, Cond);
  getCurFunction()->SwitchStack.push_back(SS);
  return Owned(SS);
}

static void AdjustAPSInt(llvm::APSInt &Val, unsigned BitWidth, bool IsSigned) {
  if (Val.getBitWidth() < BitWidth)
    Val = Val.extend(BitWidth);
  else if (Val.getBitWidth() > BitWidth)
    Val = Val.trunc(BitWidth);
  Val.setIsSigned(IsSigned);
}

StmtResult
Sema::ActOnFinishSwitchStmt(SourceLocation SwitchLoc, Stmt *Switch,
                            Stmt *BodyStmt) {
  SwitchStmt *SS = cast<SwitchStmt>(Switch);
  assert(SS == getCurFunction()->SwitchStack.back() &&
         "switch stack missing push/pop!");

  SS->setBody(BodyStmt, SwitchLoc);
  getCurFunction()->SwitchStack.pop_back();

  Expr *CondExpr = SS->getCond();
  if (!CondExpr) return StmtError();

  QualType CondType = CondExpr->getType();

  Expr *CondExprBeforePromotion = CondExpr;
  QualType CondTypeBeforePromotion =
      GetTypeBeforeIntegralPromotion(CondExprBeforePromotion);

  // C++ 6.4.2.p2:
  // Integral promotions are performed (on the switch condition).
  //
  // A case value unrepresentable by the original switch condition
  // type (before the promotion) doesn't make sense, even when it can
  // be represented by the promoted type.  Therefore we need to find
  // the pre-promotion type of the switch condition.
  if (!CondExpr->isTypeDependent()) {
    // We have already converted the expression to an integral or enumeration
    // type, when we started the switch statement. If we don't have an
    // appropriate type now, just return an error.
    if (!CondType->isIntegralOrEnumerationType())
      return StmtError();

    if (CondExpr->isKnownToHaveBooleanValue()) {
      // switch(bool_expr) {...} is often a programmer error, e.g.
      //   switch(n && mask) { ... }  // Doh - should be "n & mask".
      // One can always use an if statement instead of switch(bool_expr).
      Diag(SwitchLoc, diag::warn_bool_switch_condition)
          << CondExpr->getSourceRange();
    }
  }

  // Get the bitwidth of the switched-on value before promotions.  We must
  // convert the integer case values to this width before comparison.
  bool HasDependentValue
    = CondExpr->isTypeDependent() || CondExpr->isValueDependent();
  unsigned CondWidth
    = HasDependentValue ? 0 : Context.getIntWidth(CondTypeBeforePromotion);
  bool CondIsSigned 
    = CondTypeBeforePromotion->isSignedIntegerOrEnumerationType();

  // Accumulate all of the case values in a vector so that we can sort them
  // and detect duplicates.  This vector contains the APInt for the case after
  // it has been converted to the condition type.
  typedef SmallVector<std::pair<llvm::APSInt, CaseStmt*>, 64> CaseValsTy;
  CaseValsTy CaseVals;

  // Keep track of any GNU case ranges we see.  The APSInt is the low value.
  typedef std::vector<std::pair<llvm::APSInt, CaseStmt*> > CaseRangesTy;
  CaseRangesTy CaseRanges;

  DefaultStmt *TheDefaultStmt = 0;

  bool CaseListIsErroneous = false;

  for (SwitchCase *SC = SS->getSwitchCaseList(); SC && !HasDependentValue;
       SC = SC->getNextSwitchCase()) {

    if (DefaultStmt *DS = dyn_cast<DefaultStmt>(SC)) {
      if (TheDefaultStmt) {
        Diag(DS->getDefaultLoc(), diag::err_multiple_default_labels_defined);
        Diag(TheDefaultStmt->getDefaultLoc(), diag::note_duplicate_case_prev);

        // FIXME: Remove the default statement from the switch block so that
        // we'll return a valid AST.  This requires recursing down the AST and
        // finding it, not something we are set up to do right now.  For now,
        // just lop the entire switch stmt out of the AST.
        CaseListIsErroneous = true;
      }
      TheDefaultStmt = DS;

    } else {
      CaseStmt *CS = cast<CaseStmt>(SC);

      Expr *Lo = CS->getLHS();

      if (Lo->isTypeDependent() || Lo->isValueDependent()) {
        HasDependentValue = true;
        break;
      }

      llvm::APSInt LoVal;

      if (getLangOpts().CPlusPlus0x) {
        // C++11 [stmt.switch]p2: the constant-expression shall be a converted
        // constant expression of the promoted type of the switch condition.
        ExprResult ConvLo =
          CheckConvertedConstantExpression(Lo, CondType, LoVal, CCEK_CaseValue);
        if (ConvLo.isInvalid()) {
          CaseListIsErroneous = true;
          continue;
        }
        Lo = ConvLo.take();
      } else {
        // We already verified that the expression has a i-c-e value (C99
        // 6.8.4.2p3) - get that value now.
        LoVal = Lo->EvaluateKnownConstInt(Context);

        // If the LHS is not the same type as the condition, insert an implicit
        // cast.
        Lo = DefaultLvalueConversion(Lo).take();
        Lo = ImpCastExprToType(Lo, CondType, CK_IntegralCast).take();
      }

      // Convert the value to the same width/sign as the condition had prior to
      // integral promotions.
      //
      // FIXME: This causes us to reject valid code:
      //   switch ((char)c) { case 256: case 0: return 0; }
      // Here we claim there is a duplicated condition value, but there is not.
      ConvertIntegerToTypeWarnOnOverflow(LoVal, CondWidth, CondIsSigned,
                                         Lo->getLocStart(),
                                         diag::warn_case_value_overflow);

      CS->setLHS(Lo);

      // If this is a case range, remember it in CaseRanges, otherwise CaseVals.
      if (CS->getRHS()) {
        if (CS->getRHS()->isTypeDependent() ||
            CS->getRHS()->isValueDependent()) {
          HasDependentValue = true;
          break;
        }
        CaseRanges.push_back(std::make_pair(LoVal, CS));
      } else
        CaseVals.push_back(std::make_pair(LoVal, CS));
    }
  }

  if (!HasDependentValue) {
    // If we don't have a default statement, check whether the
    // condition is constant.
    llvm::APSInt ConstantCondValue;
    bool HasConstantCond = false;
    if (!HasDependentValue && !TheDefaultStmt) {
      HasConstantCond
        = CondExprBeforePromotion->EvaluateAsInt(ConstantCondValue, Context,
                                                 Expr::SE_AllowSideEffects);
      assert(!HasConstantCond ||
             (ConstantCondValue.getBitWidth() == CondWidth &&
              ConstantCondValue.isSigned() == CondIsSigned));
    }
    bool ShouldCheckConstantCond = HasConstantCond;

    // Sort all the scalar case values so we can easily detect duplicates.
    std::stable_sort(CaseVals.begin(), CaseVals.end(), CmpCaseVals);

    if (!CaseVals.empty()) {
      for (unsigned i = 0, e = CaseVals.size(); i != e; ++i) {
        if (ShouldCheckConstantCond &&
            CaseVals[i].first == ConstantCondValue)
          ShouldCheckConstantCond = false;

        if (i != 0 && CaseVals[i].first == CaseVals[i-1].first) {
          // If we have a duplicate, report it.
          Diag(CaseVals[i].second->getLHS()->getLocStart(),
               diag::err_duplicate_case) << CaseVals[i].first.toString(10);
          Diag(CaseVals[i-1].second->getLHS()->getLocStart(),
               diag::note_duplicate_case_prev);
          // FIXME: We really want to remove the bogus case stmt from the
          // substmt, but we have no way to do this right now.
          CaseListIsErroneous = true;
        }
      }
    }

    // Detect duplicate case ranges, which usually don't exist at all in
    // the first place.
    if (!CaseRanges.empty()) {
      // Sort all the case ranges by their low value so we can easily detect
      // overlaps between ranges.
      std::stable_sort(CaseRanges.begin(), CaseRanges.end());

      // Scan the ranges, computing the high values and removing empty ranges.
      std::vector<llvm::APSInt> HiVals;
      for (unsigned i = 0, e = CaseRanges.size(); i != e; ++i) {
        llvm::APSInt &LoVal = CaseRanges[i].first;
        CaseStmt *CR = CaseRanges[i].second;
        Expr *Hi = CR->getRHS();
        llvm::APSInt HiVal;

        if (getLangOpts().CPlusPlus0x) {
          // C++11 [stmt.switch]p2: the constant-expression shall be a converted
          // constant expression of the promoted type of the switch condition.
          ExprResult ConvHi =
            CheckConvertedConstantExpression(Hi, CondType, HiVal,
                                             CCEK_CaseValue);
          if (ConvHi.isInvalid()) {
            CaseListIsErroneous = true;
            continue;
          }
          Hi = ConvHi.take();
        } else {
          HiVal = Hi->EvaluateKnownConstInt(Context);

          // If the RHS is not the same type as the condition, insert an
          // implicit cast.
          Hi = DefaultLvalueConversion(Hi).take();
          Hi = ImpCastExprToType(Hi, CondType, CK_IntegralCast).take();
        }

        // Convert the value to the same width/sign as the condition.
        ConvertIntegerToTypeWarnOnOverflow(HiVal, CondWidth, CondIsSigned,
                                           Hi->getLocStart(),
                                           diag::warn_case_value_overflow);

        CR->setRHS(Hi);

        // If the low value is bigger than the high value, the case is empty.
        if (LoVal > HiVal) {
          Diag(CR->getLHS()->getLocStart(), diag::warn_case_empty_range)
            << SourceRange(CR->getLHS()->getLocStart(),
                           Hi->getLocEnd());
          CaseRanges.erase(CaseRanges.begin()+i);
          --i, --e;
          continue;
        }

        if (ShouldCheckConstantCond &&
            LoVal <= ConstantCondValue &&
            ConstantCondValue <= HiVal)
          ShouldCheckConstantCond = false;

        HiVals.push_back(HiVal);
      }

      // Rescan the ranges, looking for overlap with singleton values and other
      // ranges.  Since the range list is sorted, we only need to compare case
      // ranges with their neighbors.
      for (unsigned i = 0, e = CaseRanges.size(); i != e; ++i) {
        llvm::APSInt &CRLo = CaseRanges[i].first;
        llvm::APSInt &CRHi = HiVals[i];
        CaseStmt *CR = CaseRanges[i].second;

        // Check to see whether the case range overlaps with any
        // singleton cases.
        CaseStmt *OverlapStmt = 0;
        llvm::APSInt OverlapVal(32);

        // Find the smallest value >= the lower bound.  If I is in the
        // case range, then we have overlap.
        CaseValsTy::iterator I = std::lower_bound(CaseVals.begin(),
                                                  CaseVals.end(), CRLo,
                                                  CaseCompareFunctor());
        if (I != CaseVals.end() && I->first < CRHi) {
          OverlapVal  = I->first;   // Found overlap with scalar.
          OverlapStmt = I->second;
        }

        // Find the smallest value bigger than the upper bound.
        I = std::upper_bound(I, CaseVals.end(), CRHi, CaseCompareFunctor());
        if (I != CaseVals.begin() && (I-1)->first >= CRLo) {
          OverlapVal  = (I-1)->first;      // Found overlap with scalar.
          OverlapStmt = (I-1)->second;
        }

        // Check to see if this case stmt overlaps with the subsequent
        // case range.
        if (i && CRLo <= HiVals[i-1]) {
          OverlapVal  = HiVals[i-1];       // Found overlap with range.
          OverlapStmt = CaseRanges[i-1].second;
        }

        if (OverlapStmt) {
          // If we have a duplicate, report it.
          Diag(CR->getLHS()->getLocStart(), diag::err_duplicate_case)
            << OverlapVal.toString(10);
          Diag(OverlapStmt->getLHS()->getLocStart(),
               diag::note_duplicate_case_prev);
          // FIXME: We really want to remove the bogus case stmt from the
          // substmt, but we have no way to do this right now.
          CaseListIsErroneous = true;
        }
      }
    }

    // Complain if we have a constant condition and we didn't find a match.
    if (!CaseListIsErroneous && ShouldCheckConstantCond) {
      // TODO: it would be nice if we printed enums as enums, chars as
      // chars, etc.
      Diag(CondExpr->getExprLoc(), diag::warn_missing_case_for_condition)
        << ConstantCondValue.toString(10)
        << CondExpr->getSourceRange();
    }

    // Check to see if switch is over an Enum and handles all of its
    // values.  We only issue a warning if there is not 'default:', but
    // we still do the analysis to preserve this information in the AST
    // (which can be used by flow-based analyes).
    //
    const EnumType *ET = CondTypeBeforePromotion->getAs<EnumType>();

    // If switch has default case, then ignore it.
    if (!CaseListIsErroneous  && !HasConstantCond && ET) {
      const EnumDecl *ED = ET->getDecl();
      typedef SmallVector<std::pair<llvm::APSInt, EnumConstantDecl*>, 64>
        EnumValsTy;
      EnumValsTy EnumVals;

      // Gather all enum values, set their type and sort them,
      // allowing easier comparison with CaseVals.
      for (EnumDecl::enumerator_iterator EDI = ED->enumerator_begin();
           EDI != ED->enumerator_end(); ++EDI) {
        llvm::APSInt Val = EDI->getInitVal();
        AdjustAPSInt(Val, CondWidth, CondIsSigned);
        EnumVals.push_back(std::make_pair(Val, *EDI));
      }
      std::stable_sort(EnumVals.begin(), EnumVals.end(), CmpEnumVals);
      EnumValsTy::iterator EIend =
        std::unique(EnumVals.begin(), EnumVals.end(), EqEnumVals);

      // See which case values aren't in enum.
      EnumValsTy::const_iterator EI = EnumVals.begin();
      for (CaseValsTy::const_iterator CI = CaseVals.begin();
           CI != CaseVals.end(); CI++) {
        while (EI != EIend && EI->first < CI->first)
          EI++;
        if (EI == EIend || EI->first > CI->first)
          Diag(CI->second->getLHS()->getExprLoc(), diag::warn_not_in_enum)
            << CondTypeBeforePromotion;
      }
      // See which of case ranges aren't in enum
      EI = EnumVals.begin();
      for (CaseRangesTy::const_iterator RI = CaseRanges.begin();
           RI != CaseRanges.end() && EI != EIend; RI++) {
        while (EI != EIend && EI->first < RI->first)
          EI++;

        if (EI == EIend || EI->first != RI->first) {
          Diag(RI->second->getLHS()->getExprLoc(), diag::warn_not_in_enum)
            << CondTypeBeforePromotion;
        }

        llvm::APSInt Hi = 
          RI->second->getRHS()->EvaluateKnownConstInt(Context);
        AdjustAPSInt(Hi, CondWidth, CondIsSigned);
        while (EI != EIend && EI->first < Hi)
          EI++;
        if (EI == EIend || EI->first != Hi)
          Diag(RI->second->getRHS()->getExprLoc(), diag::warn_not_in_enum)
            << CondTypeBeforePromotion;
      }

      // Check which enum vals aren't in switch
      CaseValsTy::const_iterator CI = CaseVals.begin();
      CaseRangesTy::const_iterator RI = CaseRanges.begin();
      bool hasCasesNotInSwitch = false;

      SmallVector<DeclarationName,8> UnhandledNames;

      for (EI = EnumVals.begin(); EI != EIend; EI++){
        // Drop unneeded case values
        llvm::APSInt CIVal;
        while (CI != CaseVals.end() && CI->first < EI->first)
          CI++;

        if (CI != CaseVals.end() && CI->first == EI->first)
          continue;

        // Drop unneeded case ranges
        for (; RI != CaseRanges.end(); RI++) {
          llvm::APSInt Hi =
            RI->second->getRHS()->EvaluateKnownConstInt(Context);
          AdjustAPSInt(Hi, CondWidth, CondIsSigned);
          if (EI->first <= Hi)
            break;
        }

        if (RI == CaseRanges.end() || EI->first < RI->first) {
          hasCasesNotInSwitch = true;
          UnhandledNames.push_back(EI->second->getDeclName());
        }
      }

      if (TheDefaultStmt && UnhandledNames.empty())
        Diag(TheDefaultStmt->getDefaultLoc(), diag::warn_unreachable_default);

      // Produce a nice diagnostic if multiple values aren't handled.
      switch (UnhandledNames.size()) {
      case 0: break;
      case 1:
        Diag(CondExpr->getExprLoc(), TheDefaultStmt 
          ? diag::warn_def_missing_case1 : diag::warn_missing_case1)
          << UnhandledNames[0];
        break;
      case 2:
        Diag(CondExpr->getExprLoc(), TheDefaultStmt 
          ? diag::warn_def_missing_case2 : diag::warn_missing_case2)
          << UnhandledNames[0] << UnhandledNames[1];
        break;
      case 3:
        Diag(CondExpr->getExprLoc(), TheDefaultStmt
          ? diag::warn_def_missing_case3 : diag::warn_missing_case3)
          << UnhandledNames[0] << UnhandledNames[1] << UnhandledNames[2];
        break;
      default:
        Diag(CondExpr->getExprLoc(), TheDefaultStmt
          ? diag::warn_def_missing_cases : diag::warn_missing_cases)
          << (unsigned)UnhandledNames.size()
          << UnhandledNames[0] << UnhandledNames[1] << UnhandledNames[2];
        break;
      }

      if (!hasCasesNotInSwitch)
        SS->setAllEnumCasesCovered();
    }
  }

  DiagnoseEmptyStmtBody(CondExpr->getLocEnd(), BodyStmt,
                        diag::warn_empty_switch_body);

  // FIXME: If the case list was broken is some way, we don't have a good system
  // to patch it up.  Instead, just return the whole substmt as broken.
  if (CaseListIsErroneous)
    return StmtError();

  return Owned(SS);
}

StmtResult
Sema::ActOnWhileStmt(SourceLocation WhileLoc, FullExprArg Cond,
                     Decl *CondVar, Stmt *Body) {
  ExprResult CondResult(Cond.release());

  VarDecl *ConditionVar = 0;
  if (CondVar) {
    ConditionVar = cast<VarDecl>(CondVar);
    CondResult = CheckConditionVariable(ConditionVar, WhileLoc, true);
    if (CondResult.isInvalid())
      return StmtError();
  }
  Expr *ConditionExpr = CondResult.take();
  if (!ConditionExpr)
    return StmtError();

  DiagnoseUnusedExprResult(Body);

  if (isa<NullStmt>(Body))
    getCurCompoundScope().setHasEmptyLoopBodies();

  return Owned(new (Context) WhileStmt(Context, ConditionVar, ConditionExpr,
                                       Body, WhileLoc));
}

StmtResult
Sema::ActOnDoStmt(SourceLocation DoLoc, Stmt *Body,
                  SourceLocation WhileLoc, SourceLocation CondLParen,
                  Expr *Cond, SourceLocation CondRParen) {
  assert(Cond && "ActOnDoStmt(): missing expression");

  ExprResult CondResult = CheckBooleanCondition(Cond, DoLoc);
  if (CondResult.isInvalid() || CondResult.isInvalid())
    return StmtError();
  Cond = CondResult.take();

  CheckImplicitConversions(Cond, DoLoc);
  CondResult = MaybeCreateExprWithCleanups(Cond);
  if (CondResult.isInvalid())
    return StmtError();
  Cond = CondResult.take();

  DiagnoseUnusedExprResult(Body);

  return Owned(new (Context) DoStmt(Body, Cond, DoLoc, WhileLoc, CondRParen));
}

StmtResult
Sema::ActOnForStmt(SourceLocation ForLoc, SourceLocation LParenLoc,
                   Stmt *First, FullExprArg second, Decl *secondVar,
                   FullExprArg third,
                   SourceLocation RParenLoc, Stmt *Body) {
  if (!getLangOpts().CPlusPlus) {
    if (DeclStmt *DS = dyn_cast_or_null<DeclStmt>(First)) {
      // C99 6.8.5p3: The declaration part of a 'for' statement shall only
      // declare identifiers for objects having storage class 'auto' or
      // 'register'.
      for (DeclStmt::decl_iterator DI=DS->decl_begin(), DE=DS->decl_end();
           DI!=DE; ++DI) {
        VarDecl *VD = dyn_cast<VarDecl>(*DI);
        if (VD && VD->isLocalVarDecl() && !VD->hasLocalStorage())
          VD = 0;
        if (VD == 0)
          Diag((*DI)->getLocation(), diag::err_non_variable_decl_in_for);
        // FIXME: mark decl erroneous!
      }
    }
  }

  ExprResult SecondResult(second.release());
  VarDecl *ConditionVar = 0;
  if (secondVar) {
    ConditionVar = cast<VarDecl>(secondVar);
    SecondResult = CheckConditionVariable(ConditionVar, ForLoc, true);
    if (SecondResult.isInvalid())
      return StmtError();
  }

  Expr *Third  = third.release().takeAs<Expr>();

  DiagnoseUnusedExprResult(First);
  DiagnoseUnusedExprResult(Third);
  DiagnoseUnusedExprResult(Body);

  if (isa<NullStmt>(Body))
    getCurCompoundScope().setHasEmptyLoopBodies();

  return Owned(new (Context) ForStmt(Context, First,
                                     SecondResult.take(), ConditionVar,
                                     Third, Body, ForLoc, LParenLoc,
                                     RParenLoc));
}

/// In an Objective C collection iteration statement:
///   for (x in y)
/// x can be an arbitrary l-value expression.  Bind it up as a
/// full-expression.
StmtResult Sema::ActOnForEachLValueExpr(Expr *E) {
  // Reduce placeholder expressions here.  Note that this rejects the
  // use of pseudo-object l-values in this position.
  ExprResult result = CheckPlaceholderExpr(E);
  if (result.isInvalid()) return StmtError();
  E = result.take();

  CheckImplicitConversions(E);

  result = MaybeCreateExprWithCleanups(E);
  if (result.isInvalid()) return StmtError();

  return Owned(static_cast<Stmt*>(result.take()));
}

ExprResult
Sema::ActOnObjCForCollectionOperand(SourceLocation forLoc, Expr *collection) {
  assert(collection);

  // Bail out early if we've got a type-dependent expression.
  if (collection->isTypeDependent()) return Owned(collection);

  // Perform normal l-value conversion.
  ExprResult result = DefaultFunctionArrayLvalueConversion(collection);
  if (result.isInvalid())
    return ExprError();
  collection = result.take();

  // The operand needs to have object-pointer type.
  // TODO: should we do a contextual conversion?
  const ObjCObjectPointerType *pointerType =
    collection->getType()->getAs<ObjCObjectPointerType>();
  if (!pointerType)
    return Diag(forLoc, diag::err_collection_expr_type)
             << collection->getType() << collection->getSourceRange();

  // Check that the operand provides
  //   - countByEnumeratingWithState:objects:count:
  const ObjCObjectType *objectType = pointerType->getObjectType();
  ObjCInterfaceDecl *iface = objectType->getInterface();

  // If we have a forward-declared type, we can't do this check.
  // Under ARC, it is an error not to have a forward-declared class.
  if (iface && 
      RequireCompleteType(forLoc, QualType(objectType, 0),
                          getLangOpts().ObjCAutoRefCount
                            ? PDiag(diag::err_arc_collection_forward)
                                << collection->getSourceRange()
                          : PDiag(0))) {
    // Otherwise, if we have any useful type information, check that
    // the type declares the appropriate method.
  } else if (iface || !objectType->qual_empty()) {
    IdentifierInfo *selectorIdents[] = {
      &Context.Idents.get("countByEnumeratingWithState"),
      &Context.Idents.get("objects"),
      &Context.Idents.get("count")
    };
    Selector selector = Context.Selectors.getSelector(3, &selectorIdents[0]);

    ObjCMethodDecl *method = 0;

    // If there's an interface, look in both the public and private APIs.
    if (iface) {
      method = iface->lookupInstanceMethod(selector);
      if (!method) method = LookupPrivateInstanceMethod(selector, iface);
    }

    // Also check protocol qualifiers.
    if (!method)
      method = LookupMethodInQualifiedType(selector, pointerType,
                                           /*instance*/ true);

    // If we didn't find it anywhere, give up.
    if (!method) {
      Diag(forLoc, diag::warn_collection_expr_type)
        << collection->getType() << selector << collection->getSourceRange();
    }

    // TODO: check for an incompatible signature?
  }

  // Wrap up any cleanups in the expression.
  return Owned(MaybeCreateExprWithCleanups(collection));
}

StmtResult
Sema::ActOnObjCForCollectionStmt(SourceLocation ForLoc,
                                 SourceLocation LParenLoc,
                                 Stmt *First, Expr *Second,
                                 SourceLocation RParenLoc, Stmt *Body) {
  if (First) {
    QualType FirstType;
    if (DeclStmt *DS = dyn_cast<DeclStmt>(First)) {
      if (!DS->isSingleDecl())
        return StmtError(Diag((*DS->decl_begin())->getLocation(),
                         diag::err_toomany_element_decls));

      VarDecl *D = cast<VarDecl>(DS->getSingleDecl());
      FirstType = D->getType();
      // C99 6.8.5p3: The declaration part of a 'for' statement shall only
      // declare identifiers for objects having storage class 'auto' or
      // 'register'.
      if (!D->hasLocalStorage())
        return StmtError(Diag(D->getLocation(),
                              diag::err_non_variable_decl_in_for));
    } else {
      Expr *FirstE = cast<Expr>(First);
      if (!FirstE->isTypeDependent() && !FirstE->isLValue())
        return StmtError(Diag(First->getLocStart(),
                   diag::err_selector_element_not_lvalue)
          << First->getSourceRange());

      FirstType = static_cast<Expr*>(First)->getType();
    }
    if (!FirstType->isDependentType() &&
        !FirstType->isObjCObjectPointerType() &&
        !FirstType->isBlockPointerType())
        Diag(ForLoc, diag::err_selector_element_type)
          << FirstType << First->getSourceRange();
  }

  return Owned(new (Context) ObjCForCollectionStmt(First, Second, Body,
                                                   ForLoc, RParenLoc));
}

namespace {

enum BeginEndFunction {
  BEF_begin,
  BEF_end
};

/// Build a variable declaration for a for-range statement.
static VarDecl *BuildForRangeVarDecl(Sema &SemaRef, SourceLocation Loc,
                                     QualType Type, const char *Name) {
  DeclContext *DC = SemaRef.CurContext;
  IdentifierInfo *II = &SemaRef.PP.getIdentifierTable().get(Name);
  TypeSourceInfo *TInfo = SemaRef.Context.getTrivialTypeSourceInfo(Type, Loc);
  VarDecl *Decl = VarDecl::Create(SemaRef.Context, DC, Loc, Loc, II, Type,
                                  TInfo, SC_Auto, SC_None);
  Decl->setImplicit();
  return Decl;
}

/// Finish building a variable declaration for a for-range statement.
/// \return true if an error occurs.
static bool FinishForRangeVarDecl(Sema &SemaRef, VarDecl *Decl, Expr *Init,
                                  SourceLocation Loc, int diag) {
  // Deduce the type for the iterator variable now rather than leaving it to
  // AddInitializerToDecl, so we can produce a more suitable diagnostic.
  TypeSourceInfo *InitTSI = 0;
  if ((!isa<InitListExpr>(Init) && Init->getType()->isVoidType()) ||
      SemaRef.DeduceAutoType(Decl->getTypeSourceInfo(), Init, InitTSI) ==
          Sema::DAR_Failed)
    SemaRef.Diag(Loc, diag) << Init->getType();
  if (!InitTSI) {
    Decl->setInvalidDecl();
    return true;
  }
  Decl->setTypeSourceInfo(InitTSI);
  Decl->setType(InitTSI->getType());

  // In ARC, infer lifetime.
  // FIXME: ARC may want to turn this into 'const __unsafe_unretained' if
  // we're doing the equivalent of fast iteration.
  if (SemaRef.getLangOpts().ObjCAutoRefCount && 
      SemaRef.inferObjCARCLifetime(Decl))
    Decl->setInvalidDecl();

  SemaRef.AddInitializerToDecl(Decl, Init, /*DirectInit=*/false,
                               /*TypeMayContainAuto=*/false);
  SemaRef.FinalizeDeclaration(Decl);
  SemaRef.CurContext->addHiddenDecl(Decl);
  return false;
}

/// Produce a note indicating which begin/end function was implicitly called
/// by a C++0x for-range statement. This is often not obvious from the code,
/// nor from the diagnostics produced when analysing the implicit expressions
/// required in a for-range statement.
void NoteForRangeBeginEndFunction(Sema &SemaRef, Expr *E,
                                  BeginEndFunction BEF) {
  CallExpr *CE = dyn_cast<CallExpr>(E);
  if (!CE)
    return;
  FunctionDecl *D = dyn_cast<FunctionDecl>(CE->getCalleeDecl());
  if (!D)
    return;
  SourceLocation Loc = D->getLocation();

  std::string Description;
  bool IsTemplate = false;
  if (FunctionTemplateDecl *FunTmpl = D->getPrimaryTemplate()) {
    Description = SemaRef.getTemplateArgumentBindingsText(
      FunTmpl->getTemplateParameters(), *D->getTemplateSpecializationArgs());
    IsTemplate = true;
  }

  SemaRef.Diag(Loc, diag::note_for_range_begin_end)
    << BEF << IsTemplate << Description << E->getType();
}

/// Build a call to 'begin' or 'end' for a C++0x for-range statement. If the
/// given LookupResult is non-empty, it is assumed to describe a member which
/// will be invoked. Otherwise, the function will be found via argument
/// dependent lookup.
static ExprResult BuildForRangeBeginEndCall(Sema &SemaRef, Scope *S,
                                            SourceLocation Loc,
                                            VarDecl *Decl,
                                            BeginEndFunction BEF,
                                            const DeclarationNameInfo &NameInfo,
                                            LookupResult &MemberLookup,
                                            Expr *Range) {
  ExprResult CallExpr;
  if (!MemberLookup.empty()) {
    ExprResult MemberRef =
      SemaRef.BuildMemberReferenceExpr(Range, Range->getType(), Loc,
                                       /*IsPtr=*/false, CXXScopeSpec(),
                                       /*TemplateKWLoc=*/SourceLocation(),
                                       /*FirstQualifierInScope=*/0,
                                       MemberLookup,
                                       /*TemplateArgs=*/0);
    if (MemberRef.isInvalid())
      return ExprError();
    CallExpr = SemaRef.ActOnCallExpr(S, MemberRef.get(), Loc, MultiExprArg(),
                                     Loc, 0);
    if (CallExpr.isInvalid())
      return ExprError();
  } else {
    UnresolvedSet<0> FoundNames;
    // C++0x [stmt.ranged]p1: For the purposes of this name lookup, namespace
    // std is an associated namespace.
    UnresolvedLookupExpr *Fn =
      UnresolvedLookupExpr::Create(SemaRef.Context, /*NamingClass=*/0,
                                   NestedNameSpecifierLoc(), NameInfo,
                                   /*NeedsADL=*/true, /*Overloaded=*/false,
                                   FoundNames.begin(), FoundNames.end(),
                                   /*LookInStdNamespace=*/true);
    CallExpr = SemaRef.BuildOverloadedCallExpr(S, Fn, Fn, Loc, &Range, 1, Loc,
                                               0, /*AllowTypoCorrection=*/false);
    if (CallExpr.isInvalid()) {
      SemaRef.Diag(Range->getLocStart(), diag::note_for_range_type)
        << Range->getType();
      return ExprError();
    }
  }
  if (FinishForRangeVarDecl(SemaRef, Decl, CallExpr.get(), Loc,
                            diag::err_for_range_iter_deduction_failure)) {
    NoteForRangeBeginEndFunction(SemaRef, CallExpr.get(), BEF);
    return ExprError();
  }
  return CallExpr;
}

}

/// ActOnCXXForRangeStmt - Check and build a C++0x for-range statement.
///
/// C++0x [stmt.ranged]:
///   A range-based for statement is equivalent to
///
///   {
///     auto && __range = range-init;
///     for ( auto __begin = begin-expr,
///           __end = end-expr;
///           __begin != __end;
///           ++__begin ) {
///       for-range-declaration = *__begin;
///       statement
///     }
///   }
///
/// The body of the loop is not available yet, since it cannot be analysed until
/// we have determined the type of the for-range-declaration.
StmtResult
Sema::ActOnCXXForRangeStmt(SourceLocation ForLoc, SourceLocation LParenLoc,
                           Stmt *First, SourceLocation ColonLoc, Expr *Range,
                           SourceLocation RParenLoc) {
  if (!First || !Range)
    return StmtError();

  DeclStmt *DS = dyn_cast<DeclStmt>(First);
  assert(DS && "first part of for range not a decl stmt");

  if (!DS->isSingleDecl()) {
    Diag(DS->getStartLoc(), diag::err_type_defined_in_for_range);
    return StmtError();
  }
  if (DS->getSingleDecl()->isInvalidDecl())
    return StmtError();

  if (DiagnoseUnexpandedParameterPack(Range, UPPC_Expression))
    return StmtError();

  // Build  auto && __range = range-init
  SourceLocation RangeLoc = Range->getLocStart();
  VarDecl *RangeVar = BuildForRangeVarDecl(*this, RangeLoc,
                                           Context.getAutoRRefDeductType(),
                                           "__range");
  if (FinishForRangeVarDecl(*this, RangeVar, Range, RangeLoc,
                            diag::err_for_range_deduction_failure))
    return StmtError();

  // Claim the type doesn't contain auto: we've already done the checking.
  DeclGroupPtrTy RangeGroup =
    BuildDeclaratorGroup((Decl**)&RangeVar, 1, /*TypeMayContainAuto=*/false);
  StmtResult RangeDecl = ActOnDeclStmt(RangeGroup, RangeLoc, RangeLoc);
  if (RangeDecl.isInvalid())
    return StmtError();

  return BuildCXXForRangeStmt(ForLoc, ColonLoc, RangeDecl.get(),
                              /*BeginEndDecl=*/0, /*Cond=*/0, /*Inc=*/0, DS,
                              RParenLoc);
}

/// BuildCXXForRangeStmt - Build or instantiate a C++0x for-range statement.
StmtResult
Sema::BuildCXXForRangeStmt(SourceLocation ForLoc, SourceLocation ColonLoc,
                           Stmt *RangeDecl, Stmt *BeginEnd, Expr *Cond,
                           Expr *Inc, Stmt *LoopVarDecl,
                           SourceLocation RParenLoc) {
  Scope *S = getCurScope();

  DeclStmt *RangeDS = cast<DeclStmt>(RangeDecl);
  VarDecl *RangeVar = cast<VarDecl>(RangeDS->getSingleDecl());
  QualType RangeVarType = RangeVar->getType();

  DeclStmt *LoopVarDS = cast<DeclStmt>(LoopVarDecl);
  VarDecl *LoopVar = cast<VarDecl>(LoopVarDS->getSingleDecl());

  StmtResult BeginEndDecl = BeginEnd;
  ExprResult NotEqExpr = Cond, IncrExpr = Inc;

  if (!BeginEndDecl.get() && !RangeVarType->isDependentType()) {
    SourceLocation RangeLoc = RangeVar->getLocation();

    const QualType RangeVarNonRefType = RangeVarType.getNonReferenceType();

    ExprResult BeginRangeRef = BuildDeclRefExpr(RangeVar, RangeVarNonRefType,
                                                VK_LValue, ColonLoc);
    if (BeginRangeRef.isInvalid())
      return StmtError();

    ExprResult EndRangeRef = BuildDeclRefExpr(RangeVar, RangeVarNonRefType,
                                              VK_LValue, ColonLoc);
    if (EndRangeRef.isInvalid())
      return StmtError();

    QualType AutoType = Context.getAutoDeductType();
    Expr *Range = RangeVar->getInit();
    if (!Range)
      return StmtError();
    QualType RangeType = Range->getType();

    if (RequireCompleteType(RangeLoc, RangeType,
                            PDiag(diag::err_for_range_incomplete_type)))
      return StmtError();

    // Build auto __begin = begin-expr, __end = end-expr.
    VarDecl *BeginVar = BuildForRangeVarDecl(*this, ColonLoc, AutoType,
                                             "__begin");
    VarDecl *EndVar = BuildForRangeVarDecl(*this, ColonLoc, AutoType,
                                           "__end");

    // Build begin-expr and end-expr and attach to __begin and __end variables.
    ExprResult BeginExpr, EndExpr;
    if (const ArrayType *UnqAT = RangeType->getAsArrayTypeUnsafe()) {
      // - if _RangeT is an array type, begin-expr and end-expr are __range and
      //   __range + __bound, respectively, where __bound is the array bound. If
      //   _RangeT is an array of unknown size or an array of incomplete type,
      //   the program is ill-formed;

      // begin-expr is __range.
      BeginExpr = BeginRangeRef;
      if (FinishForRangeVarDecl(*this, BeginVar, BeginRangeRef.get(), ColonLoc,
                                diag::err_for_range_iter_deduction_failure)) {
        NoteForRangeBeginEndFunction(*this, BeginExpr.get(), BEF_begin);
        return StmtError();
      }

      // Find the array bound.
      ExprResult BoundExpr;
      if (const ConstantArrayType *CAT = dyn_cast<ConstantArrayType>(UnqAT))
        BoundExpr = Owned(IntegerLiteral::Create(Context, CAT->getSize(),
                                                 Context.getPointerDiffType(),
                                                 RangeLoc));
      else if (const VariableArrayType *VAT =
               dyn_cast<VariableArrayType>(UnqAT))
        BoundExpr = VAT->getSizeExpr();
      else {
        // Can't be a DependentSizedArrayType or an IncompleteArrayType since
        // UnqAT is not incomplete and Range is not type-dependent.
        llvm_unreachable("Unexpected array type in for-range");
      }

      // end-expr is __range + __bound.
      EndExpr = ActOnBinOp(S, ColonLoc, tok::plus, EndRangeRef.get(),
                           BoundExpr.get());
      if (EndExpr.isInvalid())
        return StmtError();
      if (FinishForRangeVarDecl(*this, EndVar, EndExpr.get(), ColonLoc,
                                diag::err_for_range_iter_deduction_failure)) {
        NoteForRangeBeginEndFunction(*this, EndExpr.get(), BEF_end);
        return StmtError();
      }
    } else {
      DeclarationNameInfo BeginNameInfo(&PP.getIdentifierTable().get("begin"),
                                        ColonLoc);
      DeclarationNameInfo EndNameInfo(&PP.getIdentifierTable().get("end"),
                                      ColonLoc);

      LookupResult BeginMemberLookup(*this, BeginNameInfo, LookupMemberName);
      LookupResult EndMemberLookup(*this, EndNameInfo, LookupMemberName);

      if (CXXRecordDecl *D = RangeType->getAsCXXRecordDecl()) {
        // - if _RangeT is a class type, the unqualified-ids begin and end are
        //   looked up in the scope of class _RangeT as if by class member access
        //   lookup (3.4.5), and if either (or both) finds at least one
        //   declaration, begin-expr and end-expr are __range.begin() and
        //   __range.end(), respectively;
        LookupQualifiedName(BeginMemberLookup, D);
        LookupQualifiedName(EndMemberLookup, D);

        if (BeginMemberLookup.empty() != EndMemberLookup.empty()) {
          Diag(ColonLoc, diag::err_for_range_member_begin_end_mismatch)
            << RangeType << BeginMemberLookup.empty();
          return StmtError();
        }
      } else {
        // - otherwise, begin-expr and end-expr are begin(__range) and
        //   end(__range), respectively, where begin and end are looked up with
        //   argument-dependent lookup (3.4.2). For the purposes of this name
        //   lookup, namespace std is an associated namespace.
      }

      BeginExpr = BuildForRangeBeginEndCall(*this, S, ColonLoc, BeginVar,
                                            BEF_begin, BeginNameInfo,
                                            BeginMemberLookup,
                                            BeginRangeRef.get());
      if (BeginExpr.isInvalid())
        return StmtError();

      EndExpr = BuildForRangeBeginEndCall(*this, S, ColonLoc, EndVar,
                                          BEF_end, EndNameInfo,
                                          EndMemberLookup, EndRangeRef.get());
      if (EndExpr.isInvalid())
        return StmtError();
    }

    // C++0x [decl.spec.auto]p6: BeginType and EndType must be the same.
    QualType BeginType = BeginVar->getType(), EndType = EndVar->getType();
    if (!Context.hasSameType(BeginType, EndType)) {
      Diag(RangeLoc, diag::err_for_range_begin_end_types_differ)
        << BeginType << EndType;
      NoteForRangeBeginEndFunction(*this, BeginExpr.get(), BEF_begin);
      NoteForRangeBeginEndFunction(*this, EndExpr.get(), BEF_end);
    }

    Decl *BeginEndDecls[] = { BeginVar, EndVar };
    // Claim the type doesn't contain auto: we've already done the checking.
    DeclGroupPtrTy BeginEndGroup =
      BuildDeclaratorGroup(BeginEndDecls, 2, /*TypeMayContainAuto=*/false);
    BeginEndDecl = ActOnDeclStmt(BeginEndGroup, ColonLoc, ColonLoc);

    const QualType BeginRefNonRefType = BeginType.getNonReferenceType();
    ExprResult BeginRef = BuildDeclRefExpr(BeginVar, BeginRefNonRefType,
                                           VK_LValue, ColonLoc);
    if (BeginRef.isInvalid())
      return StmtError();

    ExprResult EndRef = BuildDeclRefExpr(EndVar, EndType.getNonReferenceType(),
                                         VK_LValue, ColonLoc);
    if (EndRef.isInvalid())
      return StmtError();

    // Build and check __begin != __end expression.
    NotEqExpr = ActOnBinOp(S, ColonLoc, tok::exclaimequal,
                           BeginRef.get(), EndRef.get());
    NotEqExpr = ActOnBooleanCondition(S, ColonLoc, NotEqExpr.get());
    NotEqExpr = ActOnFinishFullExpr(NotEqExpr.get());
    if (NotEqExpr.isInvalid()) {
      NoteForRangeBeginEndFunction(*this, BeginExpr.get(), BEF_begin);
      if (!Context.hasSameType(BeginType, EndType))
        NoteForRangeBeginEndFunction(*this, EndExpr.get(), BEF_end);
      return StmtError();
    }

    // Build and check ++__begin expression.
    BeginRef = BuildDeclRefExpr(BeginVar, BeginRefNonRefType,
                                VK_LValue, ColonLoc);
    if (BeginRef.isInvalid())
      return StmtError();

    IncrExpr = ActOnUnaryOp(S, ColonLoc, tok::plusplus, BeginRef.get());
    IncrExpr = ActOnFinishFullExpr(IncrExpr.get());
    if (IncrExpr.isInvalid()) {
      NoteForRangeBeginEndFunction(*this, BeginExpr.get(), BEF_begin);
      return StmtError();
    }

    // Build and check *__begin  expression.
    BeginRef = BuildDeclRefExpr(BeginVar, BeginRefNonRefType,
                                VK_LValue, ColonLoc);
    if (BeginRef.isInvalid())
      return StmtError();

    ExprResult DerefExpr = ActOnUnaryOp(S, ColonLoc, tok::star, BeginRef.get());
    if (DerefExpr.isInvalid()) {
      NoteForRangeBeginEndFunction(*this, BeginExpr.get(), BEF_begin);
      return StmtError();
    }

    // Attach  *__begin  as initializer for VD.
    if (!LoopVar->isInvalidDecl()) {
      AddInitializerToDecl(LoopVar, DerefExpr.get(), /*DirectInit=*/false,
                           /*TypeMayContainAuto=*/true);
      if (LoopVar->isInvalidDecl())
        NoteForRangeBeginEndFunction(*this, BeginExpr.get(), BEF_begin);
    }
  } else {
    // The range is implicitly used as a placeholder when it is dependent.
    RangeVar->setUsed();
  }

  return Owned(new (Context) CXXForRangeStmt(RangeDS,
                                     cast_or_null<DeclStmt>(BeginEndDecl.get()),
                                             NotEqExpr.take(), IncrExpr.take(),
                                             LoopVarDS, /*Body=*/0, ForLoc,
                                             ColonLoc, RParenLoc));
}

/// FinishCXXForRangeStmt - Attach the body to a C++0x for-range statement.
/// This is a separate step from ActOnCXXForRangeStmt because analysis of the
/// body cannot be performed until after the type of the range variable is
/// determined.
StmtResult Sema::FinishCXXForRangeStmt(Stmt *S, Stmt *B) {
  if (!S || !B)
    return StmtError();

  CXXForRangeStmt *ForStmt = cast<CXXForRangeStmt>(S);
  ForStmt->setBody(B);

  DiagnoseEmptyStmtBody(ForStmt->getRParenLoc(), B,
                        diag::warn_empty_range_based_for_body);

  return S;
}

StmtResult Sema::ActOnGotoStmt(SourceLocation GotoLoc,
                               SourceLocation LabelLoc,
                               LabelDecl *TheDecl) {
  getCurFunction()->setHasBranchIntoScope();
  TheDecl->setUsed();
  return Owned(new (Context) GotoStmt(TheDecl, GotoLoc, LabelLoc));
}

StmtResult
Sema::ActOnIndirectGotoStmt(SourceLocation GotoLoc, SourceLocation StarLoc,
                            Expr *E) {
  // Convert operand to void*
  if (!E->isTypeDependent()) {
    QualType ETy = E->getType();
    QualType DestTy = Context.getPointerType(Context.VoidTy.withConst());
    ExprResult ExprRes = Owned(E);
    AssignConvertType ConvTy =
      CheckSingleAssignmentConstraints(DestTy, ExprRes);
    if (ExprRes.isInvalid())
      return StmtError();
    E = ExprRes.take();
    if (DiagnoseAssignmentResult(ConvTy, StarLoc, DestTy, ETy, E, AA_Passing))
      return StmtError();
    E = MaybeCreateExprWithCleanups(E);
  }

  getCurFunction()->setHasIndirectGoto();

  return Owned(new (Context) IndirectGotoStmt(GotoLoc, StarLoc, E));
}

StmtResult
Sema::ActOnContinueStmt(SourceLocation ContinueLoc, Scope *CurScope) {
  Scope *S = CurScope->getContinueParent();
  if (!S) {
    // C99 6.8.6.2p1: A break shall appear only in or as a loop body.
    return StmtError(Diag(ContinueLoc, diag::err_continue_not_in_loop));
  }

  return Owned(new (Context) ContinueStmt(ContinueLoc));
}

StmtResult
Sema::ActOnBreakStmt(SourceLocation BreakLoc, Scope *CurScope) {
  Scope *S = CurScope->getBreakParent();
  if (!S) {
    // C99 6.8.6.3p1: A break shall appear only in or as a switch/loop body.
    return StmtError(Diag(BreakLoc, diag::err_break_not_in_loop_or_switch));
  }

  return Owned(new (Context) BreakStmt(BreakLoc));
}

/// \brief Determine whether the given expression is a candidate for
/// copy elision in either a return statement or a throw expression.
///
/// \param ReturnType If we're determining the copy elision candidate for
/// a return statement, this is the return type of the function. If we're
/// determining the copy elision candidate for a throw expression, this will
/// be a NULL type.
///
/// \param E The expression being returned from the function or block, or
/// being thrown.
///
/// \param AllowFunctionParameter Whether we allow function parameters to
/// be considered NRVO candidates. C++ prohibits this for NRVO itself, but
/// we re-use this logic to determine whether we should try to move as part of
/// a return or throw (which does allow function parameters).
///
/// \returns The NRVO candidate variable, if the return statement may use the
/// NRVO, or NULL if there is no such candidate.
const VarDecl *Sema::getCopyElisionCandidate(QualType ReturnType,
                                             Expr *E,
                                             bool AllowFunctionParameter) {
  QualType ExprType = E->getType();
  // - in a return statement in a function with ...
  // ... a class return type ...
  if (!ReturnType.isNull()) {
    if (!ReturnType->isRecordType())
      return 0;
    // ... the same cv-unqualified type as the function return type ...
    if (!Context.hasSameUnqualifiedType(ReturnType, ExprType))
      return 0;
  }

  // ... the expression is the name of a non-volatile automatic object
  // (other than a function or catch-clause parameter)) ...
  const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(E->IgnoreParens());
  if (!DR)
    return 0;
  const VarDecl *VD = dyn_cast<VarDecl>(DR->getDecl());
  if (!VD)
    return 0;

  // ...object (other than a function or catch-clause parameter)...
  if (VD->getKind() != Decl::Var &&
      !(AllowFunctionParameter && VD->getKind() == Decl::ParmVar))
    return 0;
  if (VD->isExceptionVariable()) return 0;

  // ...automatic...
  if (!VD->hasLocalStorage()) return 0;

  // ...non-volatile...
  if (VD->getType().isVolatileQualified()) return 0;
  if (VD->getType()->isReferenceType()) return 0;

  // __block variables can't be allocated in a way that permits NRVO.
  if (VD->hasAttr<BlocksAttr>()) return 0;

  // Variables with higher required alignment than their type's ABI
  // alignment cannot use NRVO.
  if (VD->hasAttr<AlignedAttr>() &&
      Context.getDeclAlign(VD) > Context.getTypeAlignInChars(VD->getType()))
    return 0;

  return VD;
}

/// \brief Perform the initialization of a potentially-movable value, which
/// is the result of return value.
///
/// This routine implements C++0x [class.copy]p33, which attempts to treat
/// returned lvalues as rvalues in certain cases (to prefer move construction),
/// then falls back to treating them as lvalues if that failed.
ExprResult
Sema::PerformMoveOrCopyInitialization(const InitializedEntity &Entity,
                                      const VarDecl *NRVOCandidate,
                                      QualType ResultType,
                                      Expr *Value,
                                      bool AllowNRVO) {
  // C++0x [class.copy]p33:
  //   When the criteria for elision of a copy operation are met or would
  //   be met save for the fact that the source object is a function
  //   parameter, and the object to be copied is designated by an lvalue,
  //   overload resolution to select the constructor for the copy is first
  //   performed as if the object were designated by an rvalue.
  ExprResult Res = ExprError();
  if (AllowNRVO &&
      (NRVOCandidate || getCopyElisionCandidate(ResultType, Value, true))) {
    ImplicitCastExpr AsRvalue(ImplicitCastExpr::OnStack,
                              Value->getType(), CK_LValueToRValue,
                              Value, VK_XValue);

    Expr *InitExpr = &AsRvalue;
    InitializationKind Kind
      = InitializationKind::CreateCopy(Value->getLocStart(),
                                       Value->getLocStart());
    InitializationSequence Seq(*this, Entity, Kind, &InitExpr, 1);

    //   [...] If overload resolution fails, or if the type of the first
    //   parameter of the selected constructor is not an rvalue reference
    //   to the object's type (possibly cv-qualified), overload resolution
    //   is performed again, considering the object as an lvalue.
    if (Seq) {
      for (InitializationSequence::step_iterator Step = Seq.step_begin(),
           StepEnd = Seq.step_end();
           Step != StepEnd; ++Step) {
        if (Step->Kind != InitializationSequence::SK_ConstructorInitialization)
          continue;

        CXXConstructorDecl *Constructor
        = cast<CXXConstructorDecl>(Step->Function.Function);

        const RValueReferenceType *RRefType
          = Constructor->getParamDecl(0)->getType()
                                                 ->getAs<RValueReferenceType>();

        // If we don't meet the criteria, break out now.
        if (!RRefType ||
            !Context.hasSameUnqualifiedType(RRefType->getPointeeType(),
                            Context.getTypeDeclType(Constructor->getParent())))
          break;

        // Promote "AsRvalue" to the heap, since we now need this
        // expression node to persist.
        Value = ImplicitCastExpr::Create(Context, Value->getType(),
                                         CK_LValueToRValue, Value, 0,
                                         VK_XValue);

        // Complete type-checking the initialization of the return type
        // using the constructor we found.
        Res = Seq.Perform(*this, Entity, Kind, MultiExprArg(&Value, 1));
      }
    }
  }

  // Either we didn't meet the criteria for treating an lvalue as an rvalue,
  // above, or overload resolution failed. Either way, we need to try
  // (again) now with the return value expression as written.
  if (Res.isInvalid())
    Res = PerformCopyInitialization(Entity, SourceLocation(), Value);

  return Res;
}

/// ActOnCapScopeReturnStmt - Utility routine to type-check return statements
/// for capturing scopes.
///
StmtResult
Sema::ActOnCapScopeReturnStmt(SourceLocation ReturnLoc, Expr *RetValExp) {
  // If this is the first return we've seen, infer the return type.
  // [expr.prim.lambda]p4 in C++11; block literals follow a superset of those
  // rules which allows multiple return statements.
  CapturingScopeInfo *CurCap = cast<CapturingScopeInfo>(getCurFunction());
  if (CurCap->HasImplicitReturnType) {
    QualType ReturnT;
    if (RetValExp && !isa<InitListExpr>(RetValExp)) {
      ExprResult Result = DefaultFunctionArrayLvalueConversion(RetValExp);
      if (Result.isInvalid())
        return StmtError();
      RetValExp = Result.take();

      if (!RetValExp->isTypeDependent())
        ReturnT = RetValExp->getType();
      else
        ReturnT = Context.DependentTy;
    } else { 
      if (RetValExp) {
        // C++11 [expr.lambda.prim]p4 bans inferring the result from an
        // initializer list, because it is not an expression (even
        // though we represent it as one). We still deduce 'void'.
        Diag(ReturnLoc, diag::err_lambda_return_init_list)
          << RetValExp->getSourceRange();
      }

      ReturnT = Context.VoidTy;
    }
    // We require the return types to strictly match here.
    if (!CurCap->ReturnType.isNull() &&
        !CurCap->ReturnType->isDependentType() &&
        !ReturnT->isDependentType() &&
        !Context.hasSameType(ReturnT, CurCap->ReturnType)) { 
      Diag(ReturnLoc, diag::err_typecheck_missing_return_type_incompatible) 
          << ReturnT << CurCap->ReturnType
          << (getCurLambda() != 0);
      return StmtError();
    }
    CurCap->ReturnType = ReturnT;
  }
  QualType FnRetType = CurCap->ReturnType;
  assert(!FnRetType.isNull());

  if (BlockScopeInfo *CurBlock = dyn_cast<BlockScopeInfo>(CurCap)) {
    if (CurBlock->FunctionType->getAs<FunctionType>()->getNoReturnAttr()) {
      Diag(ReturnLoc, diag::err_noreturn_block_has_return_expr);
      return StmtError();
    }
  } else {
    LambdaScopeInfo *LSI = cast<LambdaScopeInfo>(CurCap);
    if (LSI->CallOperator->getType()->getAs<FunctionType>()->getNoReturnAttr()){
      Diag(ReturnLoc, diag::err_noreturn_lambda_has_return_expr);
      return StmtError();
    }
  }

  // Otherwise, verify that this result type matches the previous one.  We are
  // pickier with blocks than for normal functions because we don't have GCC
  // compatibility to worry about here.
  const VarDecl *NRVOCandidate = 0;
  if (FnRetType->isDependentType()) {
    // Delay processing for now.  TODO: there are lots of dependent
    // types we can conclusively prove aren't void.
  } else if (FnRetType->isVoidType()) {
    if (RetValExp && !isa<InitListExpr>(RetValExp) &&
        !(getLangOpts().CPlusPlus &&
          (RetValExp->isTypeDependent() ||
           RetValExp->getType()->isVoidType()))) {
      if (!getLangOpts().CPlusPlus &&
          RetValExp->getType()->isVoidType())
        Diag(ReturnLoc, diag::ext_return_has_void_expr) << "literal" << 2;
      else {
        Diag(ReturnLoc, diag::err_return_block_has_expr);
        RetValExp = 0;
      }
    }
  } else if (!RetValExp) {
    return StmtError(Diag(ReturnLoc, diag::err_block_return_missing_expr));
  } else if (!RetValExp->isTypeDependent()) {
    // we have a non-void block with an expression, continue checking

    // C99 6.8.6.4p3(136): The return statement is not an assignment. The
    // overlap restriction of subclause 6.5.16.1 does not apply to the case of
    // function return.

    // In C++ the return statement is handled via a copy initialization.
    // the C version of which boils down to CheckSingleAssignmentConstraints.
    NRVOCandidate = getCopyElisionCandidate(FnRetType, RetValExp, false);
    InitializedEntity Entity = InitializedEntity::InitializeResult(ReturnLoc,
                                                                   FnRetType,
                                                          NRVOCandidate != 0);
    ExprResult Res = PerformMoveOrCopyInitialization(Entity, NRVOCandidate,
                                                     FnRetType, RetValExp);
    if (Res.isInvalid()) {
      // FIXME: Cleanup temporaries here, anyway?
      return StmtError();
    }
    RetValExp = Res.take();
    CheckReturnStackAddr(RetValExp, FnRetType, ReturnLoc);
  }

  if (RetValExp) {
    CheckImplicitConversions(RetValExp, ReturnLoc);
    RetValExp = MaybeCreateExprWithCleanups(RetValExp);
  }
  ReturnStmt *Result = new (Context) ReturnStmt(ReturnLoc, RetValExp,
                                                NRVOCandidate);

  // If we need to check for the named return value optimization, save the
  // return statement in our scope for later processing.
  if (getLangOpts().CPlusPlus && FnRetType->isRecordType() && 
      !CurContext->isDependentContext())
    FunctionScopes.back()->Returns.push_back(Result);

  return Owned(Result);
}

StmtResult
Sema::ActOnReturnStmt(SourceLocation ReturnLoc, Expr *RetValExp) {
  // Check for unexpanded parameter packs.
  if (RetValExp && DiagnoseUnexpandedParameterPack(RetValExp))
    return StmtError();
  
  if (isa<CapturingScopeInfo>(getCurFunction()))
    return ActOnCapScopeReturnStmt(ReturnLoc, RetValExp);

  QualType FnRetType;
  QualType RelatedRetType;
  if (const FunctionDecl *FD = getCurFunctionDecl()) {
    FnRetType = FD->getResultType();
    if (FD->hasAttr<NoReturnAttr>() ||
        FD->getType()->getAs<FunctionType>()->getNoReturnAttr())
      Diag(ReturnLoc, diag::warn_noreturn_function_has_return_expr)
        << FD->getDeclName();
  } else if (ObjCMethodDecl *MD = getCurMethodDecl()) {
    FnRetType = MD->getResultType();
    if (MD->hasRelatedResultType() && MD->getClassInterface()) {
      // In the implementation of a method with a related return type, the
      // type used to type-check the validity of return statements within the 
      // method body is a pointer to the type of the class being implemented.
      RelatedRetType = Context.getObjCInterfaceType(MD->getClassInterface());
      RelatedRetType = Context.getObjCObjectPointerType(RelatedRetType);
    }
  } else // If we don't have a function/method context, bail.
    return StmtError();

  ReturnStmt *Result = 0;
  if (FnRetType->isVoidType()) {
    if (RetValExp) {
      if (isa<InitListExpr>(RetValExp)) {
        // We simply never allow init lists as the return value of void
        // functions. This is compatible because this was never allowed before,
        // so there's no legacy code to deal with.
        NamedDecl *CurDecl = getCurFunctionOrMethodDecl();
        int FunctionKind = 0;
        if (isa<ObjCMethodDecl>(CurDecl))
          FunctionKind = 1;
        else if (isa<CXXConstructorDecl>(CurDecl))
          FunctionKind = 2;
        else if (isa<CXXDestructorDecl>(CurDecl))
          FunctionKind = 3;

        Diag(ReturnLoc, diag::err_return_init_list)
          << CurDecl->getDeclName() << FunctionKind
          << RetValExp->getSourceRange();

        // Drop the expression.
        RetValExp = 0;
      } else if (!RetValExp->isTypeDependent()) {
        // C99 6.8.6.4p1 (ext_ since GCC warns)
        unsigned D = diag::ext_return_has_expr;
        if (RetValExp->getType()->isVoidType())
          D = diag::ext_return_has_void_expr;
        else {
          ExprResult Result = Owned(RetValExp);
          Result = IgnoredValueConversions(Result.take());
          if (Result.isInvalid())
            return StmtError();
          RetValExp = Result.take();
          RetValExp = ImpCastExprToType(RetValExp,
                                        Context.VoidTy, CK_ToVoid).take();
        }

        // return (some void expression); is legal in C++.
        if (D != diag::ext_return_has_void_expr ||
            !getLangOpts().CPlusPlus) {
          NamedDecl *CurDecl = getCurFunctionOrMethodDecl();

          int FunctionKind = 0;
          if (isa<ObjCMethodDecl>(CurDecl))
            FunctionKind = 1;
          else if (isa<CXXConstructorDecl>(CurDecl))
            FunctionKind = 2;
          else if (isa<CXXDestructorDecl>(CurDecl))
            FunctionKind = 3;

          Diag(ReturnLoc, D)
            << CurDecl->getDeclName() << FunctionKind
            << RetValExp->getSourceRange();
        }
      }

      if (RetValExp) {
        CheckImplicitConversions(RetValExp, ReturnLoc);
        RetValExp = MaybeCreateExprWithCleanups(RetValExp);
      }
    }

    Result = new (Context) ReturnStmt(ReturnLoc, RetValExp, 0);
  } else if (!RetValExp && !FnRetType->isDependentType()) {
    unsigned DiagID = diag::warn_return_missing_expr;  // C90 6.6.6.4p4
    // C99 6.8.6.4p1 (ext_ since GCC warns)
    if (getLangOpts().C99) DiagID = diag::ext_return_missing_expr;

    if (FunctionDecl *FD = getCurFunctionDecl())
      Diag(ReturnLoc, DiagID) << FD->getIdentifier() << 0/*fn*/;
    else
      Diag(ReturnLoc, DiagID) << getCurMethodDecl()->getDeclName() << 1/*meth*/;
    Result = new (Context) ReturnStmt(ReturnLoc);
  } else {
    const VarDecl *NRVOCandidate = 0;
    if (!FnRetType->isDependentType() && !RetValExp->isTypeDependent()) {
      // we have a non-void function with an expression, continue checking

      if (!RelatedRetType.isNull()) {
        // If we have a related result type, perform an extra conversion here.
        // FIXME: The diagnostics here don't really describe what is happening.
        InitializedEntity Entity =
            InitializedEntity::InitializeTemporary(RelatedRetType);
        
        ExprResult Res = PerformCopyInitialization(Entity, SourceLocation(),
                                                   RetValExp);
        if (Res.isInvalid()) {
          // FIXME: Cleanup temporaries here, anyway?
          return StmtError();
        }
        RetValExp = Res.takeAs<Expr>();
      }

      // C99 6.8.6.4p3(136): The return statement is not an assignment. The
      // overlap restriction of subclause 6.5.16.1 does not apply to the case of
      // function return.

      // In C++ the return statement is handled via a copy initialization,
      // the C version of which boils down to CheckSingleAssignmentConstraints.
      NRVOCandidate = getCopyElisionCandidate(FnRetType, RetValExp, false);
      InitializedEntity Entity = InitializedEntity::InitializeResult(ReturnLoc,
                                                                     FnRetType,
                                                            NRVOCandidate != 0);
      ExprResult Res = PerformMoveOrCopyInitialization(Entity, NRVOCandidate,
                                                       FnRetType, RetValExp);
      if (Res.isInvalid()) {
        // FIXME: Cleanup temporaries here, anyway?
        return StmtError();
      }

      RetValExp = Res.takeAs<Expr>();
      if (RetValExp)
        CheckReturnStackAddr(RetValExp, FnRetType, ReturnLoc);
    }

    if (RetValExp) {
      CheckImplicitConversions(RetValExp, ReturnLoc);
      RetValExp = MaybeCreateExprWithCleanups(RetValExp);
    }
    Result = new (Context) ReturnStmt(ReturnLoc, RetValExp, NRVOCandidate);
  }

  // If we need to check for the named return value optimization, save the
  // return statement in our scope for later processing.
  if (getLangOpts().CPlusPlus && FnRetType->isRecordType() &&
      !CurContext->isDependentContext())
    FunctionScopes.back()->Returns.push_back(Result);
  
  return Owned(Result);
}

/// CheckAsmLValue - GNU C has an extremely ugly extension whereby they silently
/// ignore "noop" casts in places where an lvalue is required by an inline asm.
/// We emulate this behavior when -fheinous-gnu-extensions is specified, but
/// provide a strong guidance to not use it.
///
/// This method checks to see if the argument is an acceptable l-value and
/// returns false if it is a case we can handle.
static bool CheckAsmLValue(const Expr *E, Sema &S) {
  // Type dependent expressions will be checked during instantiation.
  if (E->isTypeDependent())
    return false;

  if (E->isLValue())
    return false;  // Cool, this is an lvalue.

  // Okay, this is not an lvalue, but perhaps it is the result of a cast that we
  // are supposed to allow.
  const Expr *E2 = E->IgnoreParenNoopCasts(S.Context);
  if (E != E2 && E2->isLValue()) {
    if (!S.getLangOpts().HeinousExtensions)
      S.Diag(E2->getLocStart(), diag::err_invalid_asm_cast_lvalue)
        << E->getSourceRange();
    else
      S.Diag(E2->getLocStart(), diag::warn_invalid_asm_cast_lvalue)
        << E->getSourceRange();
    // Accept, even if we emitted an error diagnostic.
    return false;
  }

  // None of the above, just randomly invalid non-lvalue.
  return true;
}

/// isOperandMentioned - Return true if the specified operand # is mentioned
/// anywhere in the decomposed asm string.
static bool isOperandMentioned(unsigned OpNo, 
                         ArrayRef<AsmStmt::AsmStringPiece> AsmStrPieces) {
  for (unsigned p = 0, e = AsmStrPieces.size(); p != e; ++p) {
    const AsmStmt::AsmStringPiece &Piece = AsmStrPieces[p];
    if (!Piece.isOperand()) continue;
    
    // If this is a reference to the input and if the input was the smaller
    // one, then we have to reject this asm.
    if (Piece.getOperandNo() == OpNo)
      return true;
  }
 
  return false;
}

StmtResult Sema::ActOnAsmStmt(SourceLocation AsmLoc, bool IsSimple,
                              bool IsVolatile, unsigned NumOutputs,
                              unsigned NumInputs, IdentifierInfo **Names,
                              MultiExprArg constraints, MultiExprArg exprs,
                              Expr *asmString, MultiExprArg clobbers,
                              SourceLocation RParenLoc, bool MSAsm) {
  unsigned NumClobbers = clobbers.size();
  StringLiteral **Constraints =
    reinterpret_cast<StringLiteral**>(constraints.get());
  Expr **Exprs = exprs.get();
  StringLiteral *AsmString = cast<StringLiteral>(asmString);
  StringLiteral **Clobbers = reinterpret_cast<StringLiteral**>(clobbers.get());

  SmallVector<TargetInfo::ConstraintInfo, 4> OutputConstraintInfos;

  // The parser verifies that there is a string literal here.
  if (!AsmString->isAscii())
    return StmtError(Diag(AsmString->getLocStart(),diag::err_asm_wide_character)
      << AsmString->getSourceRange());

  for (unsigned i = 0; i != NumOutputs; i++) {
    StringLiteral *Literal = Constraints[i];
    if (!Literal->isAscii())
      return StmtError(Diag(Literal->getLocStart(),diag::err_asm_wide_character)
        << Literal->getSourceRange());

    StringRef OutputName;
    if (Names[i])
      OutputName = Names[i]->getName();

    TargetInfo::ConstraintInfo Info(Literal->getString(), OutputName);
    if (!Context.getTargetInfo().validateOutputConstraint(Info))
      return StmtError(Diag(Literal->getLocStart(),
                            diag::err_asm_invalid_output_constraint)
                       << Info.getConstraintStr());

    // Check that the output exprs are valid lvalues.
    Expr *OutputExpr = Exprs[i];
    if (CheckAsmLValue(OutputExpr, *this)) {
      return StmtError(Diag(OutputExpr->getLocStart(),
                  diag::err_asm_invalid_lvalue_in_output)
        << OutputExpr->getSourceRange());
    }

    OutputConstraintInfos.push_back(Info);
  }

  SmallVector<TargetInfo::ConstraintInfo, 4> InputConstraintInfos;

  for (unsigned i = NumOutputs, e = NumOutputs + NumInputs; i != e; i++) {
    StringLiteral *Literal = Constraints[i];
    if (!Literal->isAscii())
      return StmtError(Diag(Literal->getLocStart(),diag::err_asm_wide_character)
        << Literal->getSourceRange());

    StringRef InputName;
    if (Names[i])
      InputName = Names[i]->getName();

    TargetInfo::ConstraintInfo Info(Literal->getString(), InputName);
    if (!Context.getTargetInfo().validateInputConstraint(OutputConstraintInfos.data(),
                                                NumOutputs, Info)) {
      return StmtError(Diag(Literal->getLocStart(),
                            diag::err_asm_invalid_input_constraint)
                       << Info.getConstraintStr());
    }

    Expr *InputExpr = Exprs[i];

    // Only allow void types for memory constraints.
    if (Info.allowsMemory() && !Info.allowsRegister()) {
      if (CheckAsmLValue(InputExpr, *this))
        return StmtError(Diag(InputExpr->getLocStart(),
                              diag::err_asm_invalid_lvalue_in_input)
                         << Info.getConstraintStr()
                         << InputExpr->getSourceRange());
    }

    if (Info.allowsRegister()) {
      if (InputExpr->getType()->isVoidType()) {
        return StmtError(Diag(InputExpr->getLocStart(),
                              diag::err_asm_invalid_type_in_input)
          << InputExpr->getType() << Info.getConstraintStr()
          << InputExpr->getSourceRange());
      }
    }

    ExprResult Result = DefaultFunctionArrayLvalueConversion(Exprs[i]);
    if (Result.isInvalid())
      return StmtError();

    Exprs[i] = Result.take();
    InputConstraintInfos.push_back(Info);
  }

  // Check that the clobbers are valid.
  for (unsigned i = 0; i != NumClobbers; i++) {
    StringLiteral *Literal = Clobbers[i];
    if (!Literal->isAscii())
      return StmtError(Diag(Literal->getLocStart(),diag::err_asm_wide_character)
        << Literal->getSourceRange());

    StringRef Clobber = Literal->getString();

    if (!Context.getTargetInfo().isValidClobber(Clobber))
      return StmtError(Diag(Literal->getLocStart(),
                  diag::err_asm_unknown_register_name) << Clobber);
  }

  AsmStmt *NS =
    new (Context) AsmStmt(Context, AsmLoc, IsSimple, IsVolatile, MSAsm,
                          NumOutputs, NumInputs, Names, Constraints, Exprs,
                          AsmString, NumClobbers, Clobbers, RParenLoc);
  // Validate the asm string, ensuring it makes sense given the operands we
  // have.
  SmallVector<AsmStmt::AsmStringPiece, 8> Pieces;
  unsigned DiagOffs;
  if (unsigned DiagID = NS->AnalyzeAsmString(Pieces, Context, DiagOffs)) {
    Diag(getLocationOfStringLiteralByte(AsmString, DiagOffs), DiagID)
           << AsmString->getSourceRange();
    return StmtError();
  }

  // Validate tied input operands for type mismatches.
  for (unsigned i = 0, e = InputConstraintInfos.size(); i != e; ++i) {
    TargetInfo::ConstraintInfo &Info = InputConstraintInfos[i];

    // If this is a tied constraint, verify that the output and input have
    // either exactly the same type, or that they are int/ptr operands with the
    // same size (int/long, int*/long, are ok etc).
    if (!Info.hasTiedOperand()) continue;

    unsigned TiedTo = Info.getTiedOperand();
    unsigned InputOpNo = i+NumOutputs;
    Expr *OutputExpr = Exprs[TiedTo];
    Expr *InputExpr = Exprs[InputOpNo];

    if (OutputExpr->isTypeDependent() || InputExpr->isTypeDependent())
      continue;

    QualType InTy = InputExpr->getType();
    QualType OutTy = OutputExpr->getType();
    if (Context.hasSameType(InTy, OutTy))
      continue;  // All types can be tied to themselves.

    // Decide if the input and output are in the same domain (integer/ptr or
    // floating point.
    enum AsmDomain {
      AD_Int, AD_FP, AD_Other
    } InputDomain, OutputDomain;

    if (InTy->isIntegerType() || InTy->isPointerType())
      InputDomain = AD_Int;
    else if (InTy->isRealFloatingType())
      InputDomain = AD_FP;
    else
      InputDomain = AD_Other;

    if (OutTy->isIntegerType() || OutTy->isPointerType())
      OutputDomain = AD_Int;
    else if (OutTy->isRealFloatingType())
      OutputDomain = AD_FP;
    else
      OutputDomain = AD_Other;

    // They are ok if they are the same size and in the same domain.  This
    // allows tying things like:
    //   void* to int*
    //   void* to int            if they are the same size.
    //   double to long double   if they are the same size.
    //
    uint64_t OutSize = Context.getTypeSize(OutTy);
    uint64_t InSize = Context.getTypeSize(InTy);
    if (OutSize == InSize && InputDomain == OutputDomain &&
        InputDomain != AD_Other)
      continue;

    // If the smaller input/output operand is not mentioned in the asm string,
    // then we can promote the smaller one to a larger input and the asm string
    // won't notice.
    bool SmallerValueMentioned = false;
    
    // If this is a reference to the input and if the input was the smaller
    // one, then we have to reject this asm.
    if (isOperandMentioned(InputOpNo, Pieces)) {
      // This is a use in the asm string of the smaller operand.  Since we
      // codegen this by promoting to a wider value, the asm will get printed
      // "wrong".
      SmallerValueMentioned |= InSize < OutSize;
    }
    if (isOperandMentioned(TiedTo, Pieces)) {
      // If this is a reference to the output, and if the output is the larger
      // value, then it's ok because we'll promote the input to the larger type.
      SmallerValueMentioned |= OutSize < InSize;
    }

    // If the smaller value wasn't mentioned in the asm string, and if the
    // output was a register, just extend the shorter one to the size of the
    // larger one.
    if (!SmallerValueMentioned && InputDomain != AD_Other &&
        OutputConstraintInfos[TiedTo].allowsRegister())
      continue;
    
    // Either both of the operands were mentioned or the smaller one was
    // mentioned.  One more special case that we'll allow: if the tied input is
    // integer, unmentioned, and is a constant, then we'll allow truncating it
    // down to the size of the destination.
    if (InputDomain == AD_Int && OutputDomain == AD_Int &&
        !isOperandMentioned(InputOpNo, Pieces) &&
        InputExpr->isEvaluatable(Context)) {
      CastKind castKind =
        (OutTy->isBooleanType() ? CK_IntegralToBoolean : CK_IntegralCast);
      InputExpr = ImpCastExprToType(InputExpr, OutTy, castKind).take();
      Exprs[InputOpNo] = InputExpr;
      NS->setInputExpr(i, InputExpr);
      continue;
    }
    
    Diag(InputExpr->getLocStart(),
         diag::err_asm_tying_incompatible_types)
      << InTy << OutTy << OutputExpr->getSourceRange()
      << InputExpr->getSourceRange();
    return StmtError();
  }

  return Owned(NS);
}

StmtResult
Sema::ActOnObjCAtCatchStmt(SourceLocation AtLoc,
                           SourceLocation RParen, Decl *Parm,
                           Stmt *Body) {
  VarDecl *Var = cast_or_null<VarDecl>(Parm);
  if (Var && Var->isInvalidDecl())
    return StmtError();

  return Owned(new (Context) ObjCAtCatchStmt(AtLoc, RParen, Var, Body));
}

StmtResult
Sema::ActOnObjCAtFinallyStmt(SourceLocation AtLoc, Stmt *Body) {
  return Owned(new (Context) ObjCAtFinallyStmt(AtLoc, Body));
}

StmtResult
Sema::ActOnObjCAtTryStmt(SourceLocation AtLoc, Stmt *Try,
                         MultiStmtArg CatchStmts, Stmt *Finally) {
  if (!getLangOpts().ObjCExceptions)
    Diag(AtLoc, diag::err_objc_exceptions_disabled) << "@try";

  getCurFunction()->setHasBranchProtectedScope();
  unsigned NumCatchStmts = CatchStmts.size();
  return Owned(ObjCAtTryStmt::Create(Context, AtLoc, Try,
                                     CatchStmts.release(),
                                     NumCatchStmts,
                                     Finally));
}

StmtResult Sema::BuildObjCAtThrowStmt(SourceLocation AtLoc,
                                                  Expr *Throw) {
  if (Throw) {
    Throw = MaybeCreateExprWithCleanups(Throw);
    ExprResult Result = DefaultLvalueConversion(Throw);
    if (Result.isInvalid())
      return StmtError();

    Throw = Result.take();
    QualType ThrowType = Throw->getType();
    // Make sure the expression type is an ObjC pointer or "void *".
    if (!ThrowType->isDependentType() &&
        !ThrowType->isObjCObjectPointerType()) {
      const PointerType *PT = ThrowType->getAs<PointerType>();
      if (!PT || !PT->getPointeeType()->isVoidType())
        return StmtError(Diag(AtLoc, diag::error_objc_throw_expects_object)
                         << Throw->getType() << Throw->getSourceRange());
    }
  }

  return Owned(new (Context) ObjCAtThrowStmt(AtLoc, Throw));
}

StmtResult
Sema::ActOnObjCAtThrowStmt(SourceLocation AtLoc, Expr *Throw,
                           Scope *CurScope) {
  if (!getLangOpts().ObjCExceptions)
    Diag(AtLoc, diag::err_objc_exceptions_disabled) << "@throw";

  if (!Throw) {
    // @throw without an expression designates a rethrow (which much occur
    // in the context of an @catch clause).
    Scope *AtCatchParent = CurScope;
    while (AtCatchParent && !AtCatchParent->isAtCatchScope())
      AtCatchParent = AtCatchParent->getParent();
    if (!AtCatchParent)
      return StmtError(Diag(AtLoc, diag::error_rethrow_used_outside_catch));
  }
  
  return BuildObjCAtThrowStmt(AtLoc, Throw);
}

ExprResult
Sema::ActOnObjCAtSynchronizedOperand(SourceLocation atLoc, Expr *operand) {
  ExprResult result = DefaultLvalueConversion(operand);
  if (result.isInvalid())
    return ExprError();
  operand = result.take();

  // Make sure the expression type is an ObjC pointer or "void *".
  QualType type = operand->getType();
  if (!type->isDependentType() &&
      !type->isObjCObjectPointerType()) {
    const PointerType *pointerType = type->getAs<PointerType>();
    if (!pointerType || !pointerType->getPointeeType()->isVoidType())
      return Diag(atLoc, diag::error_objc_synchronized_expects_object)
               << type << operand->getSourceRange();
  }

  // The operand to @synchronized is a full-expression.
  return MaybeCreateExprWithCleanups(operand);
}

StmtResult
Sema::ActOnObjCAtSynchronizedStmt(SourceLocation AtLoc, Expr *SyncExpr,
                                  Stmt *SyncBody) {
  // We can't jump into or indirect-jump out of a @synchronized block.
  getCurFunction()->setHasBranchProtectedScope();
  return Owned(new (Context) ObjCAtSynchronizedStmt(AtLoc, SyncExpr, SyncBody));
}

/// ActOnCXXCatchBlock - Takes an exception declaration and a handler block
/// and creates a proper catch handler from them.
StmtResult
Sema::ActOnCXXCatchBlock(SourceLocation CatchLoc, Decl *ExDecl,
                         Stmt *HandlerBlock) {
  // There's nothing to test that ActOnExceptionDecl didn't already test.
  return Owned(new (Context) CXXCatchStmt(CatchLoc,
                                          cast_or_null<VarDecl>(ExDecl),
                                          HandlerBlock));
}

StmtResult
Sema::ActOnObjCAutoreleasePoolStmt(SourceLocation AtLoc, Stmt *Body) {
  getCurFunction()->setHasBranchProtectedScope();
  return Owned(new (Context) ObjCAutoreleasePoolStmt(AtLoc, Body));
}

namespace {

class TypeWithHandler {
  QualType t;
  CXXCatchStmt *stmt;
public:
  TypeWithHandler(const QualType &type, CXXCatchStmt *statement)
  : t(type), stmt(statement) {}

  // An arbitrary order is fine as long as it places identical
  // types next to each other.
  bool operator<(const TypeWithHandler &y) const {
    if (t.getAsOpaquePtr() < y.t.getAsOpaquePtr())
      return true;
    if (t.getAsOpaquePtr() > y.t.getAsOpaquePtr())
      return false;
    else
      return getTypeSpecStartLoc() < y.getTypeSpecStartLoc();
  }

  bool operator==(const TypeWithHandler& other) const {
    return t == other.t;
  }

  CXXCatchStmt *getCatchStmt() const { return stmt; }
  SourceLocation getTypeSpecStartLoc() const {
    return stmt->getExceptionDecl()->getTypeSpecStartLoc();
  }
};

}

/// ActOnCXXTryBlock - Takes a try compound-statement and a number of
/// handlers and creates a try statement from them.
StmtResult
Sema::ActOnCXXTryBlock(SourceLocation TryLoc, Stmt *TryBlock,
                       MultiStmtArg RawHandlers) {
  // Don't report an error if 'try' is used in system headers.
  if (!getLangOpts().CXXExceptions &&
      !getSourceManager().isInSystemHeader(TryLoc))
      Diag(TryLoc, diag::err_exceptions_disabled) << "try";

  unsigned NumHandlers = RawHandlers.size();
  assert(NumHandlers > 0 &&
         "The parser shouldn't call this if there are no handlers.");
  Stmt **Handlers = RawHandlers.get();

  SmallVector<TypeWithHandler, 8> TypesWithHandlers;

  for (unsigned i = 0; i < NumHandlers; ++i) {
    CXXCatchStmt *Handler = cast<CXXCatchStmt>(Handlers[i]);
    if (!Handler->getExceptionDecl()) {
      if (i < NumHandlers - 1)
        return StmtError(Diag(Handler->getLocStart(),
                              diag::err_early_catch_all));

      continue;
    }

    const QualType CaughtType = Handler->getCaughtType();
    const QualType CanonicalCaughtType = Context.getCanonicalType(CaughtType);
    TypesWithHandlers.push_back(TypeWithHandler(CanonicalCaughtType, Handler));
  }

  // Detect handlers for the same type as an earlier one.
  if (NumHandlers > 1) {
    llvm::array_pod_sort(TypesWithHandlers.begin(), TypesWithHandlers.end());

    TypeWithHandler prev = TypesWithHandlers[0];
    for (unsigned i = 1; i < TypesWithHandlers.size(); ++i) {
      TypeWithHandler curr = TypesWithHandlers[i];

      if (curr == prev) {
        Diag(curr.getTypeSpecStartLoc(),
             diag::warn_exception_caught_by_earlier_handler)
          << curr.getCatchStmt()->getCaughtType().getAsString();
        Diag(prev.getTypeSpecStartLoc(),
             diag::note_previous_exception_handler)
          << prev.getCatchStmt()->getCaughtType().getAsString();
      }

      prev = curr;
    }
  }

  getCurFunction()->setHasBranchProtectedScope();

  // FIXME: We should detect handlers that cannot catch anything because an
  // earlier handler catches a superclass. Need to find a method that is not
  // quadratic for this.
  // Neither of these are explicitly forbidden, but every compiler detects them
  // and warns.

  return Owned(CXXTryStmt::Create(Context, TryLoc, TryBlock,
                                  Handlers, NumHandlers));
}

StmtResult
Sema::ActOnSEHTryBlock(bool IsCXXTry,
                       SourceLocation TryLoc,
                       Stmt *TryBlock,
                       Stmt *Handler) {
  assert(TryBlock && Handler);

  getCurFunction()->setHasBranchProtectedScope();

  return Owned(SEHTryStmt::Create(Context,IsCXXTry,TryLoc,TryBlock,Handler));
}

StmtResult
Sema::ActOnSEHExceptBlock(SourceLocation Loc,
                          Expr *FilterExpr,
                          Stmt *Block) {
  assert(FilterExpr && Block);

  if(!FilterExpr->getType()->isIntegerType()) {
    return StmtError(Diag(FilterExpr->getExprLoc(),
                     diag::err_filter_expression_integral)
                     << FilterExpr->getType());
  }

  return Owned(SEHExceptStmt::Create(Context,Loc,FilterExpr,Block));
}

StmtResult
Sema::ActOnSEHFinallyBlock(SourceLocation Loc,
                           Stmt *Block) {
  assert(Block);
  return Owned(SEHFinallyStmt::Create(Context,Loc,Block));
}

StmtResult Sema::BuildMSDependentExistsStmt(SourceLocation KeywordLoc,
                                            bool IsIfExists,
                                            NestedNameSpecifierLoc QualifierLoc,
                                            DeclarationNameInfo NameInfo,
                                            Stmt *Nested)
{
  return new (Context) MSDependentExistsStmt(KeywordLoc, IsIfExists,
                                             QualifierLoc, NameInfo, 
                                             cast<CompoundStmt>(Nested));
}


StmtResult Sema::ActOnMSDependentExistsStmt(SourceLocation KeywordLoc, 
                                            bool IsIfExists,
                                            CXXScopeSpec &SS, 
                                            UnqualifiedId &Name,
                                            Stmt *Nested) {
  return BuildMSDependentExistsStmt(KeywordLoc, IsIfExists, 
                                    SS.getWithLocInContext(Context),
                                    GetNameFromUnqualifiedId(Name),
                                    Nested);
}
