//===--- SemaExpr.cpp - Semantic Analysis for Expressions -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for expressions.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/AnalysisBasedWarnings.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Designator.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/SemaFixItUtils.h"
#include "clang/Sema/Template.h"
using namespace clang;
using namespace sema;

/// \brief Determine whether the use of this declaration is valid, without
/// emitting diagnostics.
bool Sema::CanUseDecl(NamedDecl *D) {
  // See if this is an auto-typed variable whose initializer we are parsing.
  if (ParsingInitForAutoVars.count(D))
    return false;

  // See if this is a deleted function.
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->isDeleted())
      return false;
  }
  return true;
}

static AvailabilityResult DiagnoseAvailabilityOfDecl(Sema &S,
                              NamedDecl *D, SourceLocation Loc,
                              const ObjCInterfaceDecl *UnknownObjCClass) {
  // See if this declaration is unavailable or deprecated.
  std::string Message;
  AvailabilityResult Result = D->getAvailability(&Message);
  switch (Result) {
    case AR_Available:
    case AR_NotYetIntroduced:
      break;
            
    case AR_Deprecated:
      S.EmitDeprecationWarning(D, Message, Loc, UnknownObjCClass);
      break;
            
    case AR_Unavailable:
      if (S.getCurContextAvailability() != AR_Unavailable) {
        if (Message.empty()) {
          if (!UnknownObjCClass)
            S.Diag(Loc, diag::err_unavailable) << D->getDeclName();
          else
            S.Diag(Loc, diag::warn_unavailable_fwdclass_message) 
              << D->getDeclName();
        }
        else 
          S.Diag(Loc, diag::err_unavailable_message) 
            << D->getDeclName() << Message;
          S.Diag(D->getLocation(), diag::note_unavailable_here) 
          << isa<FunctionDecl>(D) << false;
      }
      break;
    }
    return Result;
}

/// \brief Determine whether the use of this declaration is valid, and
/// emit any corresponding diagnostics.
///
/// This routine diagnoses various problems with referencing
/// declarations that can occur when using a declaration. For example,
/// it might warn if a deprecated or unavailable declaration is being
/// used, or produce an error (and return true) if a C++0x deleted
/// function is being used.
///
/// \returns true if there was an error (this declaration cannot be
/// referenced), false otherwise.
///
bool Sema::DiagnoseUseOfDecl(NamedDecl *D, SourceLocation Loc,
                             const ObjCInterfaceDecl *UnknownObjCClass) {
  if (getLangOptions().CPlusPlus && isa<FunctionDecl>(D)) {
    // If there were any diagnostics suppressed by template argument deduction,
    // emit them now.
    llvm::DenseMap<Decl *, SmallVector<PartialDiagnosticAt, 1> >::iterator
      Pos = SuppressedDiagnostics.find(D->getCanonicalDecl());
    if (Pos != SuppressedDiagnostics.end()) {
      SmallVectorImpl<PartialDiagnosticAt> &Suppressed = Pos->second;
      for (unsigned I = 0, N = Suppressed.size(); I != N; ++I)
        Diag(Suppressed[I].first, Suppressed[I].second);
      
      // Clear out the list of suppressed diagnostics, so that we don't emit
      // them again for this specialization. However, we don't obsolete this
      // entry from the table, because we want to avoid ever emitting these
      // diagnostics again.
      Suppressed.clear();
    }
  }

  // See if this is an auto-typed variable whose initializer we are parsing.
  if (ParsingInitForAutoVars.count(D)) {
    Diag(Loc, diag::err_auto_variable_cannot_appear_in_own_initializer)
      << D->getDeclName();
    return true;
  }

  // See if this is a deleted function.
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->isDeleted()) {
      Diag(Loc, diag::err_deleted_function_use);
      Diag(D->getLocation(), diag::note_unavailable_here) << 1 << true;
      return true;
    }
  }
  AvailabilityResult Result =
    DiagnoseAvailabilityOfDecl(*this, D, Loc, UnknownObjCClass);

  // Warn if this is used but marked unused.
  if (D->hasAttr<UnusedAttr>())
    Diag(Loc, diag::warn_used_but_marked_unused) << D->getDeclName();
  // For available enumerator, it will become unavailable/deprecated
  // if its enum declaration is as such.
  if (Result == AR_Available)
    if (const EnumConstantDecl *ECD = dyn_cast<EnumConstantDecl>(D)) {
      const DeclContext *DC = ECD->getDeclContext();
      if (const EnumDecl *TheEnumDecl = dyn_cast<EnumDecl>(DC))
        DiagnoseAvailabilityOfDecl(*this,
                          const_cast< EnumDecl *>(TheEnumDecl), 
                          Loc, UnknownObjCClass);
    }
  return false;
}

/// \brief Retrieve the message suffix that should be added to a
/// diagnostic complaining about the given function being deleted or
/// unavailable.
std::string Sema::getDeletedOrUnavailableSuffix(const FunctionDecl *FD) {
  // FIXME: C++0x implicitly-deleted special member functions could be
  // detected here so that we could improve diagnostics to say, e.g.,
  // "base class 'A' had a deleted copy constructor".
  if (FD->isDeleted())
    return std::string();

  std::string Message;
  if (FD->getAvailability(&Message))
    return ": " + Message;

  return std::string();
}

/// DiagnoseSentinelCalls - This routine checks whether a call or
/// message-send is to a declaration with the sentinel attribute, and
/// if so, it checks that the requirements of the sentinel are
/// satisfied.
void Sema::DiagnoseSentinelCalls(NamedDecl *D, SourceLocation Loc,
                                 Expr **args, unsigned numArgs) {
  const SentinelAttr *attr = D->getAttr<SentinelAttr>();
  if (!attr)
    return;

  // The number of formal parameters of the declaration.
  unsigned numFormalParams;

  // The kind of declaration.  This is also an index into a %select in
  // the diagnostic.
  enum CalleeType { CT_Function, CT_Method, CT_Block } calleeType;

  if (ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(D)) {
    numFormalParams = MD->param_size();
    calleeType = CT_Method;
  } else if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    numFormalParams = FD->param_size();
    calleeType = CT_Function;
  } else if (isa<VarDecl>(D)) {
    QualType type = cast<ValueDecl>(D)->getType();
    const FunctionType *fn = 0;
    if (const PointerType *ptr = type->getAs<PointerType>()) {
      fn = ptr->getPointeeType()->getAs<FunctionType>();
      if (!fn) return;
      calleeType = CT_Function;
    } else if (const BlockPointerType *ptr = type->getAs<BlockPointerType>()) {
      fn = ptr->getPointeeType()->castAs<FunctionType>();
      calleeType = CT_Block;
    } else {
      return;
    }

    if (const FunctionProtoType *proto = dyn_cast<FunctionProtoType>(fn)) {
      numFormalParams = proto->getNumArgs();
    } else {
      numFormalParams = 0;
    }
  } else {
    return;
  }

  // "nullPos" is the number of formal parameters at the end which
  // effectively count as part of the variadic arguments.  This is
  // useful if you would prefer to not have *any* formal parameters,
  // but the language forces you to have at least one.
  unsigned nullPos = attr->getNullPos();
  assert((nullPos == 0 || nullPos == 1) && "invalid null position on sentinel");
  numFormalParams = (nullPos > numFormalParams ? 0 : numFormalParams - nullPos);

  // The number of arguments which should follow the sentinel.
  unsigned numArgsAfterSentinel = attr->getSentinel();

  // If there aren't enough arguments for all the formal parameters,
  // the sentinel, and the args after the sentinel, complain.
  if (numArgs < numFormalParams + numArgsAfterSentinel + 1) {
    Diag(Loc, diag::warn_not_enough_argument) << D->getDeclName();
    Diag(D->getLocation(), diag::note_sentinel_here) << calleeType;
    return;
  }

  // Otherwise, find the sentinel expression.
  Expr *sentinelExpr = args[numArgs - numArgsAfterSentinel - 1];
  if (!sentinelExpr) return;
  if (sentinelExpr->isValueDependent()) return;

  // nullptr_t is always treated as null.
  if (sentinelExpr->getType()->isNullPtrType()) return;

  if (sentinelExpr->getType()->isAnyPointerType() &&
      sentinelExpr->IgnoreParenCasts()->isNullPointerConstant(Context,
                                            Expr::NPC_ValueDependentIsNull))
    return;

  // Unfortunately, __null has type 'int'.
  if (isa<GNUNullExpr>(sentinelExpr)) return;

  // Pick a reasonable string to insert.  Optimistically use 'nil' or
  // 'NULL' if those are actually defined in the context.  Only use
  // 'nil' for ObjC methods, where it's much more likely that the
  // variadic arguments form a list of object pointers.
  SourceLocation MissingNilLoc
    = PP.getLocForEndOfToken(sentinelExpr->getLocEnd());
  std::string NullValue;
  if (calleeType == CT_Method &&
      PP.getIdentifierInfo("nil")->hasMacroDefinition())
    NullValue = "nil";
  else if (PP.getIdentifierInfo("NULL")->hasMacroDefinition())
    NullValue = "NULL";
  else
    NullValue = "(void*) 0";

  if (MissingNilLoc.isInvalid())
    Diag(Loc, diag::warn_missing_sentinel) << calleeType;
  else
    Diag(MissingNilLoc, diag::warn_missing_sentinel) 
      << calleeType
      << FixItHint::CreateInsertion(MissingNilLoc, ", " + NullValue);
  Diag(D->getLocation(), diag::note_sentinel_here) << calleeType;
}

SourceRange Sema::getExprRange(Expr *E) const {
  return E ? E->getSourceRange() : SourceRange();
}

//===----------------------------------------------------------------------===//
//  Standard Promotions and Conversions
//===----------------------------------------------------------------------===//

/// DefaultFunctionArrayConversion (C99 6.3.2.1p3, C99 6.3.2.1p4).
ExprResult Sema::DefaultFunctionArrayConversion(Expr *E) {
  // Handle any placeholder expressions which made it here.
  if (E->getType()->isPlaceholderType()) {
    ExprResult result = CheckPlaceholderExpr(E);
    if (result.isInvalid()) return ExprError();
    E = result.take();
  }
  
  QualType Ty = E->getType();
  assert(!Ty.isNull() && "DefaultFunctionArrayConversion - missing type");

  if (Ty->isFunctionType())
    E = ImpCastExprToType(E, Context.getPointerType(Ty),
                          CK_FunctionToPointerDecay).take();
  else if (Ty->isArrayType()) {
    // In C90 mode, arrays only promote to pointers if the array expression is
    // an lvalue.  The relevant legalese is C90 6.2.2.1p3: "an lvalue that has
    // type 'array of type' is converted to an expression that has type 'pointer
    // to type'...".  In C99 this was changed to: C99 6.3.2.1p3: "an expression
    // that has type 'array of type' ...".  The relevant change is "an lvalue"
    // (C90) to "an expression" (C99).
    //
    // C++ 4.2p1:
    // An lvalue or rvalue of type "array of N T" or "array of unknown bound of
    // T" can be converted to an rvalue of type "pointer to T".
    //
    if (getLangOptions().C99 || getLangOptions().CPlusPlus || E->isLValue())
      E = ImpCastExprToType(E, Context.getArrayDecayedType(Ty),
                            CK_ArrayToPointerDecay).take();
  }
  return Owned(E);
}

static void CheckForNullPointerDereference(Sema &S, Expr *E) {
  // Check to see if we are dereferencing a null pointer.  If so,
  // and if not volatile-qualified, this is undefined behavior that the
  // optimizer will delete, so warn about it.  People sometimes try to use this
  // to get a deterministic trap and are surprised by clang's behavior.  This
  // only handles the pattern "*null", which is a very syntactic check.
  if (UnaryOperator *UO = dyn_cast<UnaryOperator>(E->IgnoreParenCasts()))
    if (UO->getOpcode() == UO_Deref &&
        UO->getSubExpr()->IgnoreParenCasts()->
          isNullPointerConstant(S.Context, Expr::NPC_ValueDependentIsNotNull) &&
        !UO->getType().isVolatileQualified()) {
    S.DiagRuntimeBehavior(UO->getOperatorLoc(), UO,
                          S.PDiag(diag::warn_indirection_through_null)
                            << UO->getSubExpr()->getSourceRange());
    S.DiagRuntimeBehavior(UO->getOperatorLoc(), UO,
                        S.PDiag(diag::note_indirection_through_null));
  }
}

ExprResult Sema::DefaultLvalueConversion(Expr *E) {
  // Handle any placeholder expressions which made it here.
  if (E->getType()->isPlaceholderType()) {
    ExprResult result = CheckPlaceholderExpr(E);
    if (result.isInvalid()) return ExprError();
    E = result.take();
  }
  
  // C++ [conv.lval]p1:
  //   A glvalue of a non-function, non-array type T can be
  //   converted to a prvalue.
  if (!E->isGLValue()) return Owned(E);

  QualType T = E->getType();
  assert(!T.isNull() && "r-value conversion on typeless expression?");

  // We can't do lvalue-to-rvalue on atomics yet.
  if (T->getAs<AtomicType>())
    return Owned(E);

  // Create a load out of an ObjCProperty l-value, if necessary.
  if (E->getObjectKind() == OK_ObjCProperty) {
    ExprResult Res = ConvertPropertyForRValue(E);
    if (Res.isInvalid())
      return Owned(E);
    E = Res.take();
    if (!E->isGLValue())
      return Owned(E);
  }

  // We don't want to throw lvalue-to-rvalue casts on top of
  // expressions of certain types in C++.
  if (getLangOptions().CPlusPlus &&
      (E->getType() == Context.OverloadTy ||
       T->isDependentType() ||
       T->isRecordType()))
    return Owned(E);

  // The C standard is actually really unclear on this point, and
  // DR106 tells us what the result should be but not why.  It's
  // generally best to say that void types just doesn't undergo
  // lvalue-to-rvalue at all.  Note that expressions of unqualified
  // 'void' type are never l-values, but qualified void can be.
  if (T->isVoidType())
    return Owned(E);

  CheckForNullPointerDereference(*this, E);

  // C++ [conv.lval]p1:
  //   [...] If T is a non-class type, the type of the prvalue is the
  //   cv-unqualified version of T. Otherwise, the type of the
  //   rvalue is T.
  //
  // C99 6.3.2.1p2:
  //   If the lvalue has qualified type, the value has the unqualified
  //   version of the type of the lvalue; otherwise, the value has the
  //   type of the lvalue.
  if (T.hasQualifiers())
    T = T.getUnqualifiedType();

  ExprResult Res = Owned(ImplicitCastExpr::Create(Context, T, CK_LValueToRValue,
                                                  E, 0, VK_RValue));

  return Res;
}

ExprResult Sema::DefaultFunctionArrayLvalueConversion(Expr *E) {
  ExprResult Res = DefaultFunctionArrayConversion(E);
  if (Res.isInvalid())
    return ExprError();
  Res = DefaultLvalueConversion(Res.take());
  if (Res.isInvalid())
    return ExprError();
  return move(Res);
}


/// UsualUnaryConversions - Performs various conversions that are common to most
/// operators (C99 6.3). The conversions of array and function types are
/// sometimes suppressed. For example, the array->pointer conversion doesn't
/// apply if the array is an argument to the sizeof or address (&) operators.
/// In these instances, this routine should *not* be called.
ExprResult Sema::UsualUnaryConversions(Expr *E) {
  // First, convert to an r-value.
  ExprResult Res = DefaultFunctionArrayLvalueConversion(E);
  if (Res.isInvalid())
    return Owned(E);
  E = Res.take();

  QualType Ty = E->getType();
  assert(!Ty.isNull() && "UsualUnaryConversions - missing type");

  // Half FP is a bit different: it's a storage-only type, meaning that any
  // "use" of it should be promoted to float.
  if (Ty->isHalfType())
    return ImpCastExprToType(Res.take(), Context.FloatTy, CK_FloatingCast);

  // Try to perform integral promotions if the object has a theoretically
  // promotable type.
  if (Ty->isIntegralOrUnscopedEnumerationType()) {
    // C99 6.3.1.1p2:
    //
    //   The following may be used in an expression wherever an int or
    //   unsigned int may be used:
    //     - an object or expression with an integer type whose integer
    //       conversion rank is less than or equal to the rank of int
    //       and unsigned int.
    //     - A bit-field of type _Bool, int, signed int, or unsigned int.
    //
    //   If an int can represent all values of the original type, the
    //   value is converted to an int; otherwise, it is converted to an
    //   unsigned int. These are called the integer promotions. All
    //   other types are unchanged by the integer promotions.

    QualType PTy = Context.isPromotableBitField(E);
    if (!PTy.isNull()) {
      E = ImpCastExprToType(E, PTy, CK_IntegralCast).take();
      return Owned(E);
    }
    if (Ty->isPromotableIntegerType()) {
      QualType PT = Context.getPromotedIntegerType(Ty);
      E = ImpCastExprToType(E, PT, CK_IntegralCast).take();
      return Owned(E);
    }
  }
  return Owned(E);
}

/// DefaultArgumentPromotion (C99 6.5.2.2p6). Used for function calls that
/// do not have a prototype. Arguments that have type float are promoted to
/// double. All other argument types are converted by UsualUnaryConversions().
ExprResult Sema::DefaultArgumentPromotion(Expr *E) {
  QualType Ty = E->getType();
  assert(!Ty.isNull() && "DefaultArgumentPromotion - missing type");

  ExprResult Res = UsualUnaryConversions(E);
  if (Res.isInvalid())
    return Owned(E);
  E = Res.take();

  // If this is a 'float' (CVR qualified or typedef) promote to double.
  if (Ty->isSpecificBuiltinType(BuiltinType::Float))
    E = ImpCastExprToType(E, Context.DoubleTy, CK_FloatingCast).take();

  // C++ performs lvalue-to-rvalue conversion as a default argument
  // promotion, even on class types, but note:
  //   C++11 [conv.lval]p2:
  //     When an lvalue-to-rvalue conversion occurs in an unevaluated
  //     operand or a subexpression thereof the value contained in the
  //     referenced object is not accessed. Otherwise, if the glvalue
  //     has a class type, the conversion copy-initializes a temporary
  //     of type T from the glvalue and the result of the conversion
  //     is a prvalue for the temporary.
  // FIXME: add some way to gate this entire thing for correctness in
  // potentially potentially evaluated contexts.
  if (getLangOptions().CPlusPlus && E->isGLValue() && 
      ExprEvalContexts.back().Context != Unevaluated) {
    ExprResult Temp = PerformCopyInitialization(
                       InitializedEntity::InitializeTemporary(E->getType()),
                                                E->getExprLoc(),
                                                Owned(E));
    if (Temp.isInvalid())
      return ExprError();
    E = Temp.get();
  }

  return Owned(E);
}

/// DefaultVariadicArgumentPromotion - Like DefaultArgumentPromotion, but
/// will warn if the resulting type is not a POD type, and rejects ObjC
/// interfaces passed by value.
ExprResult Sema::DefaultVariadicArgumentPromotion(Expr *E, VariadicCallType CT,
                                                  FunctionDecl *FDecl) {
  ExprResult ExprRes = CheckPlaceholderExpr(E);
  if (ExprRes.isInvalid())
    return ExprError();
  
  ExprRes = DefaultArgumentPromotion(E);
  if (ExprRes.isInvalid())
    return ExprError();
  E = ExprRes.take();

  // Don't allow one to pass an Objective-C interface to a vararg.
  if (E->getType()->isObjCObjectType() &&
    DiagRuntimeBehavior(E->getLocStart(), 0,
                        PDiag(diag::err_cannot_pass_objc_interface_to_vararg)
                          << E->getType() << CT))
    return ExprError();

  // Complain about passing non-POD types through varargs. However, don't
  // perform this check for incomplete types, which we can get here when we're
  // in an unevaluated context.
  if (!E->getType()->isIncompleteType() && !E->getType().isPODType(Context)) {
    // C++0x [expr.call]p7:
    //   Passing a potentially-evaluated argument of class type (Clause 9) 
    //   having a non-trivial copy constructor, a non-trivial move constructor,
    //   or a non-trivial destructor, with no corresponding parameter, 
    //   is conditionally-supported with implementation-defined semantics.
    bool TrivialEnough = false;
    if (getLangOptions().CPlusPlus0x && !E->getType()->isDependentType())  {
      if (CXXRecordDecl *Record = E->getType()->getAsCXXRecordDecl()) {
        if (Record->hasTrivialCopyConstructor() &&
            Record->hasTrivialMoveConstructor() &&
            Record->hasTrivialDestructor())
          TrivialEnough = true;
      }
    }

    if (!TrivialEnough &&
        getLangOptions().ObjCAutoRefCount &&
        E->getType()->isObjCLifetimeType())
      TrivialEnough = true;
      
    if (TrivialEnough) {
      // Nothing to diagnose. This is okay.
    } else if (DiagRuntimeBehavior(E->getLocStart(), 0,
                          PDiag(diag::warn_cannot_pass_non_pod_arg_to_vararg)
                            << getLangOptions().CPlusPlus0x << E->getType() 
                            << CT)) {
      // Turn this into a trap.
      CXXScopeSpec SS;
      UnqualifiedId Name;
      Name.setIdentifier(PP.getIdentifierInfo("__builtin_trap"),
                         E->getLocStart());
      ExprResult TrapFn = ActOnIdExpression(TUScope, SS, Name, true, false);
      if (TrapFn.isInvalid())
        return ExprError();

      ExprResult Call = ActOnCallExpr(TUScope, TrapFn.get(), E->getLocStart(),
                                      MultiExprArg(), E->getLocEnd());
      if (Call.isInvalid())
        return ExprError();
      
      ExprResult Comma = ActOnBinOp(TUScope, E->getLocStart(), tok::comma,
                                    Call.get(), E);
      if (Comma.isInvalid())
        return ExprError();      
      E = Comma.get();
    }
  }
  
  return Owned(E);
}

/// \brief Converts an integer to complex float type.  Helper function of
/// UsualArithmeticConversions()
///
/// \return false if the integer expression is an integer type and is
/// successfully converted to the complex type.
static bool handleIntegerToComplexFloatConversion(Sema &S, ExprResult &IntExpr,
                                                  ExprResult &ComplexExpr,
                                                  QualType IntTy,
                                                  QualType ComplexTy,
                                                  bool SkipCast) {
  if (IntTy->isComplexType() || IntTy->isRealFloatingType()) return true;
  if (SkipCast) return false;
  if (IntTy->isIntegerType()) {
    QualType fpTy = cast<ComplexType>(ComplexTy)->getElementType();
    IntExpr = S.ImpCastExprToType(IntExpr.take(), fpTy, CK_IntegralToFloating);
    IntExpr = S.ImpCastExprToType(IntExpr.take(), ComplexTy,
                                  CK_FloatingRealToComplex);
  } else {
    assert(IntTy->isComplexIntegerType());
    IntExpr = S.ImpCastExprToType(IntExpr.take(), ComplexTy,
                                  CK_IntegralComplexToFloatingComplex);
  }
  return false;
}

/// \brief Takes two complex float types and converts them to the same type.
/// Helper function of UsualArithmeticConversions()
static QualType
handleComplexFloatToComplexFloatConverstion(Sema &S, ExprResult &LHS,
                                            ExprResult &RHS, QualType LHSType,
                                            QualType RHSType,
                                            bool IsCompAssign) {
  int order = S.Context.getFloatingTypeOrder(LHSType, RHSType);

  if (order < 0) {
    // _Complex float -> _Complex double
    if (!IsCompAssign)
      LHS = S.ImpCastExprToType(LHS.take(), RHSType, CK_FloatingComplexCast);
    return RHSType;
  }
  if (order > 0)
    // _Complex float -> _Complex double
    RHS = S.ImpCastExprToType(RHS.take(), LHSType, CK_FloatingComplexCast);
  return LHSType;
}

/// \brief Converts otherExpr to complex float and promotes complexExpr if
/// necessary.  Helper function of UsualArithmeticConversions()
static QualType handleOtherComplexFloatConversion(Sema &S,
                                                  ExprResult &ComplexExpr,
                                                  ExprResult &OtherExpr,
                                                  QualType ComplexTy,
                                                  QualType OtherTy,
                                                  bool ConvertComplexExpr,
                                                  bool ConvertOtherExpr) {
  int order = S.Context.getFloatingTypeOrder(ComplexTy, OtherTy);

  // If just the complexExpr is complex, the otherExpr needs to be converted,
  // and the complexExpr might need to be promoted.
  if (order > 0) { // complexExpr is wider
    // float -> _Complex double
    if (ConvertOtherExpr) {
      QualType fp = cast<ComplexType>(ComplexTy)->getElementType();
      OtherExpr = S.ImpCastExprToType(OtherExpr.take(), fp, CK_FloatingCast);
      OtherExpr = S.ImpCastExprToType(OtherExpr.take(), ComplexTy,
                                      CK_FloatingRealToComplex);
    }
    return ComplexTy;
  }

  // otherTy is at least as wide.  Find its corresponding complex type.
  QualType result = (order == 0 ? ComplexTy :
                                  S.Context.getComplexType(OtherTy));

  // double -> _Complex double
  if (ConvertOtherExpr)
    OtherExpr = S.ImpCastExprToType(OtherExpr.take(), result,
                                    CK_FloatingRealToComplex);

  // _Complex float -> _Complex double
  if (ConvertComplexExpr && order < 0)
    ComplexExpr = S.ImpCastExprToType(ComplexExpr.take(), result,
                                      CK_FloatingComplexCast);

  return result;
}

/// \brief Handle arithmetic conversion with complex types.  Helper function of
/// UsualArithmeticConversions()
static QualType handleComplexFloatConversion(Sema &S, ExprResult &LHS,
                                             ExprResult &RHS, QualType LHSType,
                                             QualType RHSType,
                                             bool IsCompAssign) {
  // if we have an integer operand, the result is the complex type.
  if (!handleIntegerToComplexFloatConversion(S, RHS, LHS, RHSType, LHSType,
                                             /*skipCast*/false))
    return LHSType;
  if (!handleIntegerToComplexFloatConversion(S, LHS, RHS, LHSType, RHSType,
                                             /*skipCast*/IsCompAssign))
    return RHSType;

  // This handles complex/complex, complex/float, or float/complex.
  // When both operands are complex, the shorter operand is converted to the
  // type of the longer, and that is the type of the result. This corresponds
  // to what is done when combining two real floating-point operands.
  // The fun begins when size promotion occur across type domains.
  // From H&S 6.3.4: When one operand is complex and the other is a real
  // floating-point type, the less precise type is converted, within it's
  // real or complex domain, to the precision of the other type. For example,
  // when combining a "long double" with a "double _Complex", the
  // "double _Complex" is promoted to "long double _Complex".

  bool LHSComplexFloat = LHSType->isComplexType();
  bool RHSComplexFloat = RHSType->isComplexType();

  // If both are complex, just cast to the more precise type.
  if (LHSComplexFloat && RHSComplexFloat)
    return handleComplexFloatToComplexFloatConverstion(S, LHS, RHS,
                                                       LHSType, RHSType,
                                                       IsCompAssign);

  // If only one operand is complex, promote it if necessary and convert the
  // other operand to complex.
  if (LHSComplexFloat)
    return handleOtherComplexFloatConversion(
        S, LHS, RHS, LHSType, RHSType, /*convertComplexExpr*/!IsCompAssign,
        /*convertOtherExpr*/ true);

  assert(RHSComplexFloat);
  return handleOtherComplexFloatConversion(
      S, RHS, LHS, RHSType, LHSType, /*convertComplexExpr*/true,
      /*convertOtherExpr*/ !IsCompAssign);
}

/// \brief Hande arithmetic conversion from integer to float.  Helper function
/// of UsualArithmeticConversions()
static QualType handleIntToFloatConversion(Sema &S, ExprResult &FloatExpr,
                                           ExprResult &IntExpr,
                                           QualType FloatTy, QualType IntTy,
                                           bool ConvertFloat, bool ConvertInt) {
  if (IntTy->isIntegerType()) {
    if (ConvertInt)
      // Convert intExpr to the lhs floating point type.
      IntExpr = S.ImpCastExprToType(IntExpr.take(), FloatTy,
                                    CK_IntegralToFloating);
    return FloatTy;
  }
     
  // Convert both sides to the appropriate complex float.
  assert(IntTy->isComplexIntegerType());
  QualType result = S.Context.getComplexType(FloatTy);

  // _Complex int -> _Complex float
  if (ConvertInt)
    IntExpr = S.ImpCastExprToType(IntExpr.take(), result,
                                  CK_IntegralComplexToFloatingComplex);

  // float -> _Complex float
  if (ConvertFloat)
    FloatExpr = S.ImpCastExprToType(FloatExpr.take(), result,
                                    CK_FloatingRealToComplex);

  return result;
}

/// \brief Handle arithmethic conversion with floating point types.  Helper
/// function of UsualArithmeticConversions()
static QualType handleFloatConversion(Sema &S, ExprResult &LHS,
                                      ExprResult &RHS, QualType LHSType,
                                      QualType RHSType, bool IsCompAssign) {
  bool LHSFloat = LHSType->isRealFloatingType();
  bool RHSFloat = RHSType->isRealFloatingType();

  // If we have two real floating types, convert the smaller operand
  // to the bigger result.
  if (LHSFloat && RHSFloat) {
    int order = S.Context.getFloatingTypeOrder(LHSType, RHSType);
    if (order > 0) {
      RHS = S.ImpCastExprToType(RHS.take(), LHSType, CK_FloatingCast);
      return LHSType;
    }

    assert(order < 0 && "illegal float comparison");
    if (!IsCompAssign)
      LHS = S.ImpCastExprToType(LHS.take(), RHSType, CK_FloatingCast);
    return RHSType;
  }

  if (LHSFloat)
    return handleIntToFloatConversion(S, LHS, RHS, LHSType, RHSType,
                                      /*convertFloat=*/!IsCompAssign,
                                      /*convertInt=*/ true);
  assert(RHSFloat);
  return handleIntToFloatConversion(S, RHS, LHS, RHSType, LHSType,
                                    /*convertInt=*/ true,
                                    /*convertFloat=*/!IsCompAssign);
}

/// \brief Handle conversions with GCC complex int extension.  Helper function
/// of UsualArithmeticConversions()
// FIXME: if the operands are (int, _Complex long), we currently
// don't promote the complex.  Also, signedness?
static QualType handleComplexIntConversion(Sema &S, ExprResult &LHS,
                                           ExprResult &RHS, QualType LHSType,
                                           QualType RHSType,
                                           bool IsCompAssign) {
  const ComplexType *LHSComplexInt = LHSType->getAsComplexIntegerType();
  const ComplexType *RHSComplexInt = RHSType->getAsComplexIntegerType();

  if (LHSComplexInt && RHSComplexInt) {
    int order = S.Context.getIntegerTypeOrder(LHSComplexInt->getElementType(),
                                              RHSComplexInt->getElementType());
    assert(order && "inequal types with equal element ordering");
    if (order > 0) {
      // _Complex int -> _Complex long
      RHS = S.ImpCastExprToType(RHS.take(), LHSType, CK_IntegralComplexCast);
      return LHSType;
    }

    if (!IsCompAssign)
      LHS = S.ImpCastExprToType(LHS.take(), RHSType, CK_IntegralComplexCast);
    return RHSType;
  }

  if (LHSComplexInt) {
    // int -> _Complex int
    RHS = S.ImpCastExprToType(RHS.take(), LHSType, CK_IntegralRealToComplex);
    return LHSType;
  }

  assert(RHSComplexInt);
  // int -> _Complex int
  if (!IsCompAssign)
    LHS = S.ImpCastExprToType(LHS.take(), RHSType, CK_IntegralRealToComplex);
  return RHSType;
}

/// \brief Handle integer arithmetic conversions.  Helper function of
/// UsualArithmeticConversions()
static QualType handleIntegerConversion(Sema &S, ExprResult &LHS,
                                        ExprResult &RHS, QualType LHSType,
                                        QualType RHSType, bool IsCompAssign) {
  // The rules for this case are in C99 6.3.1.8
  int order = S.Context.getIntegerTypeOrder(LHSType, RHSType);
  bool LHSSigned = LHSType->hasSignedIntegerRepresentation();
  bool RHSSigned = RHSType->hasSignedIntegerRepresentation();
  if (LHSSigned == RHSSigned) {
    // Same signedness; use the higher-ranked type
    if (order >= 0) {
      RHS = S.ImpCastExprToType(RHS.take(), LHSType, CK_IntegralCast);
      return LHSType;
    } else if (!IsCompAssign)
      LHS = S.ImpCastExprToType(LHS.take(), RHSType, CK_IntegralCast);
    return RHSType;
  } else if (order != (LHSSigned ? 1 : -1)) {
    // The unsigned type has greater than or equal rank to the
    // signed type, so use the unsigned type
    if (RHSSigned) {
      RHS = S.ImpCastExprToType(RHS.take(), LHSType, CK_IntegralCast);
      return LHSType;
    } else if (!IsCompAssign)
      LHS = S.ImpCastExprToType(LHS.take(), RHSType, CK_IntegralCast);
    return RHSType;
  } else if (S.Context.getIntWidth(LHSType) != S.Context.getIntWidth(RHSType)) {
    // The two types are different widths; if we are here, that
    // means the signed type is larger than the unsigned type, so
    // use the signed type.
    if (LHSSigned) {
      RHS = S.ImpCastExprToType(RHS.take(), LHSType, CK_IntegralCast);
      return LHSType;
    } else if (!IsCompAssign)
      LHS = S.ImpCastExprToType(LHS.take(), RHSType, CK_IntegralCast);
    return RHSType;
  } else {
    // The signed type is higher-ranked than the unsigned type,
    // but isn't actually any bigger (like unsigned int and long
    // on most 32-bit systems).  Use the unsigned type corresponding
    // to the signed type.
    QualType result =
      S.Context.getCorrespondingUnsignedType(LHSSigned ? LHSType : RHSType);
    RHS = S.ImpCastExprToType(RHS.take(), result, CK_IntegralCast);
    if (!IsCompAssign)
      LHS = S.ImpCastExprToType(LHS.take(), result, CK_IntegralCast);
    return result;
  }
}

/// UsualArithmeticConversions - Performs various conversions that are common to
/// binary operators (C99 6.3.1.8). If both operands aren't arithmetic, this
/// routine returns the first non-arithmetic type found. The client is
/// responsible for emitting appropriate error diagnostics.
/// FIXME: verify the conversion rules for "complex int" are consistent with
/// GCC.
QualType Sema::UsualArithmeticConversions(ExprResult &LHS, ExprResult &RHS,
                                          bool IsCompAssign) {
  if (!IsCompAssign) {
    LHS = UsualUnaryConversions(LHS.take());
    if (LHS.isInvalid())
      return QualType();
  }

  RHS = UsualUnaryConversions(RHS.take());
  if (RHS.isInvalid())
    return QualType();

  // For conversion purposes, we ignore any qualifiers.
  // For example, "const float" and "float" are equivalent.
  QualType LHSType =
    Context.getCanonicalType(LHS.get()->getType()).getUnqualifiedType();
  QualType RHSType =
    Context.getCanonicalType(RHS.get()->getType()).getUnqualifiedType();

  // If both types are identical, no conversion is needed.
  if (LHSType == RHSType)
    return LHSType;

  // If either side is a non-arithmetic type (e.g. a pointer), we are done.
  // The caller can deal with this (e.g. pointer + int).
  if (!LHSType->isArithmeticType() || !RHSType->isArithmeticType())
    return LHSType;

  // Apply unary and bitfield promotions to the LHS's type.
  QualType LHSUnpromotedType = LHSType;
  if (LHSType->isPromotableIntegerType())
    LHSType = Context.getPromotedIntegerType(LHSType);
  QualType LHSBitfieldPromoteTy = Context.isPromotableBitField(LHS.get());
  if (!LHSBitfieldPromoteTy.isNull())
    LHSType = LHSBitfieldPromoteTy;
  if (LHSType != LHSUnpromotedType && !IsCompAssign)
    LHS = ImpCastExprToType(LHS.take(), LHSType, CK_IntegralCast);

  // If both types are identical, no conversion is needed.
  if (LHSType == RHSType)
    return LHSType;

  // At this point, we have two different arithmetic types.

  // Handle complex types first (C99 6.3.1.8p1).
  if (LHSType->isComplexType() || RHSType->isComplexType())
    return handleComplexFloatConversion(*this, LHS, RHS, LHSType, RHSType,
                                        IsCompAssign);

  // Now handle "real" floating types (i.e. float, double, long double).
  if (LHSType->isRealFloatingType() || RHSType->isRealFloatingType())
    return handleFloatConversion(*this, LHS, RHS, LHSType, RHSType,
                                 IsCompAssign);

  // Handle GCC complex int extension.
  if (LHSType->isComplexIntegerType() || RHSType->isComplexIntegerType())
    return handleComplexIntConversion(*this, LHS, RHS, LHSType, RHSType,
                                      IsCompAssign);

  // Finally, we have two differing integer types.
  return handleIntegerConversion(*this, LHS, RHS, LHSType, RHSType,
                                 IsCompAssign);
}

//===----------------------------------------------------------------------===//
//  Semantic Analysis for various Expression Types
//===----------------------------------------------------------------------===//


ExprResult
Sema::ActOnGenericSelectionExpr(SourceLocation KeyLoc,
                                SourceLocation DefaultLoc,
                                SourceLocation RParenLoc,
                                Expr *ControllingExpr,
                                MultiTypeArg ArgTypes,
                                MultiExprArg ArgExprs) {
  unsigned NumAssocs = ArgTypes.size();
  assert(NumAssocs == ArgExprs.size());

  ParsedType *ParsedTypes = ArgTypes.release();
  Expr **Exprs = ArgExprs.release();

  TypeSourceInfo **Types = new TypeSourceInfo*[NumAssocs];
  for (unsigned i = 0; i < NumAssocs; ++i) {
    if (ParsedTypes[i])
      (void) GetTypeFromParser(ParsedTypes[i], &Types[i]);
    else
      Types[i] = 0;
  }

  ExprResult ER = CreateGenericSelectionExpr(KeyLoc, DefaultLoc, RParenLoc,
                                             ControllingExpr, Types, Exprs,
                                             NumAssocs);
  delete [] Types;
  return ER;
}

ExprResult
Sema::CreateGenericSelectionExpr(SourceLocation KeyLoc,
                                 SourceLocation DefaultLoc,
                                 SourceLocation RParenLoc,
                                 Expr *ControllingExpr,
                                 TypeSourceInfo **Types,
                                 Expr **Exprs,
                                 unsigned NumAssocs) {
  bool TypeErrorFound = false,
       IsResultDependent = ControllingExpr->isTypeDependent(),
       ContainsUnexpandedParameterPack
         = ControllingExpr->containsUnexpandedParameterPack();

  for (unsigned i = 0; i < NumAssocs; ++i) {
    if (Exprs[i]->containsUnexpandedParameterPack())
      ContainsUnexpandedParameterPack = true;

    if (Types[i]) {
      if (Types[i]->getType()->containsUnexpandedParameterPack())
        ContainsUnexpandedParameterPack = true;

      if (Types[i]->getType()->isDependentType()) {
        IsResultDependent = true;
      } else {
        // C1X 6.5.1.1p2 "The type name in a generic association shall specify a
        // complete object type other than a variably modified type."
        unsigned D = 0;
        if (Types[i]->getType()->isIncompleteType())
          D = diag::err_assoc_type_incomplete;
        else if (!Types[i]->getType()->isObjectType())
          D = diag::err_assoc_type_nonobject;
        else if (Types[i]->getType()->isVariablyModifiedType())
          D = diag::err_assoc_type_variably_modified;

        if (D != 0) {
          Diag(Types[i]->getTypeLoc().getBeginLoc(), D)
            << Types[i]->getTypeLoc().getSourceRange()
            << Types[i]->getType();
          TypeErrorFound = true;
        }

        // C1X 6.5.1.1p2 "No two generic associations in the same generic
        // selection shall specify compatible types."
        for (unsigned j = i+1; j < NumAssocs; ++j)
          if (Types[j] && !Types[j]->getType()->isDependentType() &&
              Context.typesAreCompatible(Types[i]->getType(),
                                         Types[j]->getType())) {
            Diag(Types[j]->getTypeLoc().getBeginLoc(),
                 diag::err_assoc_compatible_types)
              << Types[j]->getTypeLoc().getSourceRange()
              << Types[j]->getType()
              << Types[i]->getType();
            Diag(Types[i]->getTypeLoc().getBeginLoc(),
                 diag::note_compat_assoc)
              << Types[i]->getTypeLoc().getSourceRange()
              << Types[i]->getType();
            TypeErrorFound = true;
          }
      }
    }
  }
  if (TypeErrorFound)
    return ExprError();

  // If we determined that the generic selection is result-dependent, don't
  // try to compute the result expression.
  if (IsResultDependent)
    return Owned(new (Context) GenericSelectionExpr(
                   Context, KeyLoc, ControllingExpr,
                   Types, Exprs, NumAssocs, DefaultLoc,
                   RParenLoc, ContainsUnexpandedParameterPack));

  SmallVector<unsigned, 1> CompatIndices;
  unsigned DefaultIndex = -1U;
  for (unsigned i = 0; i < NumAssocs; ++i) {
    if (!Types[i])
      DefaultIndex = i;
    else if (Context.typesAreCompatible(ControllingExpr->getType(),
                                        Types[i]->getType()))
      CompatIndices.push_back(i);
  }

  // C1X 6.5.1.1p2 "The controlling expression of a generic selection shall have
  // type compatible with at most one of the types named in its generic
  // association list."
  if (CompatIndices.size() > 1) {
    // We strip parens here because the controlling expression is typically
    // parenthesized in macro definitions.
    ControllingExpr = ControllingExpr->IgnoreParens();
    Diag(ControllingExpr->getLocStart(), diag::err_generic_sel_multi_match)
      << ControllingExpr->getSourceRange() << ControllingExpr->getType()
      << (unsigned) CompatIndices.size();
    for (SmallVector<unsigned, 1>::iterator I = CompatIndices.begin(),
         E = CompatIndices.end(); I != E; ++I) {
      Diag(Types[*I]->getTypeLoc().getBeginLoc(),
           diag::note_compat_assoc)
        << Types[*I]->getTypeLoc().getSourceRange()
        << Types[*I]->getType();
    }
    return ExprError();
  }

  // C1X 6.5.1.1p2 "If a generic selection has no default generic association,
  // its controlling expression shall have type compatible with exactly one of
  // the types named in its generic association list."
  if (DefaultIndex == -1U && CompatIndices.size() == 0) {
    // We strip parens here because the controlling expression is typically
    // parenthesized in macro definitions.
    ControllingExpr = ControllingExpr->IgnoreParens();
    Diag(ControllingExpr->getLocStart(), diag::err_generic_sel_no_match)
      << ControllingExpr->getSourceRange() << ControllingExpr->getType();
    return ExprError();
  }

  // C1X 6.5.1.1p3 "If a generic selection has a generic association with a
  // type name that is compatible with the type of the controlling expression,
  // then the result expression of the generic selection is the expression
  // in that generic association. Otherwise, the result expression of the
  // generic selection is the expression in the default generic association."
  unsigned ResultIndex =
    CompatIndices.size() ? CompatIndices[0] : DefaultIndex;

  return Owned(new (Context) GenericSelectionExpr(
                 Context, KeyLoc, ControllingExpr,
                 Types, Exprs, NumAssocs, DefaultLoc,
                 RParenLoc, ContainsUnexpandedParameterPack,
                 ResultIndex));
}

/// ActOnStringLiteral - The specified tokens were lexed as pasted string
/// fragments (e.g. "foo" "bar" L"baz").  The result string has to handle string
/// concatenation ([C99 5.1.1.2, translation phase #6]), so it may come from
/// multiple tokens.  However, the common case is that StringToks points to one
/// string.
///
ExprResult
Sema::ActOnStringLiteral(const Token *StringToks, unsigned NumStringToks) {
  assert(NumStringToks && "Must have at least one string!");

  StringLiteralParser Literal(StringToks, NumStringToks, PP);
  if (Literal.hadError)
    return ExprError();

  SmallVector<SourceLocation, 4> StringTokLocs;
  for (unsigned i = 0; i != NumStringToks; ++i)
    StringTokLocs.push_back(StringToks[i].getLocation());

  QualType StrTy = Context.CharTy;
  if (Literal.isWide())
    StrTy = Context.getWCharType();
  else if (Literal.isUTF16())
    StrTy = Context.Char16Ty;
  else if (Literal.isUTF32())
    StrTy = Context.Char32Ty;
  else if (Literal.Pascal)
    StrTy = Context.UnsignedCharTy;

  StringLiteral::StringKind Kind = StringLiteral::Ascii;
  if (Literal.isWide())
    Kind = StringLiteral::Wide;
  else if (Literal.isUTF8())
    Kind = StringLiteral::UTF8;
  else if (Literal.isUTF16())
    Kind = StringLiteral::UTF16;
  else if (Literal.isUTF32())
    Kind = StringLiteral::UTF32;

  // A C++ string literal has a const-qualified element type (C++ 2.13.4p1).
  if (getLangOptions().CPlusPlus || getLangOptions().ConstStrings)
    StrTy.addConst();

  // Get an array type for the string, according to C99 6.4.5.  This includes
  // the nul terminator character as well as the string length for pascal
  // strings.
  StrTy = Context.getConstantArrayType(StrTy,
                                 llvm::APInt(32, Literal.GetNumStringChars()+1),
                                       ArrayType::Normal, 0);

  // Pass &StringTokLocs[0], StringTokLocs.size() to factory!
  return Owned(StringLiteral::Create(Context, Literal.GetString(),
                                     Kind, Literal.Pascal, StrTy,
                                     &StringTokLocs[0],
                                     StringTokLocs.size()));
}

enum CaptureResult {
  /// No capture is required.
  CR_NoCapture,

  /// A capture is required.
  CR_Capture,

  /// A by-ref capture is required.
  CR_CaptureByRef,

  /// An error occurred when trying to capture the given variable.
  CR_Error
};

/// Diagnose an uncapturable value reference.
///
/// \param var - the variable referenced
/// \param DC - the context which we couldn't capture through
static CaptureResult
diagnoseUncapturableValueReference(Sema &S, SourceLocation loc,
                                   VarDecl *var, DeclContext *DC) {
  switch (S.ExprEvalContexts.back().Context) {
  case Sema::Unevaluated:
    // The argument will never be evaluated, so don't complain.
    return CR_NoCapture;

  case Sema::PotentiallyEvaluated:
  case Sema::PotentiallyEvaluatedIfUsed:
    break;

  case Sema::PotentiallyPotentiallyEvaluated:
    // FIXME: delay these!
    break;
  }

  // Don't diagnose about capture if we're not actually in code right
  // now; in general, there are more appropriate places that will
  // diagnose this.
  if (!S.CurContext->isFunctionOrMethod()) return CR_NoCapture;

  // Certain madnesses can happen with parameter declarations, which
  // we want to ignore.
  if (isa<ParmVarDecl>(var)) {
    // - If the parameter still belongs to the translation unit, then
    //   we're actually just using one parameter in the declaration of
    //   the next.  This is useful in e.g. VLAs.
    if (isa<TranslationUnitDecl>(var->getDeclContext()))
      return CR_NoCapture;

    // - This particular madness can happen in ill-formed default
    //   arguments; claim it's okay and let downstream code handle it.
    if (S.CurContext == var->getDeclContext()->getParent())
      return CR_NoCapture;
  }

  DeclarationName functionName;
  if (FunctionDecl *fn = dyn_cast<FunctionDecl>(var->getDeclContext()))
    functionName = fn->getDeclName();
  // FIXME: variable from enclosing block that we couldn't capture from!

  S.Diag(loc, diag::err_reference_to_local_var_in_enclosing_function)
    << var->getIdentifier() << functionName;
  S.Diag(var->getLocation(), diag::note_local_variable_declared_here)
    << var->getIdentifier();

  return CR_Error;
}

/// There is a well-formed capture at a particular scope level;
/// propagate it through all the nested blocks.
static CaptureResult propagateCapture(Sema &S, unsigned ValidScopeIndex,
                                      const BlockDecl::Capture &Capture) {
  VarDecl *var = Capture.getVariable();

  // Update all the inner blocks with the capture information.
  for (unsigned i = ValidScopeIndex + 1, e = S.FunctionScopes.size();
         i != e; ++i) {
    BlockScopeInfo *innerBlock = cast<BlockScopeInfo>(S.FunctionScopes[i]);
    innerBlock->Captures.push_back(
      BlockDecl::Capture(Capture.getVariable(), Capture.isByRef(),
                         /*nested*/ true, Capture.getCopyExpr()));
    innerBlock->CaptureMap[var] = innerBlock->Captures.size(); // +1
  }

  return Capture.isByRef() ? CR_CaptureByRef : CR_Capture;
}

/// shouldCaptureValueReference - Determine if a reference to the
/// given value in the current context requires a variable capture.
///
/// This also keeps the captures set in the BlockScopeInfo records
/// up-to-date.
static CaptureResult shouldCaptureValueReference(Sema &S, SourceLocation loc,
                                                 ValueDecl *Value) {
  // Only variables ever require capture.
  VarDecl *var = dyn_cast<VarDecl>(Value);
  if (!var) return CR_NoCapture;

  // Fast path: variables from the current context never require capture.
  DeclContext *DC = S.CurContext;
  if (var->getDeclContext() == DC) return CR_NoCapture;

  // Only variables with local storage require capture.
  // FIXME: What about 'const' variables in C++?
  if (!var->hasLocalStorage()) return CR_NoCapture;

  // Otherwise, we need to capture.

  unsigned functionScopesIndex = S.FunctionScopes.size() - 1;
  do {
    // Only blocks (and eventually C++0x closures) can capture; other
    // scopes don't work.
    if (!isa<BlockDecl>(DC))
      return diagnoseUncapturableValueReference(S, loc, var, DC);

    BlockScopeInfo *blockScope =
      cast<BlockScopeInfo>(S.FunctionScopes[functionScopesIndex]);
    assert(blockScope->TheDecl == static_cast<BlockDecl*>(DC));

    // Check whether we've already captured it in this block.  If so,
    // we're done.
    if (unsigned indexPlus1 = blockScope->CaptureMap[var])
      return propagateCapture(S, functionScopesIndex,
                              blockScope->Captures[indexPlus1 - 1]);

    functionScopesIndex--;
    DC = cast<BlockDecl>(DC)->getDeclContext();
  } while (var->getDeclContext() != DC);

  // Okay, we descended all the way to the block that defines the variable.
  // Actually try to capture it.
  QualType type = var->getType();

  // Prohibit variably-modified types.
  if (type->isVariablyModifiedType()) {
    S.Diag(loc, diag::err_ref_vm_type);
    S.Diag(var->getLocation(), diag::note_declared_at);
    return CR_Error;
  }

  // Prohibit arrays, even in __block variables, but not references to
  // them.
  if (type->isArrayType()) {
    S.Diag(loc, diag::err_ref_array_type);
    S.Diag(var->getLocation(), diag::note_declared_at);
    return CR_Error;
  }

  S.MarkDeclarationReferenced(loc, var);

  // The BlocksAttr indicates the variable is bound by-reference.
  bool byRef = var->hasAttr<BlocksAttr>();

  // Build a copy expression.
  Expr *copyExpr = 0;
  const RecordType *rtype;
  if (!byRef && S.getLangOptions().CPlusPlus && !type->isDependentType() &&
      (rtype = type->getAs<RecordType>())) {

    // The capture logic needs the destructor, so make sure we mark it.
    // Usually this is unnecessary because most local variables have
    // their destructors marked at declaration time, but parameters are
    // an exception because it's technically only the call site that
    // actually requires the destructor.
    if (isa<ParmVarDecl>(var))
      S.FinalizeVarWithDestructor(var, rtype);

    // According to the blocks spec, the capture of a variable from
    // the stack requires a const copy constructor.  This is not true
    // of the copy/move done to move a __block variable to the heap.
    type.addConst();

    Expr *declRef = new (S.Context) DeclRefExpr(var, type, VK_LValue, loc);
    ExprResult result =
      S.PerformCopyInitialization(
                      InitializedEntity::InitializeBlock(var->getLocation(),
                                                         type, false),
                                  loc, S.Owned(declRef));

    // Build a full-expression copy expression if initialization
    // succeeded and used a non-trivial constructor.  Recover from
    // errors by pretending that the copy isn't necessary.
    if (!result.isInvalid() &&
        !cast<CXXConstructExpr>(result.get())->getConstructor()->isTrivial()) {
      result = S.MaybeCreateExprWithCleanups(result);
      copyExpr = result.take();
    }
  }

  // We're currently at the declarer; go back to the closure.
  functionScopesIndex++;
  BlockScopeInfo *blockScope =
    cast<BlockScopeInfo>(S.FunctionScopes[functionScopesIndex]);

  // Build a valid capture in this scope.
  blockScope->Captures.push_back(
                 BlockDecl::Capture(var, byRef, /*nested*/ false, copyExpr));
  blockScope->CaptureMap[var] = blockScope->Captures.size(); // +1

  // Propagate that to inner captures if necessary.
  return propagateCapture(S, functionScopesIndex,
                          blockScope->Captures.back());
}

static ExprResult BuildBlockDeclRefExpr(Sema &S, ValueDecl *VD,
                                        const DeclarationNameInfo &NameInfo,
                                        bool ByRef) {
  assert(isa<VarDecl>(VD) && "capturing non-variable");

  VarDecl *var = cast<VarDecl>(VD);
  assert(var->hasLocalStorage() && "capturing non-local");
  assert(ByRef == var->hasAttr<BlocksAttr>() && "byref set wrong");

  QualType exprType = var->getType().getNonReferenceType();

  BlockDeclRefExpr *BDRE;
  if (!ByRef) {
    // The variable will be bound by copy; make it const within the
    // closure, but record that this was done in the expression.
    bool constAdded = !exprType.isConstQualified();
    exprType.addConst();

    BDRE = new (S.Context) BlockDeclRefExpr(var, exprType, VK_LValue,
                                            NameInfo.getLoc(), false,
                                            constAdded);
  } else {
    BDRE = new (S.Context) BlockDeclRefExpr(var, exprType, VK_LValue,
                                            NameInfo.getLoc(), true);
  }

  return S.Owned(BDRE);
}

ExprResult
Sema::BuildDeclRefExpr(ValueDecl *D, QualType Ty, ExprValueKind VK,
                       SourceLocation Loc,
                       const CXXScopeSpec *SS) {
  DeclarationNameInfo NameInfo(D->getDeclName(), Loc);
  return BuildDeclRefExpr(D, Ty, VK, NameInfo, SS);
}

/// BuildDeclRefExpr - Build an expression that references a
/// declaration that does not require a closure capture.
ExprResult
Sema::BuildDeclRefExpr(ValueDecl *D, QualType Ty, ExprValueKind VK,
                       const DeclarationNameInfo &NameInfo,
                       const CXXScopeSpec *SS) {
  if (getLangOptions().CUDA)
    if (const FunctionDecl *Caller = dyn_cast<FunctionDecl>(CurContext))
      if (const FunctionDecl *Callee = dyn_cast<FunctionDecl>(D)) {
        CUDAFunctionTarget CallerTarget = IdentifyCUDATarget(Caller),
                           CalleeTarget = IdentifyCUDATarget(Callee);
        if (CheckCUDATarget(CallerTarget, CalleeTarget)) {
          Diag(NameInfo.getLoc(), diag::err_ref_bad_target)
            << CalleeTarget << D->getIdentifier() << CallerTarget;
          Diag(D->getLocation(), diag::note_previous_decl)
            << D->getIdentifier();
          return ExprError();
        }
      }

  MarkDeclarationReferenced(NameInfo.getLoc(), D);

  Expr *E = DeclRefExpr::Create(Context,
                                SS? SS->getWithLocInContext(Context) 
                                  : NestedNameSpecifierLoc(),
                                D, NameInfo, Ty, VK);

  // Just in case we're building an illegal pointer-to-member.
  FieldDecl *FD = dyn_cast<FieldDecl>(D);
  if (FD && FD->isBitField())
    E->setObjectKind(OK_BitField);

  return Owned(E);
}

/// Decomposes the given name into a DeclarationNameInfo, its location, and
/// possibly a list of template arguments.
///
/// If this produces template arguments, it is permitted to call
/// DecomposeTemplateName.
///
/// This actually loses a lot of source location information for
/// non-standard name kinds; we should consider preserving that in
/// some way.
void
Sema::DecomposeUnqualifiedId(const UnqualifiedId &Id,
                             TemplateArgumentListInfo &Buffer,
                             DeclarationNameInfo &NameInfo,
                             const TemplateArgumentListInfo *&TemplateArgs) {
  if (Id.getKind() == UnqualifiedId::IK_TemplateId) {
    Buffer.setLAngleLoc(Id.TemplateId->LAngleLoc);
    Buffer.setRAngleLoc(Id.TemplateId->RAngleLoc);

    ASTTemplateArgsPtr TemplateArgsPtr(*this,
                                       Id.TemplateId->getTemplateArgs(),
                                       Id.TemplateId->NumArgs);
    translateTemplateArguments(TemplateArgsPtr, Buffer);
    TemplateArgsPtr.release();

    TemplateName TName = Id.TemplateId->Template.get();
    SourceLocation TNameLoc = Id.TemplateId->TemplateNameLoc;
    NameInfo = Context.getNameForTemplate(TName, TNameLoc);
    TemplateArgs = &Buffer;
  } else {
    NameInfo = GetNameFromUnqualifiedId(Id);
    TemplateArgs = 0;
  }
}

/// Diagnose an empty lookup.
///
/// \return false if new lookup candidates were found
bool Sema::DiagnoseEmptyLookup(Scope *S, CXXScopeSpec &SS, LookupResult &R,
                               CorrectTypoContext CTC,
                               TemplateArgumentListInfo *ExplicitTemplateArgs,
                               Expr **Args, unsigned NumArgs) {
  DeclarationName Name = R.getLookupName();

  unsigned diagnostic = diag::err_undeclared_var_use;
  unsigned diagnostic_suggest = diag::err_undeclared_var_use_suggest;
  if (Name.getNameKind() == DeclarationName::CXXOperatorName ||
      Name.getNameKind() == DeclarationName::CXXLiteralOperatorName ||
      Name.getNameKind() == DeclarationName::CXXConversionFunctionName) {
    diagnostic = diag::err_undeclared_use;
    diagnostic_suggest = diag::err_undeclared_use_suggest;
  }

  // If the original lookup was an unqualified lookup, fake an
  // unqualified lookup.  This is useful when (for example) the
  // original lookup would not have found something because it was a
  // dependent name.
  for (DeclContext *DC = SS.isEmpty() ? CurContext : 0;
       DC; DC = DC->getParent()) {
    if (isa<CXXRecordDecl>(DC)) {
      LookupQualifiedName(R, DC);

      if (!R.empty()) {
        // Don't give errors about ambiguities in this lookup.
        R.suppressDiagnostics();

        CXXMethodDecl *CurMethod = dyn_cast<CXXMethodDecl>(CurContext);
        bool isInstance = CurMethod &&
                          CurMethod->isInstance() &&
                          DC == CurMethod->getParent();

        // Give a code modification hint to insert 'this->'.
        // TODO: fixit for inserting 'Base<T>::' in the other cases.
        // Actually quite difficult!
        if (isInstance) {
          UnresolvedLookupExpr *ULE = cast<UnresolvedLookupExpr>(
              CallsUndergoingInstantiation.back()->getCallee());
          CXXMethodDecl *DepMethod = cast_or_null<CXXMethodDecl>(
              CurMethod->getInstantiatedFromMemberFunction());
          if (DepMethod) {
            if (getLangOptions().MicrosoftExt)
              diagnostic = diag::warn_found_via_dependent_bases_lookup;
            Diag(R.getNameLoc(), diagnostic) << Name
              << FixItHint::CreateInsertion(R.getNameLoc(), "this->");
            QualType DepThisType = DepMethod->getThisType(Context);
            CXXThisExpr *DepThis = new (Context) CXXThisExpr(
                                       R.getNameLoc(), DepThisType, false);
            TemplateArgumentListInfo TList;
            if (ULE->hasExplicitTemplateArgs())
              ULE->copyTemplateArgumentsInto(TList);
            
            CXXScopeSpec SS;
            SS.Adopt(ULE->getQualifierLoc());
            CXXDependentScopeMemberExpr *DepExpr =
                CXXDependentScopeMemberExpr::Create(
                    Context, DepThis, DepThisType, true, SourceLocation(),
                    SS.getWithLocInContext(Context), NULL,
                    R.getLookupNameInfo(),
                    ULE->hasExplicitTemplateArgs() ? &TList : 0);
            CallsUndergoingInstantiation.back()->setCallee(DepExpr);
          } else {
            // FIXME: we should be able to handle this case too. It is correct
            // to add this-> here. This is a workaround for PR7947.
            Diag(R.getNameLoc(), diagnostic) << Name;
          }
        } else {
          Diag(R.getNameLoc(), diagnostic) << Name;
        }

        // Do we really want to note all of these?
        for (LookupResult::iterator I = R.begin(), E = R.end(); I != E; ++I)
          Diag((*I)->getLocation(), diag::note_dependent_var_use);

        // Tell the callee to try to recover.
        return false;
      }

      R.clear();
    }
  }

  // We didn't find anything, so try to correct for a typo.
  TypoCorrection Corrected;
  if (S && (Corrected = CorrectTypo(R.getLookupNameInfo(), R.getLookupKind(),
                                    S, &SS, NULL, false, CTC))) {
    std::string CorrectedStr(Corrected.getAsString(getLangOptions()));
    std::string CorrectedQuotedStr(Corrected.getQuoted(getLangOptions()));
    R.setLookupName(Corrected.getCorrection());

    if (NamedDecl *ND = Corrected.getCorrectionDecl()) {
      if (Corrected.isOverloaded()) {
        OverloadCandidateSet OCS(R.getNameLoc());
        OverloadCandidateSet::iterator Best;
        for (TypoCorrection::decl_iterator CD = Corrected.begin(),
                                        CDEnd = Corrected.end();
             CD != CDEnd; ++CD) {
          if (FunctionTemplateDecl *FTD =
                   dyn_cast<FunctionTemplateDecl>(*CD))
            AddTemplateOverloadCandidate(
                FTD, DeclAccessPair::make(FTD, AS_none), ExplicitTemplateArgs,
                Args, NumArgs, OCS);
          else if (FunctionDecl *FD = dyn_cast<FunctionDecl>(*CD))
            if (!ExplicitTemplateArgs || ExplicitTemplateArgs->size() == 0)
              AddOverloadCandidate(FD, DeclAccessPair::make(FD, AS_none),
                                   Args, NumArgs, OCS);
        }
        switch (OCS.BestViableFunction(*this, R.getNameLoc(), Best)) {
          case OR_Success:
            ND = Best->Function;
            break;
          default:
            break;
        }
      }
      R.addDecl(ND);
      if (isa<ValueDecl>(ND) || isa<FunctionTemplateDecl>(ND)) {
        if (SS.isEmpty())
          Diag(R.getNameLoc(), diagnostic_suggest) << Name << CorrectedQuotedStr
            << FixItHint::CreateReplacement(R.getNameLoc(), CorrectedStr);
        else
          Diag(R.getNameLoc(), diag::err_no_member_suggest)
            << Name << computeDeclContext(SS, false) << CorrectedQuotedStr
            << SS.getRange()
            << FixItHint::CreateReplacement(R.getNameLoc(), CorrectedStr);
        if (ND)
          Diag(ND->getLocation(), diag::note_previous_decl)
            << CorrectedQuotedStr;

        // Tell the callee to try to recover.
        return false;
      }

      if (isa<TypeDecl>(ND) || isa<ObjCInterfaceDecl>(ND)) {
        // FIXME: If we ended up with a typo for a type name or
        // Objective-C class name, we're in trouble because the parser
        // is in the wrong place to recover. Suggest the typo
        // correction, but don't make it a fix-it since we're not going
        // to recover well anyway.
        if (SS.isEmpty())
          Diag(R.getNameLoc(), diagnostic_suggest)
            << Name << CorrectedQuotedStr;
        else
          Diag(R.getNameLoc(), diag::err_no_member_suggest)
            << Name << computeDeclContext(SS, false) << CorrectedQuotedStr
            << SS.getRange();

        // Don't try to recover; it won't work.
        return true;
      }
    } else {
      // FIXME: We found a keyword. Suggest it, but don't provide a fix-it
      // because we aren't able to recover.
      if (SS.isEmpty())
        Diag(R.getNameLoc(), diagnostic_suggest) << Name << CorrectedQuotedStr;
      else
        Diag(R.getNameLoc(), diag::err_no_member_suggest)
        << Name << computeDeclContext(SS, false) << CorrectedQuotedStr
        << SS.getRange();
      return true;
    }
  }
  R.clear();

  // Emit a special diagnostic for failed member lookups.
  // FIXME: computing the declaration context might fail here (?)
  if (!SS.isEmpty()) {
    Diag(R.getNameLoc(), diag::err_no_member)
      << Name << computeDeclContext(SS, false)
      << SS.getRange();
    return true;
  }

  // Give up, we can't recover.
  Diag(R.getNameLoc(), diagnostic) << Name;
  return true;
}

ExprResult Sema::ActOnIdExpression(Scope *S,
                                   CXXScopeSpec &SS,
                                   UnqualifiedId &Id,
                                   bool HasTrailingLParen,
                                   bool IsAddressOfOperand) {
  assert(!(IsAddressOfOperand && HasTrailingLParen) &&
         "cannot be direct & operand and have a trailing lparen");

  if (SS.isInvalid())
    return ExprError();

  TemplateArgumentListInfo TemplateArgsBuffer;

  // Decompose the UnqualifiedId into the following data.
  DeclarationNameInfo NameInfo;
  const TemplateArgumentListInfo *TemplateArgs;
  DecomposeUnqualifiedId(Id, TemplateArgsBuffer, NameInfo, TemplateArgs);

  DeclarationName Name = NameInfo.getName();
  IdentifierInfo *II = Name.getAsIdentifierInfo();
  SourceLocation NameLoc = NameInfo.getLoc();

  // C++ [temp.dep.expr]p3:
  //   An id-expression is type-dependent if it contains:
  //     -- an identifier that was declared with a dependent type,
  //        (note: handled after lookup)
  //     -- a template-id that is dependent,
  //        (note: handled in BuildTemplateIdExpr)
  //     -- a conversion-function-id that specifies a dependent type,
  //     -- a nested-name-specifier that contains a class-name that
  //        names a dependent type.
  // Determine whether this is a member of an unknown specialization;
  // we need to handle these differently.
  bool DependentID = false;
  if (Name.getNameKind() == DeclarationName::CXXConversionFunctionName &&
      Name.getCXXNameType()->isDependentType()) {
    DependentID = true;
  } else if (SS.isSet()) {
    if (DeclContext *DC = computeDeclContext(SS, false)) {
      if (RequireCompleteDeclContext(SS, DC))
        return ExprError();
    } else {
      DependentID = true;
    }
  }

  if (DependentID)
    return ActOnDependentIdExpression(SS, NameInfo, IsAddressOfOperand,
                                      TemplateArgs);

  bool IvarLookupFollowUp = false;
  // Perform the required lookup.
  LookupResult R(*this, NameInfo, 
                 (Id.getKind() == UnqualifiedId::IK_ImplicitSelfParam) 
                  ? LookupObjCImplicitSelfParam : LookupOrdinaryName);
  if (TemplateArgs) {
    // Lookup the template name again to correctly establish the context in
    // which it was found. This is really unfortunate as we already did the
    // lookup to determine that it was a template name in the first place. If
    // this becomes a performance hit, we can work harder to preserve those
    // results until we get here but it's likely not worth it.
    bool MemberOfUnknownSpecialization;
    LookupTemplateName(R, S, SS, QualType(), /*EnteringContext=*/false,
                       MemberOfUnknownSpecialization);
    
    if (MemberOfUnknownSpecialization ||
        (R.getResultKind() == LookupResult::NotFoundInCurrentInstantiation))
      return ActOnDependentIdExpression(SS, NameInfo, IsAddressOfOperand,
                                        TemplateArgs);
  } else {
    IvarLookupFollowUp = (!SS.isSet() && II && getCurMethodDecl());
    LookupParsedName(R, S, &SS, !IvarLookupFollowUp);

    // If the result might be in a dependent base class, this is a dependent 
    // id-expression.
    if (R.getResultKind() == LookupResult::NotFoundInCurrentInstantiation)
      return ActOnDependentIdExpression(SS, NameInfo, IsAddressOfOperand,
                                        TemplateArgs);
      
    // If this reference is in an Objective-C method, then we need to do
    // some special Objective-C lookup, too.
    if (IvarLookupFollowUp) {
      ExprResult E(LookupInObjCMethod(R, S, II, true));
      if (E.isInvalid())
        return ExprError();

      if (Expr *Ex = E.takeAs<Expr>())
        return Owned(Ex);
      
      // for further use, this must be set to false if in class method.
      IvarLookupFollowUp = getCurMethodDecl()->isInstanceMethod();
    }
  }

  if (R.isAmbiguous())
    return ExprError();

  // Determine whether this name might be a candidate for
  // argument-dependent lookup.
  bool ADL = UseArgumentDependentLookup(SS, R, HasTrailingLParen);

  if (R.empty() && !ADL) {
    // Otherwise, this could be an implicitly declared function reference (legal
    // in C90, extension in C99, forbidden in C++).
    if (HasTrailingLParen && II && !getLangOptions().CPlusPlus) {
      NamedDecl *D = ImplicitlyDefineFunction(NameLoc, *II, S);
      if (D) R.addDecl(D);
    }

    // If this name wasn't predeclared and if this is not a function
    // call, diagnose the problem.
    if (R.empty()) {

      // In Microsoft mode, if we are inside a template class member function
      // and we can't resolve an identifier then assume the identifier is type
      // dependent. The goal is to postpone name lookup to instantiation time 
      // to be able to search into type dependent base classes.
      if (getLangOptions().MicrosoftMode && CurContext->isDependentContext() &&
          isa<CXXMethodDecl>(CurContext))
        return ActOnDependentIdExpression(SS, NameInfo, IsAddressOfOperand,
                                          TemplateArgs);

      if (DiagnoseEmptyLookup(S, SS, R, CTC_Unknown))
        return ExprError();

      assert(!R.empty() &&
             "DiagnoseEmptyLookup returned false but added no results");

      // If we found an Objective-C instance variable, let
      // LookupInObjCMethod build the appropriate expression to
      // reference the ivar.
      if (ObjCIvarDecl *Ivar = R.getAsSingle<ObjCIvarDecl>()) {
        R.clear();
        ExprResult E(LookupInObjCMethod(R, S, Ivar->getIdentifier()));
        // In a hopelessly buggy code, Objective-C instance variable
        // lookup fails and no expression will be built to reference it.
        if (!E.isInvalid() && !E.get())
          return ExprError();
        return move(E);
      }
    }
  }

  // This is guaranteed from this point on.
  assert(!R.empty() || ADL);

  // Check whether this might be a C++ implicit instance member access.
  // C++ [class.mfct.non-static]p3:
  //   When an id-expression that is not part of a class member access
  //   syntax and not used to form a pointer to member is used in the
  //   body of a non-static member function of class X, if name lookup
  //   resolves the name in the id-expression to a non-static non-type
  //   member of some class C, the id-expression is transformed into a
  //   class member access expression using (*this) as the
  //   postfix-expression to the left of the . operator.
  //
  // But we don't actually need to do this for '&' operands if R
  // resolved to a function or overloaded function set, because the
  // expression is ill-formed if it actually works out to be a
  // non-static member function:
  //
  // C++ [expr.ref]p4:
  //   Otherwise, if E1.E2 refers to a non-static member function. . .
  //   [t]he expression can be used only as the left-hand operand of a
  //   member function call.
  //
  // There are other safeguards against such uses, but it's important
  // to get this right here so that we don't end up making a
  // spuriously dependent expression if we're inside a dependent
  // instance method.
  if (!R.empty() && (*R.begin())->isCXXClassMember()) {
    bool MightBeImplicitMember;
    if (!IsAddressOfOperand)
      MightBeImplicitMember = true;
    else if (!SS.isEmpty())
      MightBeImplicitMember = false;
    else if (R.isOverloadedResult())
      MightBeImplicitMember = false;
    else if (R.isUnresolvableResult())
      MightBeImplicitMember = true;
    else
      MightBeImplicitMember = isa<FieldDecl>(R.getFoundDecl()) ||
                              isa<IndirectFieldDecl>(R.getFoundDecl());

    if (MightBeImplicitMember)
      return BuildPossibleImplicitMemberExpr(SS, R, TemplateArgs);
  }

  if (TemplateArgs)
    return BuildTemplateIdExpr(SS, R, ADL, *TemplateArgs);

  return BuildDeclarationNameExpr(SS, R, ADL);
}

/// BuildQualifiedDeclarationNameExpr - Build a C++ qualified
/// declaration name, generally during template instantiation.
/// There's a large number of things which don't need to be done along
/// this path.
ExprResult
Sema::BuildQualifiedDeclarationNameExpr(CXXScopeSpec &SS,
                                        const DeclarationNameInfo &NameInfo) {
  DeclContext *DC;
  if (!(DC = computeDeclContext(SS, false)) || DC->isDependentContext())
    return BuildDependentDeclRefExpr(SS, NameInfo, 0);

  if (RequireCompleteDeclContext(SS, DC))
    return ExprError();

  LookupResult R(*this, NameInfo, LookupOrdinaryName);
  LookupQualifiedName(R, DC);

  if (R.isAmbiguous())
    return ExprError();

  if (R.empty()) {
    Diag(NameInfo.getLoc(), diag::err_no_member)
      << NameInfo.getName() << DC << SS.getRange();
    return ExprError();
  }

  return BuildDeclarationNameExpr(SS, R, /*ADL*/ false);
}

/// LookupInObjCMethod - The parser has read a name in, and Sema has
/// detected that we're currently inside an ObjC method.  Perform some
/// additional lookup.
///
/// Ideally, most of this would be done by lookup, but there's
/// actually quite a lot of extra work involved.
///
/// Returns a null sentinel to indicate trivial success.
ExprResult
Sema::LookupInObjCMethod(LookupResult &Lookup, Scope *S,
                         IdentifierInfo *II, bool AllowBuiltinCreation) {
  SourceLocation Loc = Lookup.getNameLoc();
  ObjCMethodDecl *CurMethod = getCurMethodDecl();

  // There are two cases to handle here.  1) scoped lookup could have failed,
  // in which case we should look for an ivar.  2) scoped lookup could have
  // found a decl, but that decl is outside the current instance method (i.e.
  // a global variable).  In these two cases, we do a lookup for an ivar with
  // this name, if the lookup sucedes, we replace it our current decl.

  // If we're in a class method, we don't normally want to look for
  // ivars.  But if we don't find anything else, and there's an
  // ivar, that's an error.
  bool IsClassMethod = CurMethod->isClassMethod();

  bool LookForIvars;
  if (Lookup.empty())
    LookForIvars = true;
  else if (IsClassMethod)
    LookForIvars = false;
  else
    LookForIvars = (Lookup.isSingleResult() &&
                    Lookup.getFoundDecl()->isDefinedOutsideFunctionOrMethod());
  ObjCInterfaceDecl *IFace = 0;
  if (LookForIvars) {
    IFace = CurMethod->getClassInterface();
    ObjCInterfaceDecl *ClassDeclared;
    if (ObjCIvarDecl *IV = IFace->lookupInstanceVariable(II, ClassDeclared)) {
      // Diagnose using an ivar in a class method.
      if (IsClassMethod)
        return ExprError(Diag(Loc, diag::error_ivar_use_in_class_method)
                         << IV->getDeclName());

      // If we're referencing an invalid decl, just return this as a silent
      // error node.  The error diagnostic was already emitted on the decl.
      if (IV->isInvalidDecl())
        return ExprError();

      // Check if referencing a field with __attribute__((deprecated)).
      if (DiagnoseUseOfDecl(IV, Loc))
        return ExprError();

      // Diagnose the use of an ivar outside of the declaring class.
      if (IV->getAccessControl() == ObjCIvarDecl::Private &&
          ClassDeclared != IFace)
        Diag(Loc, diag::error_private_ivar_access) << IV->getDeclName();

      // FIXME: This should use a new expr for a direct reference, don't
      // turn this into Self->ivar, just return a BareIVarExpr or something.
      IdentifierInfo &II = Context.Idents.get("self");
      UnqualifiedId SelfName;
      SelfName.setIdentifier(&II, SourceLocation());
      SelfName.setKind(UnqualifiedId::IK_ImplicitSelfParam);
      CXXScopeSpec SelfScopeSpec;
      ExprResult SelfExpr = ActOnIdExpression(S, SelfScopeSpec,
                                              SelfName, false, false);
      if (SelfExpr.isInvalid())
        return ExprError();

      SelfExpr = DefaultLvalueConversion(SelfExpr.take());
      if (SelfExpr.isInvalid())
        return ExprError();

      MarkDeclarationReferenced(Loc, IV);
      return Owned(new (Context)
                   ObjCIvarRefExpr(IV, IV->getType(), Loc,
                                   SelfExpr.take(), true, true));
    }
  } else if (CurMethod->isInstanceMethod()) {
    // We should warn if a local variable hides an ivar.
    ObjCInterfaceDecl *IFace = CurMethod->getClassInterface();
    ObjCInterfaceDecl *ClassDeclared;
    if (ObjCIvarDecl *IV = IFace->lookupInstanceVariable(II, ClassDeclared)) {
      if (IV->getAccessControl() != ObjCIvarDecl::Private ||
          IFace == ClassDeclared)
        Diag(Loc, diag::warn_ivar_use_hidden) << IV->getDeclName();
    }
  }

  if (Lookup.empty() && II && AllowBuiltinCreation) {
    // FIXME. Consolidate this with similar code in LookupName.
    if (unsigned BuiltinID = II->getBuiltinID()) {
      if (!(getLangOptions().CPlusPlus &&
            Context.BuiltinInfo.isPredefinedLibFunction(BuiltinID))) {
        NamedDecl *D = LazilyCreateBuiltin((IdentifierInfo *)II, BuiltinID,
                                           S, Lookup.isForRedeclaration(),
                                           Lookup.getNameLoc());
        if (D) Lookup.addDecl(D);
      }
    }
  }
  // Sentinel value saying that we didn't do anything special.
  return Owned((Expr*) 0);
}

/// \brief Cast a base object to a member's actual type.
///
/// Logically this happens in three phases:
///
/// * First we cast from the base type to the naming class.
///   The naming class is the class into which we were looking
///   when we found the member;  it's the qualifier type if a
///   qualifier was provided, and otherwise it's the base type.
///
/// * Next we cast from the naming class to the declaring class.
///   If the member we found was brought into a class's scope by
///   a using declaration, this is that class;  otherwise it's
///   the class declaring the member.
///
/// * Finally we cast from the declaring class to the "true"
///   declaring class of the member.  This conversion does not
///   obey access control.
ExprResult
Sema::PerformObjectMemberConversion(Expr *From,
                                    NestedNameSpecifier *Qualifier,
                                    NamedDecl *FoundDecl,
                                    NamedDecl *Member) {
  CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(Member->getDeclContext());
  if (!RD)
    return Owned(From);

  QualType DestRecordType;
  QualType DestType;
  QualType FromRecordType;
  QualType FromType = From->getType();
  bool PointerConversions = false;
  if (isa<FieldDecl>(Member)) {
    DestRecordType = Context.getCanonicalType(Context.getTypeDeclType(RD));

    if (FromType->getAs<PointerType>()) {
      DestType = Context.getPointerType(DestRecordType);
      FromRecordType = FromType->getPointeeType();
      PointerConversions = true;
    } else {
      DestType = DestRecordType;
      FromRecordType = FromType;
    }
  } else if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(Member)) {
    if (Method->isStatic())
      return Owned(From);

    DestType = Method->getThisType(Context);
    DestRecordType = DestType->getPointeeType();

    if (FromType->getAs<PointerType>()) {
      FromRecordType = FromType->getPointeeType();
      PointerConversions = true;
    } else {
      FromRecordType = FromType;
      DestType = DestRecordType;
    }
  } else {
    // No conversion necessary.
    return Owned(From);
  }

  if (DestType->isDependentType() || FromType->isDependentType())
    return Owned(From);

  // If the unqualified types are the same, no conversion is necessary.
  if (Context.hasSameUnqualifiedType(FromRecordType, DestRecordType))
    return Owned(From);

  SourceRange FromRange = From->getSourceRange();
  SourceLocation FromLoc = FromRange.getBegin();

  ExprValueKind VK = From->getValueKind();

  // C++ [class.member.lookup]p8:
  //   [...] Ambiguities can often be resolved by qualifying a name with its
  //   class name.
  //
  // If the member was a qualified name and the qualified referred to a
  // specific base subobject type, we'll cast to that intermediate type
  // first and then to the object in which the member is declared. That allows
  // one to resolve ambiguities in, e.g., a diamond-shaped hierarchy such as:
  //
  //   class Base { public: int x; };
  //   class Derived1 : public Base { };
  //   class Derived2 : public Base { };
  //   class VeryDerived : public Derived1, public Derived2 { void f(); };
  //
  //   void VeryDerived::f() {
  //     x = 17; // error: ambiguous base subobjects
  //     Derived1::x = 17; // okay, pick the Base subobject of Derived1
  //   }
  if (Qualifier) {
    QualType QType = QualType(Qualifier->getAsType(), 0);
    assert(!QType.isNull() && "lookup done with dependent qualifier?");
    assert(QType->isRecordType() && "lookup done with non-record type");

    QualType QRecordType = QualType(QType->getAs<RecordType>(), 0);

    // In C++98, the qualifier type doesn't actually have to be a base
    // type of the object type, in which case we just ignore it.
    // Otherwise build the appropriate casts.
    if (IsDerivedFrom(FromRecordType, QRecordType)) {
      CXXCastPath BasePath;
      if (CheckDerivedToBaseConversion(FromRecordType, QRecordType,
                                       FromLoc, FromRange, &BasePath))
        return ExprError();

      if (PointerConversions)
        QType = Context.getPointerType(QType);
      From = ImpCastExprToType(From, QType, CK_UncheckedDerivedToBase,
                               VK, &BasePath).take();

      FromType = QType;
      FromRecordType = QRecordType;

      // If the qualifier type was the same as the destination type,
      // we're done.
      if (Context.hasSameUnqualifiedType(FromRecordType, DestRecordType))
        return Owned(From);
    }
  }

  bool IgnoreAccess = false;

  // If we actually found the member through a using declaration, cast
  // down to the using declaration's type.
  //
  // Pointer equality is fine here because only one declaration of a
  // class ever has member declarations.
  if (FoundDecl->getDeclContext() != Member->getDeclContext()) {
    assert(isa<UsingShadowDecl>(FoundDecl));
    QualType URecordType = Context.getTypeDeclType(
                           cast<CXXRecordDecl>(FoundDecl->getDeclContext()));

    // We only need to do this if the naming-class to declaring-class
    // conversion is non-trivial.
    if (!Context.hasSameUnqualifiedType(FromRecordType, URecordType)) {
      assert(IsDerivedFrom(FromRecordType, URecordType));
      CXXCastPath BasePath;
      if (CheckDerivedToBaseConversion(FromRecordType, URecordType,
                                       FromLoc, FromRange, &BasePath))
        return ExprError();

      QualType UType = URecordType;
      if (PointerConversions)
        UType = Context.getPointerType(UType);
      From = ImpCastExprToType(From, UType, CK_UncheckedDerivedToBase,
                               VK, &BasePath).take();
      FromType = UType;
      FromRecordType = URecordType;
    }

    // We don't do access control for the conversion from the
    // declaring class to the true declaring class.
    IgnoreAccess = true;
  }

  CXXCastPath BasePath;
  if (CheckDerivedToBaseConversion(FromRecordType, DestRecordType,
                                   FromLoc, FromRange, &BasePath,
                                   IgnoreAccess))
    return ExprError();

  return ImpCastExprToType(From, DestType, CK_UncheckedDerivedToBase,
                           VK, &BasePath);
}

bool Sema::UseArgumentDependentLookup(const CXXScopeSpec &SS,
                                      const LookupResult &R,
                                      bool HasTrailingLParen) {
  // Only when used directly as the postfix-expression of a call.
  if (!HasTrailingLParen)
    return false;

  // Never if a scope specifier was provided.
  if (SS.isSet())
    return false;

  // Only in C++ or ObjC++.
  if (!getLangOptions().CPlusPlus)
    return false;

  // Turn off ADL when we find certain kinds of declarations during
  // normal lookup:
  for (LookupResult::iterator I = R.begin(), E = R.end(); I != E; ++I) {
    NamedDecl *D = *I;

    // C++0x [basic.lookup.argdep]p3:
    //     -- a declaration of a class member
    // Since using decls preserve this property, we check this on the
    // original decl.
    if (D->isCXXClassMember())
      return false;

    // C++0x [basic.lookup.argdep]p3:
    //     -- a block-scope function declaration that is not a
    //        using-declaration
    // NOTE: we also trigger this for function templates (in fact, we
    // don't check the decl type at all, since all other decl types
    // turn off ADL anyway).
    if (isa<UsingShadowDecl>(D))
      D = cast<UsingShadowDecl>(D)->getTargetDecl();
    else if (D->getDeclContext()->isFunctionOrMethod())
      return false;

    // C++0x [basic.lookup.argdep]p3:
    //     -- a declaration that is neither a function or a function
    //        template
    // And also for builtin functions.
    if (isa<FunctionDecl>(D)) {
      FunctionDecl *FDecl = cast<FunctionDecl>(D);

      // But also builtin functions.
      if (FDecl->getBuiltinID() && FDecl->isImplicit())
        return false;
    } else if (!isa<FunctionTemplateDecl>(D))
      return false;
  }

  return true;
}


/// Diagnoses obvious problems with the use of the given declaration
/// as an expression.  This is only actually called for lookups that
/// were not overloaded, and it doesn't promise that the declaration
/// will in fact be used.
static bool CheckDeclInExpr(Sema &S, SourceLocation Loc, NamedDecl *D) {
  if (isa<TypedefNameDecl>(D)) {
    S.Diag(Loc, diag::err_unexpected_typedef) << D->getDeclName();
    return true;
  }

  if (isa<ObjCInterfaceDecl>(D)) {
    S.Diag(Loc, diag::err_unexpected_interface) << D->getDeclName();
    return true;
  }

  if (isa<NamespaceDecl>(D)) {
    S.Diag(Loc, diag::err_unexpected_namespace) << D->getDeclName();
    return true;
  }

  return false;
}

ExprResult
Sema::BuildDeclarationNameExpr(const CXXScopeSpec &SS,
                               LookupResult &R,
                               bool NeedsADL) {
  // If this is a single, fully-resolved result and we don't need ADL,
  // just build an ordinary singleton decl ref.
  if (!NeedsADL && R.isSingleResult() && !R.getAsSingle<FunctionTemplateDecl>())
    return BuildDeclarationNameExpr(SS, R.getLookupNameInfo(),
                                    R.getFoundDecl());

  // We only need to check the declaration if there's exactly one
  // result, because in the overloaded case the results can only be
  // functions and function templates.
  if (R.isSingleResult() &&
      CheckDeclInExpr(*this, R.getNameLoc(), R.getFoundDecl()))
    return ExprError();

  // Otherwise, just build an unresolved lookup expression.  Suppress
  // any lookup-related diagnostics; we'll hash these out later, when
  // we've picked a target.
  R.suppressDiagnostics();

  UnresolvedLookupExpr *ULE
    = UnresolvedLookupExpr::Create(Context, R.getNamingClass(),
                                   SS.getWithLocInContext(Context),
                                   R.getLookupNameInfo(),
                                   NeedsADL, R.isOverloadedResult(),
                                   R.begin(), R.end());

  return Owned(ULE);
}

/// \brief Complete semantic analysis for a reference to the given declaration.
ExprResult
Sema::BuildDeclarationNameExpr(const CXXScopeSpec &SS,
                               const DeclarationNameInfo &NameInfo,
                               NamedDecl *D) {
  assert(D && "Cannot refer to a NULL declaration");
  assert(!isa<FunctionTemplateDecl>(D) &&
         "Cannot refer unambiguously to a function template");

  SourceLocation Loc = NameInfo.getLoc();
  if (CheckDeclInExpr(*this, Loc, D))
    return ExprError();

  if (TemplateDecl *Template = dyn_cast<TemplateDecl>(D)) {
    // Specifically diagnose references to class templates that are missing
    // a template argument list.
    Diag(Loc, diag::err_template_decl_ref)
      << Template << SS.getRange();
    Diag(Template->getLocation(), diag::note_template_decl_here);
    return ExprError();
  }

  // Make sure that we're referring to a value.
  ValueDecl *VD = dyn_cast<ValueDecl>(D);
  if (!VD) {
    Diag(Loc, diag::err_ref_non_value)
      << D << SS.getRange();
    Diag(D->getLocation(), diag::note_declared_at);
    return ExprError();
  }

  // Check whether this declaration can be used. Note that we suppress
  // this check when we're going to perform argument-dependent lookup
  // on this function name, because this might not be the function
  // that overload resolution actually selects.
  if (DiagnoseUseOfDecl(VD, Loc))
    return ExprError();

  // Only create DeclRefExpr's for valid Decl's.
  if (VD->isInvalidDecl())
    return ExprError();

  // Handle members of anonymous structs and unions.  If we got here,
  // and the reference is to a class member indirect field, then this
  // must be the subject of a pointer-to-member expression.
  if (IndirectFieldDecl *indirectField = dyn_cast<IndirectFieldDecl>(VD))
    if (!indirectField->isCXXClassMember())
      return BuildAnonymousStructUnionMemberReference(SS, NameInfo.getLoc(),
                                                      indirectField);

  // If the identifier reference is inside a block, and it refers to a value
  // that is outside the block, create a BlockDeclRefExpr instead of a
  // DeclRefExpr.  This ensures the value is treated as a copy-in snapshot when
  // the block is formed.
  //
  // We do not do this for things like enum constants, global variables, etc,
  // as they do not get snapshotted.
  //
  switch (shouldCaptureValueReference(*this, NameInfo.getLoc(), VD)) {
  case CR_Error:
    return ExprError();

  case CR_Capture:
    assert(!SS.isSet() && "referenced local variable with scope specifier?");
    return BuildBlockDeclRefExpr(*this, VD, NameInfo, /*byref*/ false);

  case CR_CaptureByRef:
    assert(!SS.isSet() && "referenced local variable with scope specifier?");
    return BuildBlockDeclRefExpr(*this, VD, NameInfo, /*byref*/ true);

  case CR_NoCapture: {
    // If this reference is not in a block or if the referenced
    // variable is within the block, create a normal DeclRefExpr.

    QualType type = VD->getType();
    ExprValueKind valueKind = VK_RValue;

    switch (D->getKind()) {
    // Ignore all the non-ValueDecl kinds.
#define ABSTRACT_DECL(kind)
#define VALUE(type, base)
#define DECL(type, base) \
    case Decl::type:
#include "clang/AST/DeclNodes.inc"
      llvm_unreachable("invalid value decl kind");
      return ExprError();

    // These shouldn't make it here.
    case Decl::ObjCAtDefsField:
    case Decl::ObjCIvar:
      llvm_unreachable("forming non-member reference to ivar?");
      return ExprError();

    // Enum constants are always r-values and never references.
    // Unresolved using declarations are dependent.
    case Decl::EnumConstant:
    case Decl::UnresolvedUsingValue:
      valueKind = VK_RValue;
      break;

    // Fields and indirect fields that got here must be for
    // pointer-to-member expressions; we just call them l-values for
    // internal consistency, because this subexpression doesn't really
    // exist in the high-level semantics.
    case Decl::Field:
    case Decl::IndirectField:
      assert(getLangOptions().CPlusPlus &&
             "building reference to field in C?");

      // These can't have reference type in well-formed programs, but
      // for internal consistency we do this anyway.
      type = type.getNonReferenceType();
      valueKind = VK_LValue;
      break;

    // Non-type template parameters are either l-values or r-values
    // depending on the type.
    case Decl::NonTypeTemplateParm: {
      if (const ReferenceType *reftype = type->getAs<ReferenceType>()) {
        type = reftype->getPointeeType();
        valueKind = VK_LValue; // even if the parameter is an r-value reference
        break;
      }

      // For non-references, we need to strip qualifiers just in case
      // the template parameter was declared as 'const int' or whatever.
      valueKind = VK_RValue;
      type = type.getUnqualifiedType();
      break;
    }

    case Decl::Var:
      // In C, "extern void blah;" is valid and is an r-value.
      if (!getLangOptions().CPlusPlus &&
          !type.hasQualifiers() &&
          type->isVoidType()) {
        valueKind = VK_RValue;
        break;
      }
      // fallthrough

    case Decl::ImplicitParam:
    case Decl::ParmVar:
      // These are always l-values.
      valueKind = VK_LValue;
      type = type.getNonReferenceType();
      break;

    case Decl::Function: {
      const FunctionType *fty = type->castAs<FunctionType>();

      // If we're referring to a function with an __unknown_anytype
      // result type, make the entire expression __unknown_anytype.
      if (fty->getResultType() == Context.UnknownAnyTy) {
        type = Context.UnknownAnyTy;
        valueKind = VK_RValue;
        break;
      }

      // Functions are l-values in C++.
      if (getLangOptions().CPlusPlus) {
        valueKind = VK_LValue;
        break;
      }
      
      // C99 DR 316 says that, if a function type comes from a
      // function definition (without a prototype), that type is only
      // used for checking compatibility. Therefore, when referencing
      // the function, we pretend that we don't have the full function
      // type.
      if (!cast<FunctionDecl>(VD)->hasPrototype() &&
          isa<FunctionProtoType>(fty))
        type = Context.getFunctionNoProtoType(fty->getResultType(),
                                              fty->getExtInfo());

      // Functions are r-values in C.
      valueKind = VK_RValue;
      break;
    }

    case Decl::CXXMethod:
      // If we're referring to a method with an __unknown_anytype
      // result type, make the entire expression __unknown_anytype.
      // This should only be possible with a type written directly.
      if (const FunctionProtoType *proto
            = dyn_cast<FunctionProtoType>(VD->getType()))
        if (proto->getResultType() == Context.UnknownAnyTy) {
          type = Context.UnknownAnyTy;
          valueKind = VK_RValue;
          break;
        }

      // C++ methods are l-values if static, r-values if non-static.
      if (cast<CXXMethodDecl>(VD)->isStatic()) {
        valueKind = VK_LValue;
        break;
      }
      // fallthrough

    case Decl::CXXConversion:
    case Decl::CXXDestructor:
    case Decl::CXXConstructor:
      valueKind = VK_RValue;
      break;
    }

    return BuildDeclRefExpr(VD, type, valueKind, NameInfo, &SS);
  }

  }

  llvm_unreachable("unknown capture result");
  return ExprError();
}

ExprResult Sema::ActOnPredefinedExpr(SourceLocation Loc, tok::TokenKind Kind) {
  PredefinedExpr::IdentType IT;

  switch (Kind) {
  default: llvm_unreachable("Unknown simple primary expr!");
  case tok::kw___func__: IT = PredefinedExpr::Func; break; // [C99 6.4.2.2]
  case tok::kw___FUNCTION__: IT = PredefinedExpr::Function; break;
  case tok::kw___PRETTY_FUNCTION__: IT = PredefinedExpr::PrettyFunction; break;
  }

  // Pre-defined identifiers are of type char[x], where x is the length of the
  // string.

  Decl *currentDecl = getCurFunctionOrMethodDecl();
  if (!currentDecl && getCurBlock())
    currentDecl = getCurBlock()->TheDecl;
  if (!currentDecl) {
    Diag(Loc, diag::ext_predef_outside_function);
    currentDecl = Context.getTranslationUnitDecl();
  }

  QualType ResTy;
  if (cast<DeclContext>(currentDecl)->isDependentContext()) {
    ResTy = Context.DependentTy;
  } else {
    unsigned Length = PredefinedExpr::ComputeName(IT, currentDecl).length();

    llvm::APInt LengthI(32, Length + 1);
    ResTy = Context.CharTy.withConst();
    ResTy = Context.getConstantArrayType(ResTy, LengthI, ArrayType::Normal, 0);
  }
  return Owned(new (Context) PredefinedExpr(Loc, ResTy, IT));
}

ExprResult Sema::ActOnCharacterConstant(const Token &Tok) {
  llvm::SmallString<16> CharBuffer;
  bool Invalid = false;
  StringRef ThisTok = PP.getSpelling(Tok, CharBuffer, &Invalid);
  if (Invalid)
    return ExprError();

  CharLiteralParser Literal(ThisTok.begin(), ThisTok.end(), Tok.getLocation(),
                            PP, Tok.getKind());
  if (Literal.hadError())
    return ExprError();

  QualType Ty;
  if (!getLangOptions().CPlusPlus)
    Ty = Context.IntTy;   // 'x' and L'x' -> int in C.
  else if (Literal.isWide())
    Ty = Context.WCharTy; // L'x' -> wchar_t in C++.
  else if (Literal.isUTF16())
    Ty = Context.Char16Ty; // u'x' -> char16_t in C++0x.
  else if (Literal.isUTF32())
    Ty = Context.Char32Ty; // U'x' -> char32_t in C++0x.
  else if (Literal.isMultiChar())
    Ty = Context.IntTy;   // 'wxyz' -> int in C++.
  else
    Ty = Context.CharTy;  // 'x' -> char in C++

  CharacterLiteral::CharacterKind Kind = CharacterLiteral::Ascii;
  if (Literal.isWide())
    Kind = CharacterLiteral::Wide;
  else if (Literal.isUTF16())
    Kind = CharacterLiteral::UTF16;
  else if (Literal.isUTF32())
    Kind = CharacterLiteral::UTF32;

  return Owned(new (Context) CharacterLiteral(Literal.getValue(), Kind, Ty,
                                              Tok.getLocation()));
}

ExprResult Sema::ActOnNumericConstant(const Token &Tok) {
  // Fast path for a single digit (which is quite common).  A single digit
  // cannot have a trigraph, escaped newline, radix prefix, or type suffix.
  if (Tok.getLength() == 1) {
    const char Val = PP.getSpellingOfSingleCharacterNumericConstant(Tok);
    unsigned IntSize = Context.getTargetInfo().getIntWidth();
    return Owned(IntegerLiteral::Create(Context, llvm::APInt(IntSize, Val-'0'),
                    Context.IntTy, Tok.getLocation()));
  }

  llvm::SmallString<512> IntegerBuffer;
  // Add padding so that NumericLiteralParser can overread by one character.
  IntegerBuffer.resize(Tok.getLength()+1);
  const char *ThisTokBegin = &IntegerBuffer[0];

  // Get the spelling of the token, which eliminates trigraphs, etc.
  bool Invalid = false;
  unsigned ActualLength = PP.getSpelling(Tok, ThisTokBegin, &Invalid);
  if (Invalid)
    return ExprError();

  NumericLiteralParser Literal(ThisTokBegin, ThisTokBegin+ActualLength,
                               Tok.getLocation(), PP);
  if (Literal.hadError)
    return ExprError();

  Expr *Res;

  if (Literal.isFloatingLiteral()) {
    QualType Ty;
    if (Literal.isFloat)
      Ty = Context.FloatTy;
    else if (!Literal.isLong)
      Ty = Context.DoubleTy;
    else
      Ty = Context.LongDoubleTy;

    const llvm::fltSemantics &Format = Context.getFloatTypeSemantics(Ty);

    using llvm::APFloat;
    APFloat Val(Format);

    APFloat::opStatus result = Literal.GetFloatValue(Val);

    // Overflow is always an error, but underflow is only an error if
    // we underflowed to zero (APFloat reports denormals as underflow).
    if ((result & APFloat::opOverflow) ||
        ((result & APFloat::opUnderflow) && Val.isZero())) {
      unsigned diagnostic;
      llvm::SmallString<20> buffer;
      if (result & APFloat::opOverflow) {
        diagnostic = diag::warn_float_overflow;
        APFloat::getLargest(Format).toString(buffer);
      } else {
        diagnostic = diag::warn_float_underflow;
        APFloat::getSmallest(Format).toString(buffer);
      }

      Diag(Tok.getLocation(), diagnostic)
        << Ty
        << StringRef(buffer.data(), buffer.size());
    }

    bool isExact = (result == APFloat::opOK);
    Res = FloatingLiteral::Create(Context, Val, isExact, Ty, Tok.getLocation());

    if (Ty == Context.DoubleTy) {
      if (getLangOptions().SinglePrecisionConstants) {
        Res = ImpCastExprToType(Res, Context.FloatTy, CK_FloatingCast).take();
      } else if (getLangOptions().OpenCL && !getOpenCLOptions().cl_khr_fp64) {
        Diag(Tok.getLocation(), diag::warn_double_const_requires_fp64);
        Res = ImpCastExprToType(Res, Context.FloatTy, CK_FloatingCast).take();
      }
    }
  } else if (!Literal.isIntegerLiteral()) {
    return ExprError();
  } else {
    QualType Ty;

    // long long is a C99 feature.
    if (!getLangOptions().C99 && !getLangOptions().CPlusPlus0x &&
        Literal.isLongLong)
      Diag(Tok.getLocation(), diag::ext_longlong);

    // Get the value in the widest-possible width.
    llvm::APInt ResultVal(Context.getTargetInfo().getIntMaxTWidth(), 0);

    if (Literal.GetIntegerValue(ResultVal)) {
      // If this value didn't fit into uintmax_t, warn and force to ull.
      Diag(Tok.getLocation(), diag::warn_integer_too_large);
      Ty = Context.UnsignedLongLongTy;
      assert(Context.getTypeSize(Ty) == ResultVal.getBitWidth() &&
             "long long is not intmax_t?");
    } else {
      // If this value fits into a ULL, try to figure out what else it fits into
      // according to the rules of C99 6.4.4.1p5.

      // Octal, Hexadecimal, and integers with a U suffix are allowed to
      // be an unsigned int.
      bool AllowUnsigned = Literal.isUnsigned || Literal.getRadix() != 10;

      // Check from smallest to largest, picking the smallest type we can.
      unsigned Width = 0;
      if (!Literal.isLong && !Literal.isLongLong) {
        // Are int/unsigned possibilities?
        unsigned IntSize = Context.getTargetInfo().getIntWidth();

        // Does it fit in a unsigned int?
        if (ResultVal.isIntN(IntSize)) {
          // Does it fit in a signed int?
          if (!Literal.isUnsigned && ResultVal[IntSize-1] == 0)
            Ty = Context.IntTy;
          else if (AllowUnsigned)
            Ty = Context.UnsignedIntTy;
          Width = IntSize;
        }
      }

      // Are long/unsigned long possibilities?
      if (Ty.isNull() && !Literal.isLongLong) {
        unsigned LongSize = Context.getTargetInfo().getLongWidth();

        // Does it fit in a unsigned long?
        if (ResultVal.isIntN(LongSize)) {
          // Does it fit in a signed long?
          if (!Literal.isUnsigned && ResultVal[LongSize-1] == 0)
            Ty = Context.LongTy;
          else if (AllowUnsigned)
            Ty = Context.UnsignedLongTy;
          Width = LongSize;
        }
      }

      // Finally, check long long if needed.
      if (Ty.isNull()) {
        unsigned LongLongSize = Context.getTargetInfo().getLongLongWidth();

        // Does it fit in a unsigned long long?
        if (ResultVal.isIntN(LongLongSize)) {
          // Does it fit in a signed long long?
          // To be compatible with MSVC, hex integer literals ending with the
          // LL or i64 suffix are always signed in Microsoft mode.
          if (!Literal.isUnsigned && (ResultVal[LongLongSize-1] == 0 ||
              (getLangOptions().MicrosoftExt && Literal.isLongLong)))
            Ty = Context.LongLongTy;
          else if (AllowUnsigned)
            Ty = Context.UnsignedLongLongTy;
          Width = LongLongSize;
        }
      }

      // If we still couldn't decide a type, we probably have something that
      // does not fit in a signed long long, but has no U suffix.
      if (Ty.isNull()) {
        Diag(Tok.getLocation(), diag::warn_integer_too_large_for_signed);
        Ty = Context.UnsignedLongLongTy;
        Width = Context.getTargetInfo().getLongLongWidth();
      }

      if (ResultVal.getBitWidth() != Width)
        ResultVal = ResultVal.trunc(Width);
    }
    Res = IntegerLiteral::Create(Context, ResultVal, Ty, Tok.getLocation());
  }

  // If this is an imaginary literal, create the ImaginaryLiteral wrapper.
  if (Literal.isImaginary)
    Res = new (Context) ImaginaryLiteral(Res,
                                        Context.getComplexType(Res->getType()));

  return Owned(Res);
}

ExprResult Sema::ActOnParenExpr(SourceLocation L, SourceLocation R, Expr *E) {
  assert((E != 0) && "ActOnParenExpr() missing expr");
  return Owned(new (Context) ParenExpr(L, R, E));
}

static bool CheckVecStepTraitOperandType(Sema &S, QualType T,
                                         SourceLocation Loc,
                                         SourceRange ArgRange) {
  // [OpenCL 1.1 6.11.12] "The vec_step built-in function takes a built-in
  // scalar or vector data type argument..."
  // Every built-in scalar type (OpenCL 1.1 6.1.1) is either an arithmetic
  // type (C99 6.2.5p18) or void.
  if (!(T->isArithmeticType() || T->isVoidType() || T->isVectorType())) {
    S.Diag(Loc, diag::err_vecstep_non_scalar_vector_type)
      << T << ArgRange;
    return true;
  }

  assert((T->isVoidType() || !T->isIncompleteType()) &&
         "Scalar types should always be complete");
  return false;
}

static bool CheckExtensionTraitOperandType(Sema &S, QualType T,
                                           SourceLocation Loc,
                                           SourceRange ArgRange,
                                           UnaryExprOrTypeTrait TraitKind) {
  // C99 6.5.3.4p1:
  if (T->isFunctionType()) {
    // alignof(function) is allowed as an extension.
    if (TraitKind == UETT_SizeOf)
      S.Diag(Loc, diag::ext_sizeof_function_type) << ArgRange;
    return false;
  }

  // Allow sizeof(void)/alignof(void) as an extension.
  if (T->isVoidType()) {
    S.Diag(Loc, diag::ext_sizeof_void_type) << TraitKind << ArgRange;
    return false;
  }

  return true;
}

static bool CheckObjCTraitOperandConstraints(Sema &S, QualType T,
                                             SourceLocation Loc,
                                             SourceRange ArgRange,
                                             UnaryExprOrTypeTrait TraitKind) {
  // Reject sizeof(interface) and sizeof(interface<proto>) in 64-bit mode.
  if (S.LangOpts.ObjCNonFragileABI && T->isObjCObjectType()) {
    S.Diag(Loc, diag::err_sizeof_nonfragile_interface)
      << T << (TraitKind == UETT_SizeOf)
      << ArgRange;
    return true;
  }

  return false;
}

/// \brief Check the constrains on expression operands to unary type expression
/// and type traits.
///
/// Completes any types necessary and validates the constraints on the operand
/// expression. The logic mostly mirrors the type-based overload, but may modify
/// the expression as it completes the type for that expression through template
/// instantiation, etc.
bool Sema::CheckUnaryExprOrTypeTraitOperand(Expr *E,
                                            UnaryExprOrTypeTrait ExprKind) {
  QualType ExprTy = E->getType();

  // C++ [expr.sizeof]p2: "When applied to a reference or a reference type,
  //   the result is the size of the referenced type."
  // C++ [expr.alignof]p3: "When alignof is applied to a reference type, the
  //   result shall be the alignment of the referenced type."
  if (const ReferenceType *Ref = ExprTy->getAs<ReferenceType>())
    ExprTy = Ref->getPointeeType();

  if (ExprKind == UETT_VecStep)
    return CheckVecStepTraitOperandType(*this, ExprTy, E->getExprLoc(),
                                        E->getSourceRange());

  // Whitelist some types as extensions
  if (!CheckExtensionTraitOperandType(*this, ExprTy, E->getExprLoc(),
                                      E->getSourceRange(), ExprKind))
    return false;

  if (RequireCompleteExprType(E,
                              PDiag(diag::err_sizeof_alignof_incomplete_type)
                              << ExprKind << E->getSourceRange(),
                              std::make_pair(SourceLocation(), PDiag(0))))
    return true;

  // Completeing the expression's type may have changed it.
  ExprTy = E->getType();
  if (const ReferenceType *Ref = ExprTy->getAs<ReferenceType>())
    ExprTy = Ref->getPointeeType();

  if (CheckObjCTraitOperandConstraints(*this, ExprTy, E->getExprLoc(),
                                       E->getSourceRange(), ExprKind))
    return true;

  if (ExprKind == UETT_SizeOf) {
    if (DeclRefExpr *DeclRef = dyn_cast<DeclRefExpr>(E->IgnoreParens())) {
      if (ParmVarDecl *PVD = dyn_cast<ParmVarDecl>(DeclRef->getFoundDecl())) {
        QualType OType = PVD->getOriginalType();
        QualType Type = PVD->getType();
        if (Type->isPointerType() && OType->isArrayType()) {
          Diag(E->getExprLoc(), diag::warn_sizeof_array_param)
            << Type << OType;
          Diag(PVD->getLocation(), diag::note_declared_at);
        }
      }
    }
  }

  return false;
}

/// \brief Check the constraints on operands to unary expression and type
/// traits.
///
/// This will complete any types necessary, and validate the various constraints
/// on those operands.
///
/// The UsualUnaryConversions() function is *not* called by this routine.
/// C99 6.3.2.1p[2-4] all state:
///   Except when it is the operand of the sizeof operator ...
///
/// C++ [expr.sizeof]p4
///   The lvalue-to-rvalue, array-to-pointer, and function-to-pointer
///   standard conversions are not applied to the operand of sizeof.
///
/// This policy is followed for all of the unary trait expressions.
bool Sema::CheckUnaryExprOrTypeTraitOperand(QualType ExprType,
                                            SourceLocation OpLoc,
                                            SourceRange ExprRange,
                                            UnaryExprOrTypeTrait ExprKind) {
  if (ExprType->isDependentType())
    return false;

  // C++ [expr.sizeof]p2: "When applied to a reference or a reference type,
  //   the result is the size of the referenced type."
  // C++ [expr.alignof]p3: "When alignof is applied to a reference type, the
  //   result shall be the alignment of the referenced type."
  if (const ReferenceType *Ref = ExprType->getAs<ReferenceType>())
    ExprType = Ref->getPointeeType();

  if (ExprKind == UETT_VecStep)
    return CheckVecStepTraitOperandType(*this, ExprType, OpLoc, ExprRange);

  // Whitelist some types as extensions
  if (!CheckExtensionTraitOperandType(*this, ExprType, OpLoc, ExprRange,
                                      ExprKind))
    return false;

  if (RequireCompleteType(OpLoc, ExprType,
                          PDiag(diag::err_sizeof_alignof_incomplete_type)
                          << ExprKind << ExprRange))
    return true;

  if (CheckObjCTraitOperandConstraints(*this, ExprType, OpLoc, ExprRange,
                                       ExprKind))
    return true;

  return false;
}

static bool CheckAlignOfExpr(Sema &S, Expr *E) {
  E = E->IgnoreParens();

  // alignof decl is always ok.
  if (isa<DeclRefExpr>(E))
    return false;

  // Cannot know anything else if the expression is dependent.
  if (E->isTypeDependent())
    return false;

  if (E->getBitField()) {
    S.Diag(E->getExprLoc(), diag::err_sizeof_alignof_bitfield)
       << 1 << E->getSourceRange();
    return true;
  }

  // Alignment of a field access is always okay, so long as it isn't a
  // bit-field.
  if (MemberExpr *ME = dyn_cast<MemberExpr>(E))
    if (isa<FieldDecl>(ME->getMemberDecl()))
      return false;

  return S.CheckUnaryExprOrTypeTraitOperand(E, UETT_AlignOf);
}

bool Sema::CheckVecStepExpr(Expr *E) {
  E = E->IgnoreParens();

  // Cannot know anything else if the expression is dependent.
  if (E->isTypeDependent())
    return false;

  return CheckUnaryExprOrTypeTraitOperand(E, UETT_VecStep);
}

/// \brief Build a sizeof or alignof expression given a type operand.
ExprResult
Sema::CreateUnaryExprOrTypeTraitExpr(TypeSourceInfo *TInfo,
                                     SourceLocation OpLoc,
                                     UnaryExprOrTypeTrait ExprKind,
                                     SourceRange R) {
  if (!TInfo)
    return ExprError();

  QualType T = TInfo->getType();

  if (!T->isDependentType() &&
      CheckUnaryExprOrTypeTraitOperand(T, OpLoc, R, ExprKind))
    return ExprError();

  // C99 6.5.3.4p4: the type (an unsigned integer type) is size_t.
  return Owned(new (Context) UnaryExprOrTypeTraitExpr(ExprKind, TInfo,
                                                      Context.getSizeType(),
                                                      OpLoc, R.getEnd()));
}

/// \brief Build a sizeof or alignof expression given an expression
/// operand.
ExprResult
Sema::CreateUnaryExprOrTypeTraitExpr(Expr *E, SourceLocation OpLoc,
                                     UnaryExprOrTypeTrait ExprKind) {
  ExprResult PE = CheckPlaceholderExpr(E);
  if (PE.isInvalid()) 
    return ExprError();

  E = PE.get();
  
  // Verify that the operand is valid.
  bool isInvalid = false;
  if (E->isTypeDependent()) {
    // Delay type-checking for type-dependent expressions.
  } else if (ExprKind == UETT_AlignOf) {
    isInvalid = CheckAlignOfExpr(*this, E);
  } else if (ExprKind == UETT_VecStep) {
    isInvalid = CheckVecStepExpr(E);
  } else if (E->getBitField()) {  // C99 6.5.3.4p1.
    Diag(E->getExprLoc(), diag::err_sizeof_alignof_bitfield) << 0;
    isInvalid = true;
  } else {
    isInvalid = CheckUnaryExprOrTypeTraitOperand(E, UETT_SizeOf);
  }

  if (isInvalid)
    return ExprError();

  // C99 6.5.3.4p4: the type (an unsigned integer type) is size_t.
  return Owned(new (Context) UnaryExprOrTypeTraitExpr(
      ExprKind, E, Context.getSizeType(), OpLoc,
      E->getSourceRange().getEnd()));
}

/// ActOnUnaryExprOrTypeTraitExpr - Handle @c sizeof(type) and @c sizeof @c
/// expr and the same for @c alignof and @c __alignof
/// Note that the ArgRange is invalid if isType is false.
ExprResult
Sema::ActOnUnaryExprOrTypeTraitExpr(SourceLocation OpLoc,
                                    UnaryExprOrTypeTrait ExprKind, bool IsType,
                                    void *TyOrEx, const SourceRange &ArgRange) {
  // If error parsing type, ignore.
  if (TyOrEx == 0) return ExprError();

  if (IsType) {
    TypeSourceInfo *TInfo;
    (void) GetTypeFromParser(ParsedType::getFromOpaquePtr(TyOrEx), &TInfo);
    return CreateUnaryExprOrTypeTraitExpr(TInfo, OpLoc, ExprKind, ArgRange);
  }

  Expr *ArgEx = (Expr *)TyOrEx;
  ExprResult Result = CreateUnaryExprOrTypeTraitExpr(ArgEx, OpLoc, ExprKind);
  return move(Result);
}

static QualType CheckRealImagOperand(Sema &S, ExprResult &V, SourceLocation Loc,
                                     bool IsReal) {
  if (V.get()->isTypeDependent())
    return S.Context.DependentTy;

  // _Real and _Imag are only l-values for normal l-values.
  if (V.get()->getObjectKind() != OK_Ordinary) {
    V = S.DefaultLvalueConversion(V.take());
    if (V.isInvalid())
      return QualType();
  }

  // These operators return the element type of a complex type.
  if (const ComplexType *CT = V.get()->getType()->getAs<ComplexType>())
    return CT->getElementType();

  // Otherwise they pass through real integer and floating point types here.
  if (V.get()->getType()->isArithmeticType())
    return V.get()->getType();

  // Test for placeholders.
  ExprResult PR = S.CheckPlaceholderExpr(V.get());
  if (PR.isInvalid()) return QualType();
  if (PR.get() != V.get()) {
    V = move(PR);
    return CheckRealImagOperand(S, V, Loc, IsReal);
  }

  // Reject anything else.
  S.Diag(Loc, diag::err_realimag_invalid_type) << V.get()->getType()
    << (IsReal ? "__real" : "__imag");
  return QualType();
}



ExprResult
Sema::ActOnPostfixUnaryOp(Scope *S, SourceLocation OpLoc,
                          tok::TokenKind Kind, Expr *Input) {
  UnaryOperatorKind Opc;
  switch (Kind) {
  default: llvm_unreachable("Unknown unary op!");
  case tok::plusplus:   Opc = UO_PostInc; break;
  case tok::minusminus: Opc = UO_PostDec; break;
  }

  return BuildUnaryOp(S, OpLoc, Opc, Input);
}

ExprResult
Sema::ActOnArraySubscriptExpr(Scope *S, Expr *Base, SourceLocation LLoc,
                              Expr *Idx, SourceLocation RLoc) {
  // Since this might be a postfix expression, get rid of ParenListExprs.
  ExprResult Result = MaybeConvertParenListExprToParenExpr(S, Base);
  if (Result.isInvalid()) return ExprError();
  Base = Result.take();

  Expr *LHSExp = Base, *RHSExp = Idx;

  if (getLangOptions().CPlusPlus &&
      (LHSExp->isTypeDependent() || RHSExp->isTypeDependent())) {
    return Owned(new (Context) ArraySubscriptExpr(LHSExp, RHSExp,
                                                  Context.DependentTy,
                                                  VK_LValue, OK_Ordinary,
                                                  RLoc));
  }

  if (getLangOptions().CPlusPlus &&
      (LHSExp->getType()->isRecordType() ||
       LHSExp->getType()->isEnumeralType() ||
       RHSExp->getType()->isRecordType() ||
       RHSExp->getType()->isEnumeralType())) {
    return CreateOverloadedArraySubscriptExpr(LLoc, RLoc, Base, Idx);
  }

  return CreateBuiltinArraySubscriptExpr(Base, LLoc, Idx, RLoc);
}


ExprResult
Sema::CreateBuiltinArraySubscriptExpr(Expr *Base, SourceLocation LLoc,
                                      Expr *Idx, SourceLocation RLoc) {
  Expr *LHSExp = Base;
  Expr *RHSExp = Idx;

  // Perform default conversions.
  if (!LHSExp->getType()->getAs<VectorType>()) {
    ExprResult Result = DefaultFunctionArrayLvalueConversion(LHSExp);
    if (Result.isInvalid())
      return ExprError();
    LHSExp = Result.take();
  }
  ExprResult Result = DefaultFunctionArrayLvalueConversion(RHSExp);
  if (Result.isInvalid())
    return ExprError();
  RHSExp = Result.take();

  QualType LHSTy = LHSExp->getType(), RHSTy = RHSExp->getType();
  ExprValueKind VK = VK_LValue;
  ExprObjectKind OK = OK_Ordinary;

  // C99 6.5.2.1p2: the expression e1[e2] is by definition precisely equivalent
  // to the expression *((e1)+(e2)). This means the array "Base" may actually be
  // in the subscript position. As a result, we need to derive the array base
  // and index from the expression types.
  Expr *BaseExpr, *IndexExpr;
  QualType ResultType;
  if (LHSTy->isDependentType() || RHSTy->isDependentType()) {
    BaseExpr = LHSExp;
    IndexExpr = RHSExp;
    ResultType = Context.DependentTy;
  } else if (const PointerType *PTy = LHSTy->getAs<PointerType>()) {
    BaseExpr = LHSExp;
    IndexExpr = RHSExp;
    ResultType = PTy->getPointeeType();
  } else if (const PointerType *PTy = RHSTy->getAs<PointerType>()) {
     // Handle the uncommon case of "123[Ptr]".
    BaseExpr = RHSExp;
    IndexExpr = LHSExp;
    ResultType = PTy->getPointeeType();
  } else if (const ObjCObjectPointerType *PTy =
               LHSTy->getAs<ObjCObjectPointerType>()) {
    BaseExpr = LHSExp;
    IndexExpr = RHSExp;
    ResultType = PTy->getPointeeType();
  } else if (const ObjCObjectPointerType *PTy =
               RHSTy->getAs<ObjCObjectPointerType>()) {
     // Handle the uncommon case of "123[Ptr]".
    BaseExpr = RHSExp;
    IndexExpr = LHSExp;
    ResultType = PTy->getPointeeType();
  } else if (const VectorType *VTy = LHSTy->getAs<VectorType>()) {
    BaseExpr = LHSExp;    // vectors: V[123]
    IndexExpr = RHSExp;
    VK = LHSExp->getValueKind();
    if (VK != VK_RValue)
      OK = OK_VectorComponent;

    // FIXME: need to deal with const...
    ResultType = VTy->getElementType();
  } else if (LHSTy->isArrayType()) {
    // If we see an array that wasn't promoted by
    // DefaultFunctionArrayLvalueConversion, it must be an array that
    // wasn't promoted because of the C90 rule that doesn't
    // allow promoting non-lvalue arrays.  Warn, then
    // force the promotion here.
    Diag(LHSExp->getLocStart(), diag::ext_subscript_non_lvalue) <<
        LHSExp->getSourceRange();
    LHSExp = ImpCastExprToType(LHSExp, Context.getArrayDecayedType(LHSTy),
                               CK_ArrayToPointerDecay).take();
    LHSTy = LHSExp->getType();

    BaseExpr = LHSExp;
    IndexExpr = RHSExp;
    ResultType = LHSTy->getAs<PointerType>()->getPointeeType();
  } else if (RHSTy->isArrayType()) {
    // Same as previous, except for 123[f().a] case
    Diag(RHSExp->getLocStart(), diag::ext_subscript_non_lvalue) <<
        RHSExp->getSourceRange();
    RHSExp = ImpCastExprToType(RHSExp, Context.getArrayDecayedType(RHSTy),
                               CK_ArrayToPointerDecay).take();
    RHSTy = RHSExp->getType();

    BaseExpr = RHSExp;
    IndexExpr = LHSExp;
    ResultType = RHSTy->getAs<PointerType>()->getPointeeType();
  } else {
    return ExprError(Diag(LLoc, diag::err_typecheck_subscript_value)
       << LHSExp->getSourceRange() << RHSExp->getSourceRange());
  }
  // C99 6.5.2.1p1
  if (!IndexExpr->getType()->isIntegerType() && !IndexExpr->isTypeDependent())
    return ExprError(Diag(LLoc, diag::err_typecheck_subscript_not_integer)
                     << IndexExpr->getSourceRange());

  if ((IndexExpr->getType()->isSpecificBuiltinType(BuiltinType::Char_S) ||
       IndexExpr->getType()->isSpecificBuiltinType(BuiltinType::Char_U))
         && !IndexExpr->isTypeDependent())
    Diag(LLoc, diag::warn_subscript_is_char) << IndexExpr->getSourceRange();

  // C99 6.5.2.1p1: "shall have type "pointer to *object* type". Similarly,
  // C++ [expr.sub]p1: The type "T" shall be a completely-defined object
  // type. Note that Functions are not objects, and that (in C99 parlance)
  // incomplete types are not object types.
  if (ResultType->isFunctionType()) {
    Diag(BaseExpr->getLocStart(), diag::err_subscript_function_type)
      << ResultType << BaseExpr->getSourceRange();
    return ExprError();
  }

  if (ResultType->isVoidType() && !getLangOptions().CPlusPlus) {
    // GNU extension: subscripting on pointer to void
    Diag(LLoc, diag::ext_gnu_subscript_void_type)
      << BaseExpr->getSourceRange();

    // C forbids expressions of unqualified void type from being l-values.
    // See IsCForbiddenLValueType.
    if (!ResultType.hasQualifiers()) VK = VK_RValue;
  } else if (!ResultType->isDependentType() &&
      RequireCompleteType(LLoc, ResultType,
                          PDiag(diag::err_subscript_incomplete_type)
                            << BaseExpr->getSourceRange()))
    return ExprError();

  // Diagnose bad cases where we step over interface counts.
  if (ResultType->isObjCObjectType() && LangOpts.ObjCNonFragileABI) {
    Diag(LLoc, diag::err_subscript_nonfragile_interface)
      << ResultType << BaseExpr->getSourceRange();
    return ExprError();
  }

  assert(VK == VK_RValue || LangOpts.CPlusPlus ||
         !ResultType.isCForbiddenLValueType());

  return Owned(new (Context) ArraySubscriptExpr(LHSExp, RHSExp,
                                                ResultType, VK, OK, RLoc));
}

ExprResult Sema::BuildCXXDefaultArgExpr(SourceLocation CallLoc,
                                        FunctionDecl *FD,
                                        ParmVarDecl *Param) {
  if (Param->hasUnparsedDefaultArg()) {
    Diag(CallLoc,
         diag::err_use_of_default_argument_to_function_declared_later) <<
      FD << cast<CXXRecordDecl>(FD->getDeclContext())->getDeclName();
    Diag(UnparsedDefaultArgLocs[Param],
         diag::note_default_argument_declared_here);
    return ExprError();
  }
  
  if (Param->hasUninstantiatedDefaultArg()) {
    Expr *UninstExpr = Param->getUninstantiatedDefaultArg();

    // Instantiate the expression.
    MultiLevelTemplateArgumentList ArgList
      = getTemplateInstantiationArgs(FD, 0, /*RelativeToPrimary=*/true);

    std::pair<const TemplateArgument *, unsigned> Innermost
      = ArgList.getInnermost();
    InstantiatingTemplate Inst(*this, CallLoc, Param, Innermost.first,
                               Innermost.second);

    ExprResult Result;
    {
      // C++ [dcl.fct.default]p5:
      //   The names in the [default argument] expression are bound, and
      //   the semantic constraints are checked, at the point where the
      //   default argument expression appears.
      ContextRAII SavedContext(*this, FD);
      Result = SubstExpr(UninstExpr, ArgList);
    }
    if (Result.isInvalid())
      return ExprError();

    // Check the expression as an initializer for the parameter.
    InitializedEntity Entity
      = InitializedEntity::InitializeParameter(Context, Param);
    InitializationKind Kind
      = InitializationKind::CreateCopy(Param->getLocation(),
             /*FIXME:EqualLoc*/UninstExpr->getSourceRange().getBegin());
    Expr *ResultE = Result.takeAs<Expr>();

    InitializationSequence InitSeq(*this, Entity, Kind, &ResultE, 1);
    Result = InitSeq.Perform(*this, Entity, Kind,
                             MultiExprArg(*this, &ResultE, 1));
    if (Result.isInvalid())
      return ExprError();

    // Build the default argument expression.
    return Owned(CXXDefaultArgExpr::Create(Context, CallLoc, Param,
                                           Result.takeAs<Expr>()));
  }

  // If the default expression creates temporaries, we need to
  // push them to the current stack of expression temporaries so they'll
  // be properly destroyed.
  // FIXME: We should really be rebuilding the default argument with new
  // bound temporaries; see the comment in PR5810.
  for (unsigned i = 0, e = Param->getNumDefaultArgTemporaries(); i != e; ++i) {
    CXXTemporary *Temporary = Param->getDefaultArgTemporary(i);
    MarkDeclarationReferenced(Param->getDefaultArg()->getLocStart(), 
                    const_cast<CXXDestructorDecl*>(Temporary->getDestructor()));
    ExprTemporaries.push_back(Temporary);
    ExprNeedsCleanups = true;
  }

  // We already type-checked the argument, so we know it works. 
  // Just mark all of the declarations in this potentially-evaluated expression
  // as being "referenced".
  MarkDeclarationsReferencedInExpr(Param->getDefaultArg());
  return Owned(CXXDefaultArgExpr::Create(Context, CallLoc, Param));
}

/// ConvertArgumentsForCall - Converts the arguments specified in
/// Args/NumArgs to the parameter types of the function FDecl with
/// function prototype Proto. Call is the call expression itself, and
/// Fn is the function expression. For a C++ member function, this
/// routine does not attempt to convert the object argument. Returns
/// true if the call is ill-formed.
bool
Sema::ConvertArgumentsForCall(CallExpr *Call, Expr *Fn,
                              FunctionDecl *FDecl,
                              const FunctionProtoType *Proto,
                              Expr **Args, unsigned NumArgs,
                              SourceLocation RParenLoc,
                              bool IsExecConfig) {
  // Bail out early if calling a builtin with custom typechecking.
  // We don't need to do this in the 
  if (FDecl)
    if (unsigned ID = FDecl->getBuiltinID())
      if (Context.BuiltinInfo.hasCustomTypechecking(ID))
        return false;

  // C99 6.5.2.2p7 - the arguments are implicitly converted, as if by
  // assignment, to the types of the corresponding parameter, ...
  unsigned NumArgsInProto = Proto->getNumArgs();
  bool Invalid = false;
  unsigned MinArgs = FDecl ? FDecl->getMinRequiredArguments() : NumArgsInProto;
  unsigned FnKind = Fn->getType()->isBlockPointerType()
                       ? 1 /* block */
                       : (IsExecConfig ? 3 /* kernel function (exec config) */
                                       : 0 /* function */);

  // If too few arguments are available (and we don't have default
  // arguments for the remaining parameters), don't make the call.
  if (NumArgs < NumArgsInProto) {
    if (NumArgs < MinArgs) {
      Diag(RParenLoc, MinArgs == NumArgsInProto
                        ? diag::err_typecheck_call_too_few_args
                        : diag::err_typecheck_call_too_few_args_at_least)
        << FnKind
        << MinArgs << NumArgs << Fn->getSourceRange();

      // Emit the location of the prototype.
      if (FDecl && !FDecl->getBuiltinID() && !IsExecConfig)
        Diag(FDecl->getLocStart(), diag::note_callee_decl)
          << FDecl;

      return true;
    }
    Call->setNumArgs(Context, NumArgsInProto);
  }

  // If too many are passed and not variadic, error on the extras and drop
  // them.
  if (NumArgs > NumArgsInProto) {
    if (!Proto->isVariadic()) {
      Diag(Args[NumArgsInProto]->getLocStart(),
           MinArgs == NumArgsInProto
             ? diag::err_typecheck_call_too_many_args
             : diag::err_typecheck_call_too_many_args_at_most)
        << FnKind
        << NumArgsInProto << NumArgs << Fn->getSourceRange()
        << SourceRange(Args[NumArgsInProto]->getLocStart(),
                       Args[NumArgs-1]->getLocEnd());

      // Emit the location of the prototype.
      if (FDecl && !FDecl->getBuiltinID() && !IsExecConfig)
        Diag(FDecl->getLocStart(), diag::note_callee_decl)
          << FDecl;
      
      // This deletes the extra arguments.
      Call->setNumArgs(Context, NumArgsInProto);
      return true;
    }
  }
  SmallVector<Expr *, 8> AllArgs;
  VariadicCallType CallType =
    Proto->isVariadic() ? VariadicFunction : VariadicDoesNotApply;
  if (Fn->getType()->isBlockPointerType())
    CallType = VariadicBlock; // Block
  else if (isa<MemberExpr>(Fn))
    CallType = VariadicMethod;
  Invalid = GatherArgumentsForCall(Call->getSourceRange().getBegin(), FDecl,
                                   Proto, 0, Args, NumArgs, AllArgs, CallType);
  if (Invalid)
    return true;
  unsigned TotalNumArgs = AllArgs.size();
  for (unsigned i = 0; i < TotalNumArgs; ++i)
    Call->setArg(i, AllArgs[i]);

  return false;
}

bool Sema::GatherArgumentsForCall(SourceLocation CallLoc,
                                  FunctionDecl *FDecl,
                                  const FunctionProtoType *Proto,
                                  unsigned FirstProtoArg,
                                  Expr **Args, unsigned NumArgs,
                                  SmallVector<Expr *, 8> &AllArgs,
                                  VariadicCallType CallType) {
  unsigned NumArgsInProto = Proto->getNumArgs();
  unsigned NumArgsToCheck = NumArgs;
  bool Invalid = false;
  if (NumArgs != NumArgsInProto)
    // Use default arguments for missing arguments
    NumArgsToCheck = NumArgsInProto;
  unsigned ArgIx = 0;
  // Continue to check argument types (even if we have too few/many args).
  for (unsigned i = FirstProtoArg; i != NumArgsToCheck; i++) {
    QualType ProtoArgType = Proto->getArgType(i);

    Expr *Arg;
    if (ArgIx < NumArgs) {
      Arg = Args[ArgIx++];

      if (RequireCompleteType(Arg->getSourceRange().getBegin(),
                              ProtoArgType,
                              PDiag(diag::err_call_incomplete_argument)
                              << Arg->getSourceRange()))
        return true;

      // Pass the argument
      ParmVarDecl *Param = 0;
      if (FDecl && i < FDecl->getNumParams())
        Param = FDecl->getParamDecl(i);

      InitializedEntity Entity =
        Param? InitializedEntity::InitializeParameter(Context, Param)
             : InitializedEntity::InitializeParameter(Context, ProtoArgType,
                                                      Proto->isArgConsumed(i));
      ExprResult ArgE = PerformCopyInitialization(Entity,
                                                  SourceLocation(),
                                                  Owned(Arg));
      if (ArgE.isInvalid())
        return true;

      Arg = ArgE.takeAs<Expr>();
    } else {
      ParmVarDecl *Param = FDecl->getParamDecl(i);

      ExprResult ArgExpr =
        BuildCXXDefaultArgExpr(CallLoc, FDecl, Param);
      if (ArgExpr.isInvalid())
        return true;

      Arg = ArgExpr.takeAs<Expr>();
    }

    // Check for array bounds violations for each argument to the call. This
    // check only triggers warnings when the argument isn't a more complex Expr
    // with its own checking, such as a BinaryOperator.
    CheckArrayAccess(Arg);

    AllArgs.push_back(Arg);
  }

  // If this is a variadic call, handle args passed through "...".
  if (CallType != VariadicDoesNotApply) {

    // Assume that extern "C" functions with variadic arguments that
    // return __unknown_anytype aren't *really* variadic.
    if (Proto->getResultType() == Context.UnknownAnyTy &&
        FDecl && FDecl->isExternC()) {
      for (unsigned i = ArgIx; i != NumArgs; ++i) {
        ExprResult arg;
        if (isa<ExplicitCastExpr>(Args[i]->IgnoreParens()))
          arg = DefaultFunctionArrayLvalueConversion(Args[i]);
        else
          arg = DefaultVariadicArgumentPromotion(Args[i], CallType, FDecl);
        Invalid |= arg.isInvalid();
        AllArgs.push_back(arg.take());
      }

    // Otherwise do argument promotion, (C99 6.5.2.2p7).
    } else {
      for (unsigned i = ArgIx; i != NumArgs; ++i) {
        ExprResult Arg = DefaultVariadicArgumentPromotion(Args[i], CallType,
                                                          FDecl);
        Invalid |= Arg.isInvalid();
        AllArgs.push_back(Arg.take());
      }
    }

    // Check for array bounds violations.
    for (unsigned i = ArgIx; i != NumArgs; ++i)
      CheckArrayAccess(Args[i]);
  }
  return Invalid;
}

/// Given a function expression of unknown-any type, try to rebuild it
/// to have a function type.
static ExprResult rebuildUnknownAnyFunction(Sema &S, Expr *fn);

/// ActOnCallExpr - Handle a call to Fn with the specified array of arguments.
/// This provides the location of the left/right parens and a list of comma
/// locations.
ExprResult
Sema::ActOnCallExpr(Scope *S, Expr *Fn, SourceLocation LParenLoc,
                    MultiExprArg ArgExprs, SourceLocation RParenLoc,
                    Expr *ExecConfig, bool IsExecConfig) {
  unsigned NumArgs = ArgExprs.size();

  // Since this might be a postfix expression, get rid of ParenListExprs.
  ExprResult Result = MaybeConvertParenListExprToParenExpr(S, Fn);
  if (Result.isInvalid()) return ExprError();
  Fn = Result.take();

  Expr **Args = ArgExprs.release();

  if (getLangOptions().CPlusPlus) {
    // If this is a pseudo-destructor expression, build the call immediately.
    if (isa<CXXPseudoDestructorExpr>(Fn)) {
      if (NumArgs > 0) {
        // Pseudo-destructor calls should not have any arguments.
        Diag(Fn->getLocStart(), diag::err_pseudo_dtor_call_with_args)
          << FixItHint::CreateRemoval(
                                    SourceRange(Args[0]->getLocStart(),
                                                Args[NumArgs-1]->getLocEnd()));

        NumArgs = 0;
      }

      return Owned(new (Context) CallExpr(Context, Fn, 0, 0, Context.VoidTy,
                                          VK_RValue, RParenLoc));
    }

    // Determine whether this is a dependent call inside a C++ template,
    // in which case we won't do any semantic analysis now.
    // FIXME: Will need to cache the results of name lookup (including ADL) in
    // Fn.
    bool Dependent = false;
    if (Fn->isTypeDependent())
      Dependent = true;
    else if (Expr::hasAnyTypeDependentArguments(Args, NumArgs))
      Dependent = true;

    if (Dependent) {
      if (ExecConfig) {
        return Owned(new (Context) CUDAKernelCallExpr(
            Context, Fn, cast<CallExpr>(ExecConfig), Args, NumArgs,
            Context.DependentTy, VK_RValue, RParenLoc));
      } else {
        return Owned(new (Context) CallExpr(Context, Fn, Args, NumArgs,
                                            Context.DependentTy, VK_RValue,
                                            RParenLoc));
      }
    }

    // Determine whether this is a call to an object (C++ [over.call.object]).
    if (Fn->getType()->isRecordType())
      return Owned(BuildCallToObjectOfClassType(S, Fn, LParenLoc, Args, NumArgs,
                                                RParenLoc));

    if (Fn->getType() == Context.UnknownAnyTy) {
      ExprResult result = rebuildUnknownAnyFunction(*this, Fn);
      if (result.isInvalid()) return ExprError();
      Fn = result.take();
    }

    if (Fn->getType() == Context.BoundMemberTy) {
      return BuildCallToMemberFunction(S, Fn, LParenLoc, Args, NumArgs,
                                       RParenLoc);
    }
  }

  // Check for overloaded calls.  This can happen even in C due to extensions.
  if (Fn->getType() == Context.OverloadTy) {
    OverloadExpr::FindResult find = OverloadExpr::find(Fn);

    // We aren't supposed to apply this logic for if there's an '&' involved.
    if (!find.HasFormOfMemberPointer) {
      OverloadExpr *ovl = find.Expression;
      if (isa<UnresolvedLookupExpr>(ovl)) {
        UnresolvedLookupExpr *ULE = cast<UnresolvedLookupExpr>(ovl);
        return BuildOverloadedCallExpr(S, Fn, ULE, LParenLoc, Args, NumArgs,
                                       RParenLoc, ExecConfig);
      } else {
        return BuildCallToMemberFunction(S, Fn, LParenLoc, Args, NumArgs,
                                         RParenLoc);
      }
    }
  }

  // If we're directly calling a function, get the appropriate declaration.

  Expr *NakedFn = Fn->IgnoreParens();

  NamedDecl *NDecl = 0;
  if (UnaryOperator *UnOp = dyn_cast<UnaryOperator>(NakedFn))
    if (UnOp->getOpcode() == UO_AddrOf)
      NakedFn = UnOp->getSubExpr()->IgnoreParens();
  
  if (isa<DeclRefExpr>(NakedFn))
    NDecl = cast<DeclRefExpr>(NakedFn)->getDecl();
  else if (isa<MemberExpr>(NakedFn))
    NDecl = cast<MemberExpr>(NakedFn)->getMemberDecl();

  return BuildResolvedCallExpr(Fn, NDecl, LParenLoc, Args, NumArgs, RParenLoc,
                               ExecConfig, IsExecConfig);
}

ExprResult
Sema::ActOnCUDAExecConfigExpr(Scope *S, SourceLocation LLLLoc,
                              MultiExprArg ExecConfig, SourceLocation GGGLoc) {
  FunctionDecl *ConfigDecl = Context.getcudaConfigureCallDecl();
  if (!ConfigDecl)
    return ExprError(Diag(LLLLoc, diag::err_undeclared_var_use)
                          << "cudaConfigureCall");
  QualType ConfigQTy = ConfigDecl->getType();

  DeclRefExpr *ConfigDR = new (Context) DeclRefExpr(
      ConfigDecl, ConfigQTy, VK_LValue, LLLLoc);

  return ActOnCallExpr(S, ConfigDR, LLLLoc, ExecConfig, GGGLoc, 0,
                       /*IsExecConfig=*/true);
}

/// ActOnAsTypeExpr - create a new asType (bitcast) from the arguments.
///
/// __builtin_astype( value, dst type )
///
ExprResult Sema::ActOnAsTypeExpr(Expr *E, ParsedType ParsedDestTy,
                                 SourceLocation BuiltinLoc,
                                 SourceLocation RParenLoc) {
  ExprValueKind VK = VK_RValue;
  ExprObjectKind OK = OK_Ordinary;
  QualType DstTy = GetTypeFromParser(ParsedDestTy);
  QualType SrcTy = E->getType();
  if (Context.getTypeSize(DstTy) != Context.getTypeSize(SrcTy))
    return ExprError(Diag(BuiltinLoc,
                          diag::err_invalid_astype_of_different_size)
                     << DstTy
                     << SrcTy
                     << E->getSourceRange());
  return Owned(new (Context) AsTypeExpr(E, DstTy, VK, OK, BuiltinLoc,
               RParenLoc));
}

/// BuildResolvedCallExpr - Build a call to a resolved expression,
/// i.e. an expression not of \p OverloadTy.  The expression should
/// unary-convert to an expression of function-pointer or
/// block-pointer type.
///
/// \param NDecl the declaration being called, if available
ExprResult
Sema::BuildResolvedCallExpr(Expr *Fn, NamedDecl *NDecl,
                            SourceLocation LParenLoc,
                            Expr **Args, unsigned NumArgs,
                            SourceLocation RParenLoc,
                            Expr *Config, bool IsExecConfig) {
  FunctionDecl *FDecl = dyn_cast_or_null<FunctionDecl>(NDecl);

  // Promote the function operand.
  ExprResult Result = UsualUnaryConversions(Fn);
  if (Result.isInvalid())
    return ExprError();
  Fn = Result.take();

  // Make the call expr early, before semantic checks.  This guarantees cleanup
  // of arguments and function on error.
  CallExpr *TheCall;
  if (Config) {
    TheCall = new (Context) CUDAKernelCallExpr(Context, Fn,
                                               cast<CallExpr>(Config),
                                               Args, NumArgs,
                                               Context.BoolTy,
                                               VK_RValue,
                                               RParenLoc);
  } else {
    TheCall = new (Context) CallExpr(Context, Fn,
                                     Args, NumArgs,
                                     Context.BoolTy,
                                     VK_RValue,
                                     RParenLoc);
  }

  unsigned BuiltinID = (FDecl ? FDecl->getBuiltinID() : 0);

  // Bail out early if calling a builtin with custom typechecking.
  if (BuiltinID && Context.BuiltinInfo.hasCustomTypechecking(BuiltinID))
    return CheckBuiltinFunctionCall(BuiltinID, TheCall);

 retry:
  const FunctionType *FuncT;
  if (const PointerType *PT = Fn->getType()->getAs<PointerType>()) {
    // C99 6.5.2.2p1 - "The expression that denotes the called function shall
    // have type pointer to function".
    FuncT = PT->getPointeeType()->getAs<FunctionType>();
    if (FuncT == 0)
      return ExprError(Diag(LParenLoc, diag::err_typecheck_call_not_function)
                         << Fn->getType() << Fn->getSourceRange());
  } else if (const BlockPointerType *BPT =
               Fn->getType()->getAs<BlockPointerType>()) {
    FuncT = BPT->getPointeeType()->castAs<FunctionType>();
  } else {
    // Handle calls to expressions of unknown-any type.
    if (Fn->getType() == Context.UnknownAnyTy) {
      ExprResult rewrite = rebuildUnknownAnyFunction(*this, Fn);
      if (rewrite.isInvalid()) return ExprError();
      Fn = rewrite.take();
      TheCall->setCallee(Fn);
      goto retry;
    }

    return ExprError(Diag(LParenLoc, diag::err_typecheck_call_not_function)
      << Fn->getType() << Fn->getSourceRange());
  }

  if (getLangOptions().CUDA) {
    if (Config) {
      // CUDA: Kernel calls must be to global functions
      if (FDecl && !FDecl->hasAttr<CUDAGlobalAttr>())
        return ExprError(Diag(LParenLoc,diag::err_kern_call_not_global_function)
            << FDecl->getName() << Fn->getSourceRange());

      // CUDA: Kernel function must have 'void' return type
      if (!FuncT->getResultType()->isVoidType())
        return ExprError(Diag(LParenLoc, diag::err_kern_type_not_void_return)
            << Fn->getType() << Fn->getSourceRange());
    } else {
      // CUDA: Calls to global functions must be configured
      if (FDecl && FDecl->hasAttr<CUDAGlobalAttr>())
        return ExprError(Diag(LParenLoc, diag::err_global_call_not_config)
            << FDecl->getName() << Fn->getSourceRange());
    }
  }

  // Check for a valid return type
  if (CheckCallReturnType(FuncT->getResultType(),
                          Fn->getSourceRange().getBegin(), TheCall,
                          FDecl))
    return ExprError();

  // We know the result type of the call, set it.
  TheCall->setType(FuncT->getCallResultType(Context));
  TheCall->setValueKind(Expr::getValueKindForType(FuncT->getResultType()));

  if (const FunctionProtoType *Proto = dyn_cast<FunctionProtoType>(FuncT)) {
    if (ConvertArgumentsForCall(TheCall, Fn, FDecl, Proto, Args, NumArgs,
                                RParenLoc, IsExecConfig))
      return ExprError();
  } else {
    assert(isa<FunctionNoProtoType>(FuncT) && "Unknown FunctionType!");

    if (FDecl) {
      // Check if we have too few/too many template arguments, based
      // on our knowledge of the function definition.
      const FunctionDecl *Def = 0;
      if (FDecl->hasBody(Def) && NumArgs != Def->param_size()) {
        const FunctionProtoType *Proto 
          = Def->getType()->getAs<FunctionProtoType>();
        if (!Proto || !(Proto->isVariadic() && NumArgs >= Def->param_size()))
          Diag(RParenLoc, diag::warn_call_wrong_number_of_arguments)
            << (NumArgs > Def->param_size()) << FDecl << Fn->getSourceRange();
      }
      
      // If the function we're calling isn't a function prototype, but we have
      // a function prototype from a prior declaratiom, use that prototype.
      if (!FDecl->hasPrototype())
        Proto = FDecl->getType()->getAs<FunctionProtoType>();
    }

    // Promote the arguments (C99 6.5.2.2p6).
    for (unsigned i = 0; i != NumArgs; i++) {
      Expr *Arg = Args[i];

      if (Proto && i < Proto->getNumArgs()) {
        InitializedEntity Entity
          = InitializedEntity::InitializeParameter(Context, 
                                                   Proto->getArgType(i),
                                                   Proto->isArgConsumed(i));
        ExprResult ArgE = PerformCopyInitialization(Entity,
                                                    SourceLocation(),
                                                    Owned(Arg));
        if (ArgE.isInvalid())
          return true;
        
        Arg = ArgE.takeAs<Expr>();

      } else {
        ExprResult ArgE = DefaultArgumentPromotion(Arg);

        if (ArgE.isInvalid())
          return true;

        Arg = ArgE.takeAs<Expr>();
      }
      
      if (RequireCompleteType(Arg->getSourceRange().getBegin(),
                              Arg->getType(),
                              PDiag(diag::err_call_incomplete_argument)
                                << Arg->getSourceRange()))
        return ExprError();

      TheCall->setArg(i, Arg);
    }
  }

  if (CXXMethodDecl *Method = dyn_cast_or_null<CXXMethodDecl>(FDecl))
    if (!Method->isStatic())
      return ExprError(Diag(LParenLoc, diag::err_member_call_without_object)
        << Fn->getSourceRange());

  // Check for sentinels
  if (NDecl)
    DiagnoseSentinelCalls(NDecl, LParenLoc, Args, NumArgs);

  // Do special checking on direct calls to functions.
  if (FDecl) {
    if (CheckFunctionCall(FDecl, TheCall))
      return ExprError();

    if (BuiltinID)
      return CheckBuiltinFunctionCall(BuiltinID, TheCall);
  } else if (NDecl) {
    if (CheckBlockCall(NDecl, TheCall))
      return ExprError();
  }

  return MaybeBindToTemporary(TheCall);
}

ExprResult
Sema::ActOnCompoundLiteral(SourceLocation LParenLoc, ParsedType Ty,
                           SourceLocation RParenLoc, Expr *InitExpr) {
  assert((Ty != 0) && "ActOnCompoundLiteral(): missing type");
  // FIXME: put back this assert when initializers are worked out.
  //assert((InitExpr != 0) && "ActOnCompoundLiteral(): missing expression");

  TypeSourceInfo *TInfo;
  QualType literalType = GetTypeFromParser(Ty, &TInfo);
  if (!TInfo)
    TInfo = Context.getTrivialTypeSourceInfo(literalType);

  return BuildCompoundLiteralExpr(LParenLoc, TInfo, RParenLoc, InitExpr);
}

ExprResult
Sema::BuildCompoundLiteralExpr(SourceLocation LParenLoc, TypeSourceInfo *TInfo,
                               SourceLocation RParenLoc, Expr *LiteralExpr) {
  QualType literalType = TInfo->getType();

  if (literalType->isArrayType()) {
    if (RequireCompleteType(LParenLoc, Context.getBaseElementType(literalType),
             PDiag(diag::err_illegal_decl_array_incomplete_type)
               << SourceRange(LParenLoc,
                              LiteralExpr->getSourceRange().getEnd())))
      return ExprError();
    if (literalType->isVariableArrayType())
      return ExprError(Diag(LParenLoc, diag::err_variable_object_no_init)
        << SourceRange(LParenLoc, LiteralExpr->getSourceRange().getEnd()));
  } else if (!literalType->isDependentType() &&
             RequireCompleteType(LParenLoc, literalType,
                      PDiag(diag::err_typecheck_decl_incomplete_type)
                        << SourceRange(LParenLoc,
                                       LiteralExpr->getSourceRange().getEnd())))
    return ExprError();

  InitializedEntity Entity
    = InitializedEntity::InitializeTemporary(literalType);
  InitializationKind Kind
    = InitializationKind::CreateCStyleCast(LParenLoc, 
                                           SourceRange(LParenLoc, RParenLoc));
  InitializationSequence InitSeq(*this, Entity, Kind, &LiteralExpr, 1);
  ExprResult Result = InitSeq.Perform(*this, Entity, Kind,
                                       MultiExprArg(*this, &LiteralExpr, 1),
                                            &literalType);
  if (Result.isInvalid())
    return ExprError();
  LiteralExpr = Result.get();

  bool isFileScope = getCurFunctionOrMethodDecl() == 0;
  if (isFileScope) { // 6.5.2.5p3
    if (CheckForConstantInitializer(LiteralExpr, literalType))
      return ExprError();
  }

  // In C, compound literals are l-values for some reason.
  ExprValueKind VK = getLangOptions().CPlusPlus ? VK_RValue : VK_LValue;

  return MaybeBindToTemporary(
           new (Context) CompoundLiteralExpr(LParenLoc, TInfo, literalType,
                                             VK, LiteralExpr, isFileScope));
}

ExprResult
Sema::ActOnInitList(SourceLocation LBraceLoc, MultiExprArg InitArgList,
                    SourceLocation RBraceLoc) {
  unsigned NumInit = InitArgList.size();
  Expr **InitList = InitArgList.release();

  // Semantic analysis for initializers is done by ActOnDeclarator() and
  // CheckInitializer() - it requires knowledge of the object being intialized.

  InitListExpr *E = new (Context) InitListExpr(Context, LBraceLoc, InitList,
                                               NumInit, RBraceLoc);
  E->setType(Context.VoidTy); // FIXME: just a place holder for now.
  return Owned(E);
}

/// Do an explicit extend of the given block pointer if we're in ARC.
static void maybeExtendBlockObject(Sema &S, ExprResult &E) {
  assert(E.get()->getType()->isBlockPointerType());
  assert(E.get()->isRValue());

  // Only do this in an r-value context.
  if (!S.getLangOptions().ObjCAutoRefCount) return;

  E = ImplicitCastExpr::Create(S.Context, E.get()->getType(),
                               CK_ARCExtendBlockObject, E.get(),
                               /*base path*/ 0, VK_RValue);
  S.ExprNeedsCleanups = true;
}

/// Prepare a conversion of the given expression to an ObjC object
/// pointer type.
CastKind Sema::PrepareCastToObjCObjectPointer(ExprResult &E) {
  QualType type = E.get()->getType();
  if (type->isObjCObjectPointerType()) {
    return CK_BitCast;
  } else if (type->isBlockPointerType()) {
    maybeExtendBlockObject(*this, E);
    return CK_BlockPointerToObjCPointerCast;
  } else {
    assert(type->isPointerType());
    return CK_CPointerToObjCPointerCast;
  }
}

/// Prepares for a scalar cast, performing all the necessary stages
/// except the final cast and returning the kind required.
CastKind Sema::PrepareScalarCast(ExprResult &Src, QualType DestTy) {
  // Both Src and Dest are scalar types, i.e. arithmetic or pointer.
  // Also, callers should have filtered out the invalid cases with
  // pointers.  Everything else should be possible.

  QualType SrcTy = Src.get()->getType();
  if (Context.hasSameUnqualifiedType(SrcTy, DestTy))
    return CK_NoOp;

  switch (Type::ScalarTypeKind SrcKind = SrcTy->getScalarTypeKind()) {
  case Type::STK_MemberPointer:
    llvm_unreachable("member pointer type in C");

  case Type::STK_CPointer:
  case Type::STK_BlockPointer:
  case Type::STK_ObjCObjectPointer:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_CPointer:
      return CK_BitCast;
    case Type::STK_BlockPointer:
      return (SrcKind == Type::STK_BlockPointer
                ? CK_BitCast : CK_AnyPointerToBlockPointerCast);
    case Type::STK_ObjCObjectPointer:
      if (SrcKind == Type::STK_ObjCObjectPointer)
        return CK_BitCast;
      else if (SrcKind == Type::STK_CPointer)
        return CK_CPointerToObjCPointerCast;
      else {
        maybeExtendBlockObject(*this, Src);
        return CK_BlockPointerToObjCPointerCast;
      }
    case Type::STK_Bool:
      return CK_PointerToBoolean;
    case Type::STK_Integral:
      return CK_PointerToIntegral;
    case Type::STK_Floating:
    case Type::STK_FloatingComplex:
    case Type::STK_IntegralComplex:
    case Type::STK_MemberPointer:
      llvm_unreachable("illegal cast from pointer");
    }
    break;

  case Type::STK_Bool: // casting from bool is like casting from an integer
  case Type::STK_Integral:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_CPointer:
    case Type::STK_ObjCObjectPointer:
    case Type::STK_BlockPointer:
      if (Src.get()->isNullPointerConstant(Context,
                                           Expr::NPC_ValueDependentIsNull))
        return CK_NullToPointer;
      return CK_IntegralToPointer;
    case Type::STK_Bool:
      return CK_IntegralToBoolean;
    case Type::STK_Integral:
      return CK_IntegralCast;
    case Type::STK_Floating:
      return CK_IntegralToFloating;
    case Type::STK_IntegralComplex:
      Src = ImpCastExprToType(Src.take(),
                              DestTy->castAs<ComplexType>()->getElementType(),
                              CK_IntegralCast);
      return CK_IntegralRealToComplex;
    case Type::STK_FloatingComplex:
      Src = ImpCastExprToType(Src.take(),
                              DestTy->castAs<ComplexType>()->getElementType(),
                              CK_IntegralToFloating);
      return CK_FloatingRealToComplex;
    case Type::STK_MemberPointer:
      llvm_unreachable("member pointer type in C");
    }
    break;

  case Type::STK_Floating:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_Floating:
      return CK_FloatingCast;
    case Type::STK_Bool:
      return CK_FloatingToBoolean;
    case Type::STK_Integral:
      return CK_FloatingToIntegral;
    case Type::STK_FloatingComplex:
      Src = ImpCastExprToType(Src.take(),
                              DestTy->castAs<ComplexType>()->getElementType(),
                              CK_FloatingCast);
      return CK_FloatingRealToComplex;
    case Type::STK_IntegralComplex:
      Src = ImpCastExprToType(Src.take(),
                              DestTy->castAs<ComplexType>()->getElementType(),
                              CK_FloatingToIntegral);
      return CK_IntegralRealToComplex;
    case Type::STK_CPointer:
    case Type::STK_ObjCObjectPointer:
    case Type::STK_BlockPointer:
      llvm_unreachable("valid float->pointer cast?");
    case Type::STK_MemberPointer:
      llvm_unreachable("member pointer type in C");
    }
    break;

  case Type::STK_FloatingComplex:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_FloatingComplex:
      return CK_FloatingComplexCast;
    case Type::STK_IntegralComplex:
      return CK_FloatingComplexToIntegralComplex;
    case Type::STK_Floating: {
      QualType ET = SrcTy->castAs<ComplexType>()->getElementType();
      if (Context.hasSameType(ET, DestTy))
        return CK_FloatingComplexToReal;
      Src = ImpCastExprToType(Src.take(), ET, CK_FloatingComplexToReal);
      return CK_FloatingCast;
    }
    case Type::STK_Bool:
      return CK_FloatingComplexToBoolean;
    case Type::STK_Integral:
      Src = ImpCastExprToType(Src.take(),
                              SrcTy->castAs<ComplexType>()->getElementType(),
                              CK_FloatingComplexToReal);
      return CK_FloatingToIntegral;
    case Type::STK_CPointer:
    case Type::STK_ObjCObjectPointer:
    case Type::STK_BlockPointer:
      llvm_unreachable("valid complex float->pointer cast?");
    case Type::STK_MemberPointer:
      llvm_unreachable("member pointer type in C");
    }
    break;

  case Type::STK_IntegralComplex:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_FloatingComplex:
      return CK_IntegralComplexToFloatingComplex;
    case Type::STK_IntegralComplex:
      return CK_IntegralComplexCast;
    case Type::STK_Integral: {
      QualType ET = SrcTy->castAs<ComplexType>()->getElementType();
      if (Context.hasSameType(ET, DestTy))
        return CK_IntegralComplexToReal;
      Src = ImpCastExprToType(Src.take(), ET, CK_IntegralComplexToReal);
      return CK_IntegralCast;
    }
    case Type::STK_Bool:
      return CK_IntegralComplexToBoolean;
    case Type::STK_Floating:
      Src = ImpCastExprToType(Src.take(),
                              SrcTy->castAs<ComplexType>()->getElementType(),
                              CK_IntegralComplexToReal);
      return CK_IntegralToFloating;
    case Type::STK_CPointer:
    case Type::STK_ObjCObjectPointer:
    case Type::STK_BlockPointer:
      llvm_unreachable("valid complex int->pointer cast?");
    case Type::STK_MemberPointer:
      llvm_unreachable("member pointer type in C");
    }
    break;
  }

  llvm_unreachable("Unhandled scalar cast");
}

bool Sema::CheckVectorCast(SourceRange R, QualType VectorTy, QualType Ty,
                           CastKind &Kind) {
  assert(VectorTy->isVectorType() && "Not a vector type!");

  if (Ty->isVectorType() || Ty->isIntegerType()) {
    if (Context.getTypeSize(VectorTy) != Context.getTypeSize(Ty))
      return Diag(R.getBegin(),
                  Ty->isVectorType() ?
                  diag::err_invalid_conversion_between_vectors :
                  diag::err_invalid_conversion_between_vector_and_integer)
        << VectorTy << Ty << R;
  } else
    return Diag(R.getBegin(),
                diag::err_invalid_conversion_between_vector_and_scalar)
      << VectorTy << Ty << R;

  Kind = CK_BitCast;
  return false;
}

ExprResult Sema::CheckExtVectorCast(SourceRange R, QualType DestTy,
                                    Expr *CastExpr, CastKind &Kind) {
  assert(DestTy->isExtVectorType() && "Not an extended vector type!");

  QualType SrcTy = CastExpr->getType();

  // If SrcTy is a VectorType, the total size must match to explicitly cast to
  // an ExtVectorType.
  // In OpenCL, casts between vectors of different types are not allowed.
  // (See OpenCL 6.2).
  if (SrcTy->isVectorType()) {
    if (Context.getTypeSize(DestTy) != Context.getTypeSize(SrcTy)
        || (getLangOptions().OpenCL &&
            (DestTy.getCanonicalType() != SrcTy.getCanonicalType()))) {
      Diag(R.getBegin(),diag::err_invalid_conversion_between_ext_vectors)
        << DestTy << SrcTy << R;
      return ExprError();
    }
    Kind = CK_BitCast;
    return Owned(CastExpr);
  }

  // All non-pointer scalars can be cast to ExtVector type.  The appropriate
  // conversion will take place first from scalar to elt type, and then
  // splat from elt type to vector.
  if (SrcTy->isPointerType())
    return Diag(R.getBegin(),
                diag::err_invalid_conversion_between_vector_and_scalar)
      << DestTy << SrcTy << R;

  QualType DestElemTy = DestTy->getAs<ExtVectorType>()->getElementType();
  ExprResult CastExprRes = Owned(CastExpr);
  CastKind CK = PrepareScalarCast(CastExprRes, DestElemTy);
  if (CastExprRes.isInvalid())
    return ExprError();
  CastExpr = ImpCastExprToType(CastExprRes.take(), DestElemTy, CK).take();

  Kind = CK_VectorSplat;
  return Owned(CastExpr);
}

ExprResult
Sema::ActOnCastExpr(Scope *S, SourceLocation LParenLoc,
                    Declarator &D, ParsedType &Ty,
                    SourceLocation RParenLoc, Expr *CastExpr) {
  assert(!D.isInvalidType() && (CastExpr != 0) &&
         "ActOnCastExpr(): missing type or expr");

  TypeSourceInfo *castTInfo = GetTypeForDeclaratorCast(D, CastExpr->getType());
  if (D.isInvalidType())
    return ExprError();

  if (getLangOptions().CPlusPlus) {
    // Check that there are no default arguments (C++ only).
    CheckExtraCXXDefaultArguments(D);
  }

  checkUnusedDeclAttributes(D);

  QualType castType = castTInfo->getType();
  Ty = CreateParsedType(castType, castTInfo);

  bool isVectorLiteral = false;

  // Check for an altivec or OpenCL literal,
  // i.e. all the elements are integer constants.
  ParenExpr *PE = dyn_cast<ParenExpr>(CastExpr);
  ParenListExpr *PLE = dyn_cast<ParenListExpr>(CastExpr);
  if ((getLangOptions().AltiVec || getLangOptions().OpenCL)
       && castType->isVectorType() && (PE || PLE)) {
    if (PLE && PLE->getNumExprs() == 0) {
      Diag(PLE->getExprLoc(), diag::err_altivec_empty_initializer);
      return ExprError();
    }
    if (PE || PLE->getNumExprs() == 1) {
      Expr *E = (PE ? PE->getSubExpr() : PLE->getExpr(0));
      if (!E->getType()->isVectorType())
        isVectorLiteral = true;
    }
    else
      isVectorLiteral = true;
  }

  // If this is a vector initializer, '(' type ')' '(' init, ..., init ')'
  // then handle it as such.
  if (isVectorLiteral)
    return BuildVectorLiteral(LParenLoc, RParenLoc, CastExpr, castTInfo);

  // If the Expr being casted is a ParenListExpr, handle it specially.
  // This is not an AltiVec-style cast, so turn the ParenListExpr into a
  // sequence of BinOp comma operators.
  if (isa<ParenListExpr>(CastExpr)) {
    ExprResult Result = MaybeConvertParenListExprToParenExpr(S, CastExpr);
    if (Result.isInvalid()) return ExprError();
    CastExpr = Result.take();
  }

  return BuildCStyleCastExpr(LParenLoc, castTInfo, RParenLoc, CastExpr);
}

ExprResult Sema::BuildVectorLiteral(SourceLocation LParenLoc,
                                    SourceLocation RParenLoc, Expr *E,
                                    TypeSourceInfo *TInfo) {
  assert((isa<ParenListExpr>(E) || isa<ParenExpr>(E)) &&
         "Expected paren or paren list expression");

  Expr **exprs;
  unsigned numExprs;
  Expr *subExpr;
  if (ParenListExpr *PE = dyn_cast<ParenListExpr>(E)) {
    exprs = PE->getExprs();
    numExprs = PE->getNumExprs();
  } else {
    subExpr = cast<ParenExpr>(E)->getSubExpr();
    exprs = &subExpr;
    numExprs = 1;
  }

  QualType Ty = TInfo->getType();
  assert(Ty->isVectorType() && "Expected vector type");

  SmallVector<Expr *, 8> initExprs;
  const VectorType *VTy = Ty->getAs<VectorType>();
  unsigned numElems = Ty->getAs<VectorType>()->getNumElements();
  
  // '(...)' form of vector initialization in AltiVec: the number of
  // initializers must be one or must match the size of the vector.
  // If a single value is specified in the initializer then it will be
  // replicated to all the components of the vector
  if (VTy->getVectorKind() == VectorType::AltiVecVector) {
    // The number of initializers must be one or must match the size of the
    // vector. If a single value is specified in the initializer then it will
    // be replicated to all the components of the vector
    if (numExprs == 1) {
      QualType ElemTy = Ty->getAs<VectorType>()->getElementType();
      ExprResult Literal = Owned(exprs[0]);
      Literal = ImpCastExprToType(Literal.take(), ElemTy,
                                  PrepareScalarCast(Literal, ElemTy));
      return BuildCStyleCastExpr(LParenLoc, TInfo, RParenLoc, Literal.take());
    }
    else if (numExprs < numElems) {
      Diag(E->getExprLoc(),
           diag::err_incorrect_number_of_vector_initializers);
      return ExprError();
    }
    else
      for (unsigned i = 0, e = numExprs; i != e; ++i)
        initExprs.push_back(exprs[i]);
  }
  else {
    // For OpenCL, when the number of initializers is a single value,
    // it will be replicated to all components of the vector.
    if (getLangOptions().OpenCL &&
        VTy->getVectorKind() == VectorType::GenericVector &&
        numExprs == 1) {
        QualType ElemTy = Ty->getAs<VectorType>()->getElementType();
        ExprResult Literal = Owned(exprs[0]);
        Literal = ImpCastExprToType(Literal.take(), ElemTy,
                                    PrepareScalarCast(Literal, ElemTy));
        return BuildCStyleCastExpr(LParenLoc, TInfo, RParenLoc, Literal.take());
    }
    
    for (unsigned i = 0, e = numExprs; i != e; ++i)
      initExprs.push_back(exprs[i]);
  }
  // FIXME: This means that pretty-printing the final AST will produce curly
  // braces instead of the original commas.
  InitListExpr *initE = new (Context) InitListExpr(Context, LParenLoc,
                                                   &initExprs[0],
                                                   initExprs.size(), RParenLoc);
  initE->setType(Ty);
  return BuildCompoundLiteralExpr(LParenLoc, TInfo, RParenLoc, initE);
}

/// This is not an AltiVec-style cast, so turn the ParenListExpr into a sequence
/// of comma binary operators.
ExprResult
Sema::MaybeConvertParenListExprToParenExpr(Scope *S, Expr *OrigExpr) {
  ParenListExpr *E = dyn_cast<ParenListExpr>(OrigExpr);
  if (!E)
    return Owned(OrigExpr);

  ExprResult Result(E->getExpr(0));

  for (unsigned i = 1, e = E->getNumExprs(); i != e && !Result.isInvalid(); ++i)
    Result = ActOnBinOp(S, E->getExprLoc(), tok::comma, Result.get(),
                        E->getExpr(i));

  if (Result.isInvalid()) return ExprError();

  return ActOnParenExpr(E->getLParenLoc(), E->getRParenLoc(), Result.get());
}

ExprResult Sema::ActOnParenOrParenListExpr(SourceLocation L,
                                           SourceLocation R,
                                           MultiExprArg Val) {
  unsigned nexprs = Val.size();
  Expr **exprs = reinterpret_cast<Expr**>(Val.release());
  assert((exprs != 0) && "ActOnParenOrParenListExpr() missing expr list");
  Expr *expr;
  if (nexprs == 1)
    expr = new (Context) ParenExpr(L, R, exprs[0]);
  else
    expr = new (Context) ParenListExpr(Context, L, exprs, nexprs, R,
                                       exprs[nexprs-1]->getType());
  return Owned(expr);
}

/// \brief Emit a specialized diagnostic when one expression is a null pointer
/// constant and the other is not a pointer.  Returns true if a diagnostic is
/// emitted.
bool Sema::DiagnoseConditionalForNull(Expr *LHSExpr, Expr *RHSExpr,
                                      SourceLocation QuestionLoc) {
  Expr *NullExpr = LHSExpr;
  Expr *NonPointerExpr = RHSExpr;
  Expr::NullPointerConstantKind NullKind =
      NullExpr->isNullPointerConstant(Context,
                                      Expr::NPC_ValueDependentIsNotNull);

  if (NullKind == Expr::NPCK_NotNull) {
    NullExpr = RHSExpr;
    NonPointerExpr = LHSExpr;
    NullKind =
        NullExpr->isNullPointerConstant(Context,
                                        Expr::NPC_ValueDependentIsNotNull);
  }

  if (NullKind == Expr::NPCK_NotNull)
    return false;

  if (NullKind == Expr::NPCK_ZeroInteger) {
    // In this case, check to make sure that we got here from a "NULL"
    // string in the source code.
    NullExpr = NullExpr->IgnoreParenImpCasts();
    SourceLocation loc = NullExpr->getExprLoc();
    if (!findMacroSpelling(loc, "NULL"))
      return false;
  }

  int DiagType = (NullKind == Expr::NPCK_CXX0X_nullptr);
  Diag(QuestionLoc, diag::err_typecheck_cond_incompatible_operands_null)
      << NonPointerExpr->getType() << DiagType
      << NonPointerExpr->getSourceRange();
  return true;
}

/// \brief Return false if the condition expression is valid, true otherwise.
static bool checkCondition(Sema &S, Expr *Cond) {
  QualType CondTy = Cond->getType();

  // C99 6.5.15p2
  if (CondTy->isScalarType()) return false;

  // OpenCL: Sec 6.3.i says the condition is allowed to be a vector or scalar.
  if (S.getLangOptions().OpenCL && CondTy->isVectorType())
    return false;

  // Emit the proper error message.
  S.Diag(Cond->getLocStart(), S.getLangOptions().OpenCL ?
                              diag::err_typecheck_cond_expect_scalar :
                              diag::err_typecheck_cond_expect_scalar_or_vector)
    << CondTy;
  return true;
}

/// \brief Return false if the two expressions can be converted to a vector,
/// true otherwise
static bool checkConditionalConvertScalarsToVectors(Sema &S, ExprResult &LHS,
                                                    ExprResult &RHS,
                                                    QualType CondTy) {
  // Both operands should be of scalar type.
  if (!LHS.get()->getType()->isScalarType()) {
    S.Diag(LHS.get()->getLocStart(), diag::err_typecheck_cond_expect_scalar)
      << CondTy;
    return true;
  }
  if (!RHS.get()->getType()->isScalarType()) {
    S.Diag(RHS.get()->getLocStart(), diag::err_typecheck_cond_expect_scalar)
      << CondTy;
    return true;
  }

  // Implicity convert these scalars to the type of the condition.
  LHS = S.ImpCastExprToType(LHS.take(), CondTy, CK_IntegralCast);
  RHS = S.ImpCastExprToType(RHS.take(), CondTy, CK_IntegralCast);
  return false;
}

/// \brief Handle when one or both operands are void type.
static QualType checkConditionalVoidType(Sema &S, ExprResult &LHS,
                                         ExprResult &RHS) {
    Expr *LHSExpr = LHS.get();
    Expr *RHSExpr = RHS.get();

    if (!LHSExpr->getType()->isVoidType())
      S.Diag(RHSExpr->getLocStart(), diag::ext_typecheck_cond_one_void)
        << RHSExpr->getSourceRange();
    if (!RHSExpr->getType()->isVoidType())
      S.Diag(LHSExpr->getLocStart(), diag::ext_typecheck_cond_one_void)
        << LHSExpr->getSourceRange();
    LHS = S.ImpCastExprToType(LHS.take(), S.Context.VoidTy, CK_ToVoid);
    RHS = S.ImpCastExprToType(RHS.take(), S.Context.VoidTy, CK_ToVoid);
    return S.Context.VoidTy;
}

/// \brief Return false if the NullExpr can be promoted to PointerTy,
/// true otherwise.
static bool checkConditionalNullPointer(Sema &S, ExprResult &NullExpr,
                                        QualType PointerTy) {
  if ((!PointerTy->isAnyPointerType() && !PointerTy->isBlockPointerType()) ||
      !NullExpr.get()->isNullPointerConstant(S.Context,
                                            Expr::NPC_ValueDependentIsNull))
    return true;

  NullExpr = S.ImpCastExprToType(NullExpr.take(), PointerTy, CK_NullToPointer);
  return false;
}

/// \brief Checks compatibility between two pointers and return the resulting
/// type.
static QualType checkConditionalPointerCompatibility(Sema &S, ExprResult &LHS,
                                                     ExprResult &RHS,
                                                     SourceLocation Loc) {
  QualType LHSTy = LHS.get()->getType();
  QualType RHSTy = RHS.get()->getType();

  if (S.Context.hasSameType(LHSTy, RHSTy)) {
    // Two identical pointers types are always compatible.
    return LHSTy;
  }

  QualType lhptee, rhptee;

  // Get the pointee types.
  if (const BlockPointerType *LHSBTy = LHSTy->getAs<BlockPointerType>()) {
    lhptee = LHSBTy->getPointeeType();
    rhptee = RHSTy->castAs<BlockPointerType>()->getPointeeType();
  } else {
    lhptee = LHSTy->castAs<PointerType>()->getPointeeType();
    rhptee = RHSTy->castAs<PointerType>()->getPointeeType();
  }

  if (!S.Context.typesAreCompatible(lhptee.getUnqualifiedType(),
                                    rhptee.getUnqualifiedType())) {
    S.Diag(Loc, diag::warn_typecheck_cond_incompatible_pointers)
      << LHSTy << RHSTy << LHS.get()->getSourceRange()
      << RHS.get()->getSourceRange();
    // In this situation, we assume void* type. No especially good
    // reason, but this is what gcc does, and we do have to pick
    // to get a consistent AST.
    QualType incompatTy = S.Context.getPointerType(S.Context.VoidTy);
    LHS = S.ImpCastExprToType(LHS.take(), incompatTy, CK_BitCast);
    RHS = S.ImpCastExprToType(RHS.take(), incompatTy, CK_BitCast);
    return incompatTy;
  }

  // The pointer types are compatible.
  // C99 6.5.15p6: If both operands are pointers to compatible types *or* to
  // differently qualified versions of compatible types, the result type is
  // a pointer to an appropriately qualified version of the *composite*
  // type.
  // FIXME: Need to calculate the composite type.
  // FIXME: Need to add qualifiers

  LHS = S.ImpCastExprToType(LHS.take(), LHSTy, CK_BitCast);
  RHS = S.ImpCastExprToType(RHS.take(), LHSTy, CK_BitCast);
  return LHSTy;
}

/// \brief Return the resulting type when the operands are both block pointers.
static QualType checkConditionalBlockPointerCompatibility(Sema &S,
                                                          ExprResult &LHS,
                                                          ExprResult &RHS,
                                                          SourceLocation Loc) {
  QualType LHSTy = LHS.get()->getType();
  QualType RHSTy = RHS.get()->getType();

  if (!LHSTy->isBlockPointerType() || !RHSTy->isBlockPointerType()) {
    if (LHSTy->isVoidPointerType() || RHSTy->isVoidPointerType()) {
      QualType destType = S.Context.getPointerType(S.Context.VoidTy);
      LHS = S.ImpCastExprToType(LHS.take(), destType, CK_BitCast);
      RHS = S.ImpCastExprToType(RHS.take(), destType, CK_BitCast);
      return destType;
    }
    S.Diag(Loc, diag::err_typecheck_cond_incompatible_operands)
      << LHSTy << RHSTy << LHS.get()->getSourceRange()
      << RHS.get()->getSourceRange();
    return QualType();
  }

  // We have 2 block pointer types.
  return checkConditionalPointerCompatibility(S, LHS, RHS, Loc);
}

/// \brief Return the resulting type when the operands are both pointers.
static QualType
checkConditionalObjectPointersCompatibility(Sema &S, ExprResult &LHS,
                                            ExprResult &RHS,
                                            SourceLocation Loc) {
  // get the pointer types
  QualType LHSTy = LHS.get()->getType();
  QualType RHSTy = RHS.get()->getType();

  // get the "pointed to" types
  QualType lhptee = LHSTy->getAs<PointerType>()->getPointeeType();
  QualType rhptee = RHSTy->getAs<PointerType>()->getPointeeType();

  // ignore qualifiers on void (C99 6.5.15p3, clause 6)
  if (lhptee->isVoidType() && rhptee->isIncompleteOrObjectType()) {
    // Figure out necessary qualifiers (C99 6.5.15p6)
    QualType destPointee
      = S.Context.getQualifiedType(lhptee, rhptee.getQualifiers());
    QualType destType = S.Context.getPointerType(destPointee);
    // Add qualifiers if necessary.
    LHS = S.ImpCastExprToType(LHS.take(), destType, CK_NoOp);
    // Promote to void*.
    RHS = S.ImpCastExprToType(RHS.take(), destType, CK_BitCast);
    return destType;
  }
  if (rhptee->isVoidType() && lhptee->isIncompleteOrObjectType()) {
    QualType destPointee
      = S.Context.getQualifiedType(rhptee, lhptee.getQualifiers());
    QualType destType = S.Context.getPointerType(destPointee);
    // Add qualifiers if necessary.
    RHS = S.ImpCastExprToType(RHS.take(), destType, CK_NoOp);
    // Promote to void*.
    LHS = S.ImpCastExprToType(LHS.take(), destType, CK_BitCast);
    return destType;
  }

  return checkConditionalPointerCompatibility(S, LHS, RHS, Loc);
}

/// \brief Return false if the first expression is not an integer and the second
/// expression is not a pointer, true otherwise.
static bool checkPointerIntegerMismatch(Sema &S, ExprResult &Int,
                                        Expr* PointerExpr, SourceLocation Loc,
                                        bool IsIntFirstExpr) {
  if (!PointerExpr->getType()->isPointerType() ||
      !Int.get()->getType()->isIntegerType())
    return false;

  Expr *Expr1 = IsIntFirstExpr ? Int.get() : PointerExpr;
  Expr *Expr2 = IsIntFirstExpr ? PointerExpr : Int.get();

  S.Diag(Loc, diag::warn_typecheck_cond_pointer_integer_mismatch)
    << Expr1->getType() << Expr2->getType()
    << Expr1->getSourceRange() << Expr2->getSourceRange();
  Int = S.ImpCastExprToType(Int.take(), PointerExpr->getType(),
                            CK_IntegralToPointer);
  return true;
}

/// Note that LHS is not null here, even if this is the gnu "x ?: y" extension.
/// In that case, LHS = cond.
/// C99 6.5.15
QualType Sema::CheckConditionalOperands(ExprResult &Cond, ExprResult &LHS,
                                        ExprResult &RHS, ExprValueKind &VK,
                                        ExprObjectKind &OK,
                                        SourceLocation QuestionLoc) {

  ExprResult LHSResult = CheckPlaceholderExpr(LHS.get());
  if (!LHSResult.isUsable()) return QualType();
  LHS = move(LHSResult);

  ExprResult RHSResult = CheckPlaceholderExpr(RHS.get());
  if (!RHSResult.isUsable()) return QualType();
  RHS = move(RHSResult);

  // C++ is sufficiently different to merit its own checker.
  if (getLangOptions().CPlusPlus)
    return CXXCheckConditionalOperands(Cond, LHS, RHS, VK, OK, QuestionLoc);

  VK = VK_RValue;
  OK = OK_Ordinary;

  Cond = UsualUnaryConversions(Cond.take());
  if (Cond.isInvalid())
    return QualType();
  LHS = UsualUnaryConversions(LHS.take());
  if (LHS.isInvalid())
    return QualType();
  RHS = UsualUnaryConversions(RHS.take());
  if (RHS.isInvalid())
    return QualType();

  QualType CondTy = Cond.get()->getType();
  QualType LHSTy = LHS.get()->getType();
  QualType RHSTy = RHS.get()->getType();

  // first, check the condition.
  if (checkCondition(*this, Cond.get()))
    return QualType();

  // Now check the two expressions.
  if (LHSTy->isVectorType() || RHSTy->isVectorType())
    return CheckVectorOperands(LHS, RHS, QuestionLoc, /*isCompAssign*/false);

  // OpenCL: If the condition is a vector, and both operands are scalar,
  // attempt to implicity convert them to the vector type to act like the
  // built in select.
  if (getLangOptions().OpenCL && CondTy->isVectorType())
    if (checkConditionalConvertScalarsToVectors(*this, LHS, RHS, CondTy))
      return QualType();
  
  // If both operands have arithmetic type, do the usual arithmetic conversions
  // to find a common type: C99 6.5.15p3,5.
  if (LHSTy->isArithmeticType() && RHSTy->isArithmeticType()) {
    UsualArithmeticConversions(LHS, RHS);
    if (LHS.isInvalid() || RHS.isInvalid())
      return QualType();
    return LHS.get()->getType();
  }

  // If both operands are the same structure or union type, the result is that
  // type.
  if (const RecordType *LHSRT = LHSTy->getAs<RecordType>()) {    // C99 6.5.15p3
    if (const RecordType *RHSRT = RHSTy->getAs<RecordType>())
      if (LHSRT->getDecl() == RHSRT->getDecl())
        // "If both the operands have structure or union type, the result has
        // that type."  This implies that CV qualifiers are dropped.
        return LHSTy.getUnqualifiedType();
    // FIXME: Type of conditional expression must be complete in C mode.
  }

  // C99 6.5.15p5: "If both operands have void type, the result has void type."
  // The following || allows only one side to be void (a GCC-ism).
  if (LHSTy->isVoidType() || RHSTy->isVoidType()) {
    return checkConditionalVoidType(*this, LHS, RHS);
  }

  // C99 6.5.15p6 - "if one operand is a null pointer constant, the result has
  // the type of the other operand."
  if (!checkConditionalNullPointer(*this, RHS, LHSTy)) return LHSTy;
  if (!checkConditionalNullPointer(*this, LHS, RHSTy)) return RHSTy;

  // All objective-c pointer type analysis is done here.
  QualType compositeType = FindCompositeObjCPointerType(LHS, RHS,
                                                        QuestionLoc);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();
  if (!compositeType.isNull())
    return compositeType;


  // Handle block pointer types.
  if (LHSTy->isBlockPointerType() || RHSTy->isBlockPointerType())
    return checkConditionalBlockPointerCompatibility(*this, LHS, RHS,
                                                     QuestionLoc);

  // Check constraints for C object pointers types (C99 6.5.15p3,6).
  if (LHSTy->isPointerType() && RHSTy->isPointerType())
    return checkConditionalObjectPointersCompatibility(*this, LHS, RHS,
                                                       QuestionLoc);

  // GCC compatibility: soften pointer/integer mismatch.  Note that
  // null pointers have been filtered out by this point.
  if (checkPointerIntegerMismatch(*this, LHS, RHS.get(), QuestionLoc,
      /*isIntFirstExpr=*/true))
    return RHSTy;
  if (checkPointerIntegerMismatch(*this, RHS, LHS.get(), QuestionLoc,
      /*isIntFirstExpr=*/false))
    return LHSTy;

  // Emit a better diagnostic if one of the expressions is a null pointer
  // constant and the other is not a pointer type. In this case, the user most
  // likely forgot to take the address of the other expression.
  if (DiagnoseConditionalForNull(LHS.get(), RHS.get(), QuestionLoc))
    return QualType();

  // Otherwise, the operands are not compatible.
  Diag(QuestionLoc, diag::err_typecheck_cond_incompatible_operands)
    << LHSTy << RHSTy << LHS.get()->getSourceRange()
    << RHS.get()->getSourceRange();
  return QualType();
}

/// FindCompositeObjCPointerType - Helper method to find composite type of
/// two objective-c pointer types of the two input expressions.
QualType Sema::FindCompositeObjCPointerType(ExprResult &LHS, ExprResult &RHS,
                                            SourceLocation QuestionLoc) {
  QualType LHSTy = LHS.get()->getType();
  QualType RHSTy = RHS.get()->getType();

  // Handle things like Class and struct objc_class*.  Here we case the result
  // to the pseudo-builtin, because that will be implicitly cast back to the
  // redefinition type if an attempt is made to access its fields.
  if (LHSTy->isObjCClassType() &&
      (Context.hasSameType(RHSTy, Context.getObjCClassRedefinitionType()))) {
    RHS = ImpCastExprToType(RHS.take(), LHSTy, CK_CPointerToObjCPointerCast);
    return LHSTy;
  }
  if (RHSTy->isObjCClassType() &&
      (Context.hasSameType(LHSTy, Context.getObjCClassRedefinitionType()))) {
    LHS = ImpCastExprToType(LHS.take(), RHSTy, CK_CPointerToObjCPointerCast);
    return RHSTy;
  }
  // And the same for struct objc_object* / id
  if (LHSTy->isObjCIdType() &&
      (Context.hasSameType(RHSTy, Context.getObjCIdRedefinitionType()))) {
    RHS = ImpCastExprToType(RHS.take(), LHSTy, CK_CPointerToObjCPointerCast);
    return LHSTy;
  }
  if (RHSTy->isObjCIdType() &&
      (Context.hasSameType(LHSTy, Context.getObjCIdRedefinitionType()))) {
    LHS = ImpCastExprToType(LHS.take(), RHSTy, CK_CPointerToObjCPointerCast);
    return RHSTy;
  }
  // And the same for struct objc_selector* / SEL
  if (Context.isObjCSelType(LHSTy) &&
      (Context.hasSameType(RHSTy, Context.getObjCSelRedefinitionType()))) {
    RHS = ImpCastExprToType(RHS.take(), LHSTy, CK_BitCast);
    return LHSTy;
  }
  if (Context.isObjCSelType(RHSTy) &&
      (Context.hasSameType(LHSTy, Context.getObjCSelRedefinitionType()))) {
    LHS = ImpCastExprToType(LHS.take(), RHSTy, CK_BitCast);
    return RHSTy;
  }
  // Check constraints for Objective-C object pointers types.
  if (LHSTy->isObjCObjectPointerType() && RHSTy->isObjCObjectPointerType()) {

    if (Context.getCanonicalType(LHSTy) == Context.getCanonicalType(RHSTy)) {
      // Two identical object pointer types are always compatible.
      return LHSTy;
    }
    const ObjCObjectPointerType *LHSOPT = LHSTy->castAs<ObjCObjectPointerType>();
    const ObjCObjectPointerType *RHSOPT = RHSTy->castAs<ObjCObjectPointerType>();
    QualType compositeType = LHSTy;

    // If both operands are interfaces and either operand can be
    // assigned to the other, use that type as the composite
    // type. This allows
    //   xxx ? (A*) a : (B*) b
    // where B is a subclass of A.
    //
    // Additionally, as for assignment, if either type is 'id'
    // allow silent coercion. Finally, if the types are
    // incompatible then make sure to use 'id' as the composite
    // type so the result is acceptable for sending messages to.

    // FIXME: Consider unifying with 'areComparableObjCPointerTypes'.
    // It could return the composite type.
    if (Context.canAssignObjCInterfaces(LHSOPT, RHSOPT)) {
      compositeType = RHSOPT->isObjCBuiltinType() ? RHSTy : LHSTy;
    } else if (Context.canAssignObjCInterfaces(RHSOPT, LHSOPT)) {
      compositeType = LHSOPT->isObjCBuiltinType() ? LHSTy : RHSTy;
    } else if ((LHSTy->isObjCQualifiedIdType() ||
                RHSTy->isObjCQualifiedIdType()) &&
               Context.ObjCQualifiedIdTypesAreCompatible(LHSTy, RHSTy, true)) {
      // Need to handle "id<xx>" explicitly.
      // GCC allows qualified id and any Objective-C type to devolve to
      // id. Currently localizing to here until clear this should be
      // part of ObjCQualifiedIdTypesAreCompatible.
      compositeType = Context.getObjCIdType();
    } else if (LHSTy->isObjCIdType() || RHSTy->isObjCIdType()) {
      compositeType = Context.getObjCIdType();
    } else if (!(compositeType =
                 Context.areCommonBaseCompatible(LHSOPT, RHSOPT)).isNull())
      ;
    else {
      Diag(QuestionLoc, diag::ext_typecheck_cond_incompatible_operands)
      << LHSTy << RHSTy
      << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
      QualType incompatTy = Context.getObjCIdType();
      LHS = ImpCastExprToType(LHS.take(), incompatTy, CK_BitCast);
      RHS = ImpCastExprToType(RHS.take(), incompatTy, CK_BitCast);
      return incompatTy;
    }
    // The object pointer types are compatible.
    LHS = ImpCastExprToType(LHS.take(), compositeType, CK_BitCast);
    RHS = ImpCastExprToType(RHS.take(), compositeType, CK_BitCast);
    return compositeType;
  }
  // Check Objective-C object pointer types and 'void *'
  if (LHSTy->isVoidPointerType() && RHSTy->isObjCObjectPointerType()) {
    QualType lhptee = LHSTy->getAs<PointerType>()->getPointeeType();
    QualType rhptee = RHSTy->getAs<ObjCObjectPointerType>()->getPointeeType();
    QualType destPointee
    = Context.getQualifiedType(lhptee, rhptee.getQualifiers());
    QualType destType = Context.getPointerType(destPointee);
    // Add qualifiers if necessary.
    LHS = ImpCastExprToType(LHS.take(), destType, CK_NoOp);
    // Promote to void*.
    RHS = ImpCastExprToType(RHS.take(), destType, CK_BitCast);
    return destType;
  }
  if (LHSTy->isObjCObjectPointerType() && RHSTy->isVoidPointerType()) {
    QualType lhptee = LHSTy->getAs<ObjCObjectPointerType>()->getPointeeType();
    QualType rhptee = RHSTy->getAs<PointerType>()->getPointeeType();
    QualType destPointee
    = Context.getQualifiedType(rhptee, lhptee.getQualifiers());
    QualType destType = Context.getPointerType(destPointee);
    // Add qualifiers if necessary.
    RHS = ImpCastExprToType(RHS.take(), destType, CK_NoOp);
    // Promote to void*.
    LHS = ImpCastExprToType(LHS.take(), destType, CK_BitCast);
    return destType;
  }
  return QualType();
}

/// SuggestParentheses - Emit a note with a fixit hint that wraps
/// ParenRange in parentheses.
static void SuggestParentheses(Sema &Self, SourceLocation Loc,
                               const PartialDiagnostic &Note,
                               SourceRange ParenRange) {
  SourceLocation EndLoc = Self.PP.getLocForEndOfToken(ParenRange.getEnd());
  if (ParenRange.getBegin().isFileID() && ParenRange.getEnd().isFileID() &&
      EndLoc.isValid()) {
    Self.Diag(Loc, Note)
      << FixItHint::CreateInsertion(ParenRange.getBegin(), "(")
      << FixItHint::CreateInsertion(EndLoc, ")");
  } else {
    // We can't display the parentheses, so just show the bare note.
    Self.Diag(Loc, Note) << ParenRange;
  }
}

static bool IsArithmeticOp(BinaryOperatorKind Opc) {
  return Opc >= BO_Mul && Opc <= BO_Shr;
}

/// IsArithmeticBinaryExpr - Returns true if E is an arithmetic binary
/// expression, either using a built-in or overloaded operator,
/// and sets *OpCode to the opcode and *RHSExprs to the right-hand side
/// expression.
static bool IsArithmeticBinaryExpr(Expr *E, BinaryOperatorKind *Opcode,
                                   Expr **RHSExprs) {
  // Don't strip parenthesis: we should not warn if E is in parenthesis.
  E = E->IgnoreImpCasts();
  E = E->IgnoreConversionOperator();
  E = E->IgnoreImpCasts();

  // Built-in binary operator.
  if (BinaryOperator *OP = dyn_cast<BinaryOperator>(E)) {
    if (IsArithmeticOp(OP->getOpcode())) {
      *Opcode = OP->getOpcode();
      *RHSExprs = OP->getRHS();
      return true;
    }
  }

  // Overloaded operator.
  if (CXXOperatorCallExpr *Call = dyn_cast<CXXOperatorCallExpr>(E)) {
    if (Call->getNumArgs() != 2)
      return false;

    // Make sure this is really a binary operator that is safe to pass into
    // BinaryOperator::getOverloadedOpcode(), e.g. it's not a subscript op.
    OverloadedOperatorKind OO = Call->getOperator();
    if (OO < OO_Plus || OO > OO_Arrow)
      return false;

    BinaryOperatorKind OpKind = BinaryOperator::getOverloadedOpcode(OO);
    if (IsArithmeticOp(OpKind)) {
      *Opcode = OpKind;
      *RHSExprs = Call->getArg(1);
      return true;
    }
  }

  return false;
}

static bool IsLogicOp(BinaryOperatorKind Opc) {
  return (Opc >= BO_LT && Opc <= BO_NE) || (Opc >= BO_LAnd && Opc <= BO_LOr);
}

/// ExprLooksBoolean - Returns true if E looks boolean, i.e. it has boolean type
/// or is a logical expression such as (x==y) which has int type, but is
/// commonly interpreted as boolean.
static bool ExprLooksBoolean(Expr *E) {
  E = E->IgnoreParenImpCasts();

  if (E->getType()->isBooleanType())
    return true;
  if (BinaryOperator *OP = dyn_cast<BinaryOperator>(E))
    return IsLogicOp(OP->getOpcode());
  if (UnaryOperator *OP = dyn_cast<UnaryOperator>(E))
    return OP->getOpcode() == UO_LNot;

  return false;
}

/// DiagnoseConditionalPrecedence - Emit a warning when a conditional operator
/// and binary operator are mixed in a way that suggests the programmer assumed
/// the conditional operator has higher precedence, for example:
/// "int x = a + someBinaryCondition ? 1 : 2".
static void DiagnoseConditionalPrecedence(Sema &Self,
                                          SourceLocation OpLoc,
                                          Expr *Condition,
                                          Expr *LHSExpr,
                                          Expr *RHSExpr) {
  BinaryOperatorKind CondOpcode;
  Expr *CondRHS;

  if (!IsArithmeticBinaryExpr(Condition, &CondOpcode, &CondRHS))
    return;
  if (!ExprLooksBoolean(CondRHS))
    return;

  // The condition is an arithmetic binary expression, with a right-
  // hand side that looks boolean, so warn.

  Self.Diag(OpLoc, diag::warn_precedence_conditional)
      << Condition->getSourceRange()
      << BinaryOperator::getOpcodeStr(CondOpcode);

  SuggestParentheses(Self, OpLoc,
    Self.PDiag(diag::note_precedence_conditional_silence)
      << BinaryOperator::getOpcodeStr(CondOpcode),
    SourceRange(Condition->getLocStart(), Condition->getLocEnd()));

  SuggestParentheses(Self, OpLoc,
    Self.PDiag(diag::note_precedence_conditional_first),
    SourceRange(CondRHS->getLocStart(), RHSExpr->getLocEnd()));
}

/// ActOnConditionalOp - Parse a ?: operation.  Note that 'LHS' may be null
/// in the case of a the GNU conditional expr extension.
ExprResult Sema::ActOnConditionalOp(SourceLocation QuestionLoc,
                                    SourceLocation ColonLoc,
                                    Expr *CondExpr, Expr *LHSExpr,
                                    Expr *RHSExpr) {
  // If this is the gnu "x ?: y" extension, analyze the types as though the LHS
  // was the condition.
  OpaqueValueExpr *opaqueValue = 0;
  Expr *commonExpr = 0;
  if (LHSExpr == 0) {
    commonExpr = CondExpr;

    // We usually want to apply unary conversions *before* saving, except
    // in the special case of a C++ l-value conditional.
    if (!(getLangOptions().CPlusPlus
          && !commonExpr->isTypeDependent()
          && commonExpr->getValueKind() == RHSExpr->getValueKind()
          && commonExpr->isGLValue()
          && commonExpr->isOrdinaryOrBitFieldObject()
          && RHSExpr->isOrdinaryOrBitFieldObject()
          && Context.hasSameType(commonExpr->getType(), RHSExpr->getType()))) {
      ExprResult commonRes = UsualUnaryConversions(commonExpr);
      if (commonRes.isInvalid())
        return ExprError();
      commonExpr = commonRes.take();
    }

    opaqueValue = new (Context) OpaqueValueExpr(commonExpr->getExprLoc(),
                                                commonExpr->getType(),
                                                commonExpr->getValueKind(),
                                                commonExpr->getObjectKind());
    LHSExpr = CondExpr = opaqueValue;
  }

  ExprValueKind VK = VK_RValue;
  ExprObjectKind OK = OK_Ordinary;
  ExprResult Cond = Owned(CondExpr), LHS = Owned(LHSExpr), RHS = Owned(RHSExpr);
  QualType result = CheckConditionalOperands(Cond, LHS, RHS, 
                                             VK, OK, QuestionLoc);
  if (result.isNull() || Cond.isInvalid() || LHS.isInvalid() ||
      RHS.isInvalid())
    return ExprError();

  DiagnoseConditionalPrecedence(*this, QuestionLoc, Cond.get(), LHS.get(),
                                RHS.get());

  if (!commonExpr)
    return Owned(new (Context) ConditionalOperator(Cond.take(), QuestionLoc,
                                                   LHS.take(), ColonLoc, 
                                                   RHS.take(), result, VK, OK));

  return Owned(new (Context)
    BinaryConditionalOperator(commonExpr, opaqueValue, Cond.take(), LHS.take(),
                              RHS.take(), QuestionLoc, ColonLoc, result, VK,
                              OK));
}

// checkPointerTypesForAssignment - This is a very tricky routine (despite
// being closely modeled after the C99 spec:-). The odd characteristic of this
// routine is it effectively iqnores the qualifiers on the top level pointee.
// This circumvents the usual type rules specified in 6.2.7p1 & 6.7.5.[1-3].
// FIXME: add a couple examples in this comment.
static Sema::AssignConvertType
checkPointerTypesForAssignment(Sema &S, QualType LHSType, QualType RHSType) {
  assert(LHSType.isCanonical() && "LHS not canonicalized!");
  assert(RHSType.isCanonical() && "RHS not canonicalized!");

  // get the "pointed to" type (ignoring qualifiers at the top level)
  const Type *lhptee, *rhptee;
  Qualifiers lhq, rhq;
  llvm::tie(lhptee, lhq) = cast<PointerType>(LHSType)->getPointeeType().split();
  llvm::tie(rhptee, rhq) = cast<PointerType>(RHSType)->getPointeeType().split();

  Sema::AssignConvertType ConvTy = Sema::Compatible;

  // C99 6.5.16.1p1: This following citation is common to constraints
  // 3 & 4 (below). ...and the type *pointed to* by the left has all the
  // qualifiers of the type *pointed to* by the right;
  Qualifiers lq;

  // As a special case, 'non-__weak A *' -> 'non-__weak const *' is okay.
  if (lhq.getObjCLifetime() != rhq.getObjCLifetime() &&
      lhq.compatiblyIncludesObjCLifetime(rhq)) {
    // Ignore lifetime for further calculation.
    lhq.removeObjCLifetime();
    rhq.removeObjCLifetime();
  }

  if (!lhq.compatiblyIncludes(rhq)) {
    // Treat address-space mismatches as fatal.  TODO: address subspaces
    if (lhq.getAddressSpace() != rhq.getAddressSpace())
      ConvTy = Sema::IncompatiblePointerDiscardsQualifiers;

    // It's okay to add or remove GC or lifetime qualifiers when converting to
    // and from void*.
    else if (lhq.withoutObjCGCAttr().withoutObjCGLifetime()
                        .compatiblyIncludes(
                                rhq.withoutObjCGCAttr().withoutObjCGLifetime())
             && (lhptee->isVoidType() || rhptee->isVoidType()))
      ; // keep old

    // Treat lifetime mismatches as fatal.
    else if (lhq.getObjCLifetime() != rhq.getObjCLifetime())
      ConvTy = Sema::IncompatiblePointerDiscardsQualifiers;
    
    // For GCC compatibility, other qualifier mismatches are treated
    // as still compatible in C.
    else ConvTy = Sema::CompatiblePointerDiscardsQualifiers;
  }

  // C99 6.5.16.1p1 (constraint 4): If one operand is a pointer to an object or
  // incomplete type and the other is a pointer to a qualified or unqualified
  // version of void...
  if (lhptee->isVoidType()) {
    if (rhptee->isIncompleteOrObjectType())
      return ConvTy;

    // As an extension, we allow cast to/from void* to function pointer.
    assert(rhptee->isFunctionType());
    return Sema::FunctionVoidPointer;
  }

  if (rhptee->isVoidType()) {
    if (lhptee->isIncompleteOrObjectType())
      return ConvTy;

    // As an extension, we allow cast to/from void* to function pointer.
    assert(lhptee->isFunctionType());
    return Sema::FunctionVoidPointer;
  }

  // C99 6.5.16.1p1 (constraint 3): both operands are pointers to qualified or
  // unqualified versions of compatible types, ...
  QualType ltrans = QualType(lhptee, 0), rtrans = QualType(rhptee, 0);
  if (!S.Context.typesAreCompatible(ltrans, rtrans)) {
    // Check if the pointee types are compatible ignoring the sign.
    // We explicitly check for char so that we catch "char" vs
    // "unsigned char" on systems where "char" is unsigned.
    if (lhptee->isCharType())
      ltrans = S.Context.UnsignedCharTy;
    else if (lhptee->hasSignedIntegerRepresentation())
      ltrans = S.Context.getCorrespondingUnsignedType(ltrans);

    if (rhptee->isCharType())
      rtrans = S.Context.UnsignedCharTy;
    else if (rhptee->hasSignedIntegerRepresentation())
      rtrans = S.Context.getCorrespondingUnsignedType(rtrans);

    if (ltrans == rtrans) {
      // Types are compatible ignoring the sign. Qualifier incompatibility
      // takes priority over sign incompatibility because the sign
      // warning can be disabled.
      if (ConvTy != Sema::Compatible)
        return ConvTy;

      return Sema::IncompatiblePointerSign;
    }

    // If we are a multi-level pointer, it's possible that our issue is simply
    // one of qualification - e.g. char ** -> const char ** is not allowed. If
    // the eventual target type is the same and the pointers have the same
    // level of indirection, this must be the issue.
    if (isa<PointerType>(lhptee) && isa<PointerType>(rhptee)) {
      do {
        lhptee = cast<PointerType>(lhptee)->getPointeeType().getTypePtr();
        rhptee = cast<PointerType>(rhptee)->getPointeeType().getTypePtr();
      } while (isa<PointerType>(lhptee) && isa<PointerType>(rhptee));

      if (lhptee == rhptee)
        return Sema::IncompatibleNestedPointerQualifiers;
    }

    // General pointer incompatibility takes priority over qualifiers.
    return Sema::IncompatiblePointer;
  }
  if (!S.getLangOptions().CPlusPlus &&
      S.IsNoReturnConversion(ltrans, rtrans, ltrans))
    return Sema::IncompatiblePointer;
  return ConvTy;
}

/// checkBlockPointerTypesForAssignment - This routine determines whether two
/// block pointer types are compatible or whether a block and normal pointer
/// are compatible. It is more restrict than comparing two function pointer
// types.
static Sema::AssignConvertType
checkBlockPointerTypesForAssignment(Sema &S, QualType LHSType,
                                    QualType RHSType) {
  assert(LHSType.isCanonical() && "LHS not canonicalized!");
  assert(RHSType.isCanonical() && "RHS not canonicalized!");

  QualType lhptee, rhptee;

  // get the "pointed to" type (ignoring qualifiers at the top level)
  lhptee = cast<BlockPointerType>(LHSType)->getPointeeType();
  rhptee = cast<BlockPointerType>(RHSType)->getPointeeType();

  // In C++, the types have to match exactly.
  if (S.getLangOptions().CPlusPlus)
    return Sema::IncompatibleBlockPointer;

  Sema::AssignConvertType ConvTy = Sema::Compatible;

  // For blocks we enforce that qualifiers are identical.
  if (lhptee.getLocalQualifiers() != rhptee.getLocalQualifiers())
    ConvTy = Sema::CompatiblePointerDiscardsQualifiers;

  if (!S.Context.typesAreBlockPointerCompatible(LHSType, RHSType))
    return Sema::IncompatibleBlockPointer;

  return ConvTy;
}

/// checkObjCPointerTypesForAssignment - Compares two objective-c pointer types
/// for assignment compatibility.
static Sema::AssignConvertType
checkObjCPointerTypesForAssignment(Sema &S, QualType LHSType,
                                   QualType RHSType) {
  assert(LHSType.isCanonical() && "LHS was not canonicalized!");
  assert(RHSType.isCanonical() && "RHS was not canonicalized!");

  if (LHSType->isObjCBuiltinType()) {
    // Class is not compatible with ObjC object pointers.
    if (LHSType->isObjCClassType() && !RHSType->isObjCBuiltinType() &&
        !RHSType->isObjCQualifiedClassType())
      return Sema::IncompatiblePointer;
    return Sema::Compatible;
  }
  if (RHSType->isObjCBuiltinType()) {
    if (RHSType->isObjCClassType() && !LHSType->isObjCBuiltinType() &&
        !LHSType->isObjCQualifiedClassType())
      return Sema::IncompatiblePointer;
    return Sema::Compatible;
  }
  QualType lhptee = LHSType->getAs<ObjCObjectPointerType>()->getPointeeType();
  QualType rhptee = RHSType->getAs<ObjCObjectPointerType>()->getPointeeType();

  if (!lhptee.isAtLeastAsQualifiedAs(rhptee))
    return Sema::CompatiblePointerDiscardsQualifiers;

  if (S.Context.typesAreCompatible(LHSType, RHSType))
    return Sema::Compatible;
  if (LHSType->isObjCQualifiedIdType() || RHSType->isObjCQualifiedIdType())
    return Sema::IncompatibleObjCQualifiedId;
  return Sema::IncompatiblePointer;
}

Sema::AssignConvertType
Sema::CheckAssignmentConstraints(SourceLocation Loc,
                                 QualType LHSType, QualType RHSType) {
  // Fake up an opaque expression.  We don't actually care about what
  // cast operations are required, so if CheckAssignmentConstraints
  // adds casts to this they'll be wasted, but fortunately that doesn't
  // usually happen on valid code.
  OpaqueValueExpr RHSExpr(Loc, RHSType, VK_RValue);
  ExprResult RHSPtr = &RHSExpr;
  CastKind K = CK_Invalid;

  return CheckAssignmentConstraints(LHSType, RHSPtr, K);
}

/// CheckAssignmentConstraints (C99 6.5.16) - This routine currently
/// has code to accommodate several GCC extensions when type checking
/// pointers. Here are some objectionable examples that GCC considers warnings:
///
///  int a, *pint;
///  short *pshort;
///  struct foo *pfoo;
///
///  pint = pshort; // warning: assignment from incompatible pointer type
///  a = pint; // warning: assignment makes integer from pointer without a cast
///  pint = a; // warning: assignment makes pointer from integer without a cast
///  pint = pfoo; // warning: assignment from incompatible pointer type
///
/// As a result, the code for dealing with pointers is more complex than the
/// C99 spec dictates.
///
/// Sets 'Kind' for any result kind except Incompatible.
Sema::AssignConvertType
Sema::CheckAssignmentConstraints(QualType LHSType, ExprResult &RHS,
                                 CastKind &Kind) {
  QualType RHSType = RHS.get()->getType();
  QualType OrigLHSType = LHSType;

  // Get canonical types.  We're not formatting these types, just comparing
  // them.
  LHSType = Context.getCanonicalType(LHSType).getUnqualifiedType();
  RHSType = Context.getCanonicalType(RHSType).getUnqualifiedType();

  // We can't do assignment from/to atomics yet.
  if (LHSType->isAtomicType())
    return Incompatible;

  // Common case: no conversion required.
  if (LHSType == RHSType) {
    Kind = CK_NoOp;
    return Compatible;
  }

  // If the left-hand side is a reference type, then we are in a
  // (rare!) case where we've allowed the use of references in C,
  // e.g., as a parameter type in a built-in function. In this case,
  // just make sure that the type referenced is compatible with the
  // right-hand side type. The caller is responsible for adjusting
  // LHSType so that the resulting expression does not have reference
  // type.
  if (const ReferenceType *LHSTypeRef = LHSType->getAs<ReferenceType>()) {
    if (Context.typesAreCompatible(LHSTypeRef->getPointeeType(), RHSType)) {
      Kind = CK_LValueBitCast;
      return Compatible;
    }
    return Incompatible;
  }

  // Allow scalar to ExtVector assignments, and assignments of an ExtVector type
  // to the same ExtVector type.
  if (LHSType->isExtVectorType()) {
    if (RHSType->isExtVectorType())
      return Incompatible;
    if (RHSType->isArithmeticType()) {
      // CK_VectorSplat does T -> vector T, so first cast to the
      // element type.
      QualType elType = cast<ExtVectorType>(LHSType)->getElementType();
      if (elType != RHSType) {
        Kind = PrepareScalarCast(RHS, elType);
        RHS = ImpCastExprToType(RHS.take(), elType, Kind);
      }
      Kind = CK_VectorSplat;
      return Compatible;
    }
  }

  // Conversions to or from vector type.
  if (LHSType->isVectorType() || RHSType->isVectorType()) {
    if (LHSType->isVectorType() && RHSType->isVectorType()) {
      // Allow assignments of an AltiVec vector type to an equivalent GCC
      // vector type and vice versa
      if (Context.areCompatibleVectorTypes(LHSType, RHSType)) {
        Kind = CK_BitCast;
        return Compatible;
      }

      // If we are allowing lax vector conversions, and LHS and RHS are both
      // vectors, the total size only needs to be the same. This is a bitcast;
      // no bits are changed but the result type is different.
      if (getLangOptions().LaxVectorConversions &&
          (Context.getTypeSize(LHSType) == Context.getTypeSize(RHSType))) {
        Kind = CK_BitCast;
        return IncompatibleVectors;
      }
    }
    return Incompatible;
  }

  // Arithmetic conversions.
  if (LHSType->isArithmeticType() && RHSType->isArithmeticType() &&
      !(getLangOptions().CPlusPlus && LHSType->isEnumeralType())) {
    Kind = PrepareScalarCast(RHS, LHSType);
    return Compatible;
  }

  // Conversions to normal pointers.
  if (const PointerType *LHSPointer = dyn_cast<PointerType>(LHSType)) {
    // U* -> T*
    if (isa<PointerType>(RHSType)) {
      Kind = CK_BitCast;
      return checkPointerTypesForAssignment(*this, LHSType, RHSType);
    }

    // int -> T*
    if (RHSType->isIntegerType()) {
      Kind = CK_IntegralToPointer; // FIXME: null?
      return IntToPointer;
    }

    // C pointers are not compatible with ObjC object pointers,
    // with two exceptions:
    if (isa<ObjCObjectPointerType>(RHSType)) {
      //  - conversions to void*
      if (LHSPointer->getPointeeType()->isVoidType()) {
        Kind = CK_BitCast;
        return Compatible;
      }

      //  - conversions from 'Class' to the redefinition type
      if (RHSType->isObjCClassType() &&
          Context.hasSameType(LHSType, 
                              Context.getObjCClassRedefinitionType())) {
        Kind = CK_BitCast;
        return Compatible;
      }

      Kind = CK_BitCast;
      return IncompatiblePointer;
    }

    // U^ -> void*
    if (RHSType->getAs<BlockPointerType>()) {
      if (LHSPointer->getPointeeType()->isVoidType()) {
        Kind = CK_BitCast;
        return Compatible;
      }
    }

    return Incompatible;
  }

  // Conversions to block pointers.
  if (isa<BlockPointerType>(LHSType)) {
    // U^ -> T^
    if (RHSType->isBlockPointerType()) {
      Kind = CK_BitCast;
      return checkBlockPointerTypesForAssignment(*this, LHSType, RHSType);
    }

    // int or null -> T^
    if (RHSType->isIntegerType()) {
      Kind = CK_IntegralToPointer; // FIXME: null
      return IntToBlockPointer;
    }

    // id -> T^
    if (getLangOptions().ObjC1 && RHSType->isObjCIdType()) {
      Kind = CK_AnyPointerToBlockPointerCast;
      return Compatible;
    }

    // void* -> T^
    if (const PointerType *RHSPT = RHSType->getAs<PointerType>())
      if (RHSPT->getPointeeType()->isVoidType()) {
        Kind = CK_AnyPointerToBlockPointerCast;
        return Compatible;
      }

    return Incompatible;
  }

  // Conversions to Objective-C pointers.
  if (isa<ObjCObjectPointerType>(LHSType)) {
    // A* -> B*
    if (RHSType->isObjCObjectPointerType()) {
      Kind = CK_BitCast;
      Sema::AssignConvertType result = 
        checkObjCPointerTypesForAssignment(*this, LHSType, RHSType);
      if (getLangOptions().ObjCAutoRefCount &&
          result == Compatible && 
          !CheckObjCARCUnavailableWeakConversion(OrigLHSType, RHSType))
        result = IncompatibleObjCWeakRef;
      return result;
    }

    // int or null -> A*
    if (RHSType->isIntegerType()) {
      Kind = CK_IntegralToPointer; // FIXME: null
      return IntToPointer;
    }

    // In general, C pointers are not compatible with ObjC object pointers,
    // with two exceptions:
    if (isa<PointerType>(RHSType)) {
      Kind = CK_CPointerToObjCPointerCast;

      //  - conversions from 'void*'
      if (RHSType->isVoidPointerType()) {
        return Compatible;
      }

      //  - conversions to 'Class' from its redefinition type
      if (LHSType->isObjCClassType() &&
          Context.hasSameType(RHSType, 
                              Context.getObjCClassRedefinitionType())) {
        return Compatible;
      }

      return IncompatiblePointer;
    }

    // T^ -> A*
    if (RHSType->isBlockPointerType()) {
      maybeExtendBlockObject(*this, RHS);
      Kind = CK_BlockPointerToObjCPointerCast;
      return Compatible;
    }

    return Incompatible;
  }

  // Conversions from pointers that are not covered by the above.
  if (isa<PointerType>(RHSType)) {
    // T* -> _Bool
    if (LHSType == Context.BoolTy) {
      Kind = CK_PointerToBoolean;
      return Compatible;
    }

    // T* -> int
    if (LHSType->isIntegerType()) {
      Kind = CK_PointerToIntegral;
      return PointerToInt;
    }

    return Incompatible;
  }

  // Conversions from Objective-C pointers that are not covered by the above.
  if (isa<ObjCObjectPointerType>(RHSType)) {
    // T* -> _Bool
    if (LHSType == Context.BoolTy) {
      Kind = CK_PointerToBoolean;
      return Compatible;
    }

    // T* -> int
    if (LHSType->isIntegerType()) {
      Kind = CK_PointerToIntegral;
      return PointerToInt;
    }

    return Incompatible;
  }

  // struct A -> struct B
  if (isa<TagType>(LHSType) && isa<TagType>(RHSType)) {
    if (Context.typesAreCompatible(LHSType, RHSType)) {
      Kind = CK_NoOp;
      return Compatible;
    }
  }

  return Incompatible;
}

/// \brief Constructs a transparent union from an expression that is
/// used to initialize the transparent union.
static void ConstructTransparentUnion(Sema &S, ASTContext &C,
                                      ExprResult &EResult, QualType UnionType,
                                      FieldDecl *Field) {
  // Build an initializer list that designates the appropriate member
  // of the transparent union.
  Expr *E = EResult.take();
  InitListExpr *Initializer = new (C) InitListExpr(C, SourceLocation(),
                                                   &E, 1,
                                                   SourceLocation());
  Initializer->setType(UnionType);
  Initializer->setInitializedFieldInUnion(Field);

  // Build a compound literal constructing a value of the transparent
  // union type from this initializer list.
  TypeSourceInfo *unionTInfo = C.getTrivialTypeSourceInfo(UnionType);
  EResult = S.Owned(
    new (C) CompoundLiteralExpr(SourceLocation(), unionTInfo, UnionType,
                                VK_RValue, Initializer, false));
}

Sema::AssignConvertType
Sema::CheckTransparentUnionArgumentConstraints(QualType ArgType,
                                               ExprResult &RHS) {
  QualType RHSType = RHS.get()->getType();

  // If the ArgType is a Union type, we want to handle a potential
  // transparent_union GCC extension.
  const RecordType *UT = ArgType->getAsUnionType();
  if (!UT || !UT->getDecl()->hasAttr<TransparentUnionAttr>())
    return Incompatible;

  // The field to initialize within the transparent union.
  RecordDecl *UD = UT->getDecl();
  FieldDecl *InitField = 0;
  // It's compatible if the expression matches any of the fields.
  for (RecordDecl::field_iterator it = UD->field_begin(),
         itend = UD->field_end();
       it != itend; ++it) {
    if (it->getType()->isPointerType()) {
      // If the transparent union contains a pointer type, we allow:
      // 1) void pointer
      // 2) null pointer constant
      if (RHSType->isPointerType())
        if (RHSType->castAs<PointerType>()->getPointeeType()->isVoidType()) {
          RHS = ImpCastExprToType(RHS.take(), it->getType(), CK_BitCast);
          InitField = *it;
          break;
        }

      if (RHS.get()->isNullPointerConstant(Context,
                                           Expr::NPC_ValueDependentIsNull)) {
        RHS = ImpCastExprToType(RHS.take(), it->getType(),
                                CK_NullToPointer);
        InitField = *it;
        break;
      }
    }

    CastKind Kind = CK_Invalid;
    if (CheckAssignmentConstraints(it->getType(), RHS, Kind)
          == Compatible) {
      RHS = ImpCastExprToType(RHS.take(), it->getType(), Kind);
      InitField = *it;
      break;
    }
  }

  if (!InitField)
    return Incompatible;

  ConstructTransparentUnion(*this, Context, RHS, ArgType, InitField);
  return Compatible;
}

Sema::AssignConvertType
Sema::CheckSingleAssignmentConstraints(QualType LHSType, ExprResult &RHS,
                                       bool Diagnose) {
  if (getLangOptions().CPlusPlus) {
    if (!LHSType->isRecordType() && !LHSType->isAtomicType()) {
      // C++ 5.17p3: If the left operand is not of class type, the
      // expression is implicitly converted (C++ 4) to the
      // cv-unqualified type of the left operand.
      ExprResult Res = PerformImplicitConversion(RHS.get(),
                                                 LHSType.getUnqualifiedType(),
                                                 AA_Assigning, Diagnose);
      if (Res.isInvalid())
        return Incompatible;
      Sema::AssignConvertType result = Compatible;
      if (getLangOptions().ObjCAutoRefCount &&
          !CheckObjCARCUnavailableWeakConversion(LHSType,
                                                 RHS.get()->getType()))
        result = IncompatibleObjCWeakRef;
      RHS = move(Res);
      return result;
    }

    // FIXME: Currently, we fall through and treat C++ classes like C
    // structures.
    // FIXME: We also fall through for atomics; not sure what should
    // happen there, though.
  }

  // C99 6.5.16.1p1: the left operand is a pointer and the right is
  // a null pointer constant.
  if ((LHSType->isPointerType() ||
       LHSType->isObjCObjectPointerType() ||
       LHSType->isBlockPointerType())
      && RHS.get()->isNullPointerConstant(Context,
                                          Expr::NPC_ValueDependentIsNull)) {
    RHS = ImpCastExprToType(RHS.take(), LHSType, CK_NullToPointer);
    return Compatible;
  }

  // This check seems unnatural, however it is necessary to ensure the proper
  // conversion of functions/arrays. If the conversion were done for all
  // DeclExpr's (created by ActOnIdExpression), it would mess up the unary
  // expressions that suppress this implicit conversion (&, sizeof).
  //
  // Suppress this for references: C++ 8.5.3p5.
  if (!LHSType->isReferenceType()) {
    RHS = DefaultFunctionArrayLvalueConversion(RHS.take());
    if (RHS.isInvalid())
      return Incompatible;
  }

  CastKind Kind = CK_Invalid;
  Sema::AssignConvertType result =
    CheckAssignmentConstraints(LHSType, RHS, Kind);

  // C99 6.5.16.1p2: The value of the right operand is converted to the
  // type of the assignment expression.
  // CheckAssignmentConstraints allows the left-hand side to be a reference,
  // so that we can use references in built-in functions even in C.
  // The getNonReferenceType() call makes sure that the resulting expression
  // does not have reference type.
  if (result != Incompatible && RHS.get()->getType() != LHSType)
    RHS = ImpCastExprToType(RHS.take(),
                            LHSType.getNonLValueExprType(Context), Kind);
  return result;
}

QualType Sema::InvalidOperands(SourceLocation Loc, ExprResult &LHS,
                               ExprResult &RHS) {
  Diag(Loc, diag::err_typecheck_invalid_operands)
    << LHS.get()->getType() << RHS.get()->getType()
    << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
  return QualType();
}

QualType Sema::CheckVectorOperands(ExprResult &LHS, ExprResult &RHS,
                                   SourceLocation Loc, bool IsCompAssign) {
  // For conversion purposes, we ignore any qualifiers.
  // For example, "const float" and "float" are equivalent.
  QualType LHSType =
    Context.getCanonicalType(LHS.get()->getType()).getUnqualifiedType();
  QualType RHSType =
    Context.getCanonicalType(RHS.get()->getType()).getUnqualifiedType();

  // If the vector types are identical, return.
  if (LHSType == RHSType)
    return LHSType;

  // Handle the case of equivalent AltiVec and GCC vector types
  if (LHSType->isVectorType() && RHSType->isVectorType() &&
      Context.areCompatibleVectorTypes(LHSType, RHSType)) {
    if (LHSType->isExtVectorType()) {
      RHS = ImpCastExprToType(RHS.take(), LHSType, CK_BitCast);
      return LHSType;
    }

    if (!IsCompAssign)
      LHS = ImpCastExprToType(LHS.take(), RHSType, CK_BitCast);
    return RHSType;
  }

  if (getLangOptions().LaxVectorConversions &&
      Context.getTypeSize(LHSType) == Context.getTypeSize(RHSType)) {
    // If we are allowing lax vector conversions, and LHS and RHS are both
    // vectors, the total size only needs to be the same. This is a
    // bitcast; no bits are changed but the result type is different.
    // FIXME: Should we really be allowing this?
    RHS = ImpCastExprToType(RHS.take(), LHSType, CK_BitCast);
    return LHSType;
  }

  // Canonicalize the ExtVector to the LHS, remember if we swapped so we can
  // swap back (so that we don't reverse the inputs to a subtract, for instance.
  bool swapped = false;
  if (RHSType->isExtVectorType() && !IsCompAssign) {
    swapped = true;
    std::swap(RHS, LHS);
    std::swap(RHSType, LHSType);
  }

  // Handle the case of an ext vector and scalar.
  if (const ExtVectorType *LV = LHSType->getAs<ExtVectorType>()) {
    QualType EltTy = LV->getElementType();
    if (EltTy->isIntegralType(Context) && RHSType->isIntegralType(Context)) {
      int order = Context.getIntegerTypeOrder(EltTy, RHSType);
      if (order > 0)
        RHS = ImpCastExprToType(RHS.take(), EltTy, CK_IntegralCast);
      if (order >= 0) {
        RHS = ImpCastExprToType(RHS.take(), LHSType, CK_VectorSplat);
        if (swapped) std::swap(RHS, LHS);
        return LHSType;
      }
    }
    if (EltTy->isRealFloatingType() && RHSType->isScalarType() &&
        RHSType->isRealFloatingType()) {
      int order = Context.getFloatingTypeOrder(EltTy, RHSType);
      if (order > 0)
        RHS = ImpCastExprToType(RHS.take(), EltTy, CK_FloatingCast);
      if (order >= 0) {
        RHS = ImpCastExprToType(RHS.take(), LHSType, CK_VectorSplat);
        if (swapped) std::swap(RHS, LHS);
        return LHSType;
      }
    }
  }

  // Vectors of different size or scalar and non-ext-vector are errors.
  if (swapped) std::swap(RHS, LHS);
  Diag(Loc, diag::err_typecheck_vector_not_convertable)
    << LHS.get()->getType() << RHS.get()->getType()
    << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
  return QualType();
}

// checkArithmeticNull - Detect when a NULL constant is used improperly in an
// expression.  These are mainly cases where the null pointer is used as an
// integer instead of a pointer.
static void checkArithmeticNull(Sema &S, ExprResult &LHS, ExprResult &RHS,
                                SourceLocation Loc, bool IsCompare) {
  // The canonical way to check for a GNU null is with isNullPointerConstant,
  // but we use a bit of a hack here for speed; this is a relatively
  // hot path, and isNullPointerConstant is slow.
  bool LHSNull = isa<GNUNullExpr>(LHS.get()->IgnoreParenImpCasts());
  bool RHSNull = isa<GNUNullExpr>(RHS.get()->IgnoreParenImpCasts());

  QualType NonNullType = LHSNull ? RHS.get()->getType() : LHS.get()->getType();

  // Avoid analyzing cases where the result will either be invalid (and
  // diagnosed as such) or entirely valid and not something to warn about.
  if ((!LHSNull && !RHSNull) || NonNullType->isBlockPointerType() ||
      NonNullType->isMemberPointerType() || NonNullType->isFunctionType())
    return;

  // Comparison operations would not make sense with a null pointer no matter
  // what the other expression is.
  if (!IsCompare) {
    S.Diag(Loc, diag::warn_null_in_arithmetic_operation)
        << (LHSNull ? LHS.get()->getSourceRange() : SourceRange())
        << (RHSNull ? RHS.get()->getSourceRange() : SourceRange());
    return;
  }

  // The rest of the operations only make sense with a null pointer
  // if the other expression is a pointer.
  if (LHSNull == RHSNull || NonNullType->isAnyPointerType() ||
      NonNullType->canDecayToPointerType())
    return;

  S.Diag(Loc, diag::warn_null_in_comparison_operation)
      << LHSNull /* LHS is NULL */ << NonNullType
      << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
}

QualType Sema::CheckMultiplyDivideOperands(ExprResult &LHS, ExprResult &RHS,
                                           SourceLocation Loc,
                                           bool IsCompAssign, bool IsDiv) {
  checkArithmeticNull(*this, LHS, RHS, Loc, /*isCompare=*/false);

  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType())
    return CheckVectorOperands(LHS, RHS, Loc, IsCompAssign);

  QualType compType = UsualArithmeticConversions(LHS, RHS, IsCompAssign);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  if (!LHS.get()->getType()->isArithmeticType() ||
      !RHS.get()->getType()->isArithmeticType())
    return InvalidOperands(Loc, LHS, RHS);

  // Check for division by zero.
  if (IsDiv &&
      RHS.get()->isNullPointerConstant(Context,
                                       Expr::NPC_ValueDependentIsNotNull))
    DiagRuntimeBehavior(Loc, RHS.get(), PDiag(diag::warn_division_by_zero)
                                          << RHS.get()->getSourceRange());

  return compType;
}

QualType Sema::CheckRemainderOperands(
  ExprResult &LHS, ExprResult &RHS, SourceLocation Loc, bool IsCompAssign) {
  checkArithmeticNull(*this, LHS, RHS, Loc, /*isCompare=*/false);

  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() && 
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckVectorOperands(LHS, RHS, Loc, IsCompAssign);
    return InvalidOperands(Loc, LHS, RHS);
  }

  QualType compType = UsualArithmeticConversions(LHS, RHS, IsCompAssign);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  if (!LHS.get()->getType()->isIntegerType() ||
      !RHS.get()->getType()->isIntegerType())
    return InvalidOperands(Loc, LHS, RHS);

  // Check for remainder by zero.
  if (RHS.get()->isNullPointerConstant(Context,
                                       Expr::NPC_ValueDependentIsNotNull))
    DiagRuntimeBehavior(Loc, RHS.get(), PDiag(diag::warn_remainder_by_zero)
                                 << RHS.get()->getSourceRange());

  return compType;
}

/// \brief Diagnose invalid arithmetic on two void pointers.
static void diagnoseArithmeticOnTwoVoidPointers(Sema &S, SourceLocation Loc,
                                                Expr *LHSExpr, Expr *RHSExpr) {
  S.Diag(Loc, S.getLangOptions().CPlusPlus
                ? diag::err_typecheck_pointer_arith_void_type
                : diag::ext_gnu_void_ptr)
    << 1 /* two pointers */ << LHSExpr->getSourceRange()
                            << RHSExpr->getSourceRange();
}

/// \brief Diagnose invalid arithmetic on a void pointer.
static void diagnoseArithmeticOnVoidPointer(Sema &S, SourceLocation Loc,
                                            Expr *Pointer) {
  S.Diag(Loc, S.getLangOptions().CPlusPlus
                ? diag::err_typecheck_pointer_arith_void_type
                : diag::ext_gnu_void_ptr)
    << 0 /* one pointer */ << Pointer->getSourceRange();
}

/// \brief Diagnose invalid arithmetic on two function pointers.
static void diagnoseArithmeticOnTwoFunctionPointers(Sema &S, SourceLocation Loc,
                                                    Expr *LHS, Expr *RHS) {
  assert(LHS->getType()->isAnyPointerType());
  assert(RHS->getType()->isAnyPointerType());
  S.Diag(Loc, S.getLangOptions().CPlusPlus
                ? diag::err_typecheck_pointer_arith_function_type
                : diag::ext_gnu_ptr_func_arith)
    << 1 /* two pointers */ << LHS->getType()->getPointeeType()
    // We only show the second type if it differs from the first.
    << (unsigned)!S.Context.hasSameUnqualifiedType(LHS->getType(),
                                                   RHS->getType())
    << RHS->getType()->getPointeeType()
    << LHS->getSourceRange() << RHS->getSourceRange();
}

/// \brief Diagnose invalid arithmetic on a function pointer.
static void diagnoseArithmeticOnFunctionPointer(Sema &S, SourceLocation Loc,
                                                Expr *Pointer) {
  assert(Pointer->getType()->isAnyPointerType());
  S.Diag(Loc, S.getLangOptions().CPlusPlus
                ? diag::err_typecheck_pointer_arith_function_type
                : diag::ext_gnu_ptr_func_arith)
    << 0 /* one pointer */ << Pointer->getType()->getPointeeType()
    << 0 /* one pointer, so only one type */
    << Pointer->getSourceRange();
}

/// \brief Emit error if Operand is incomplete pointer type
///
/// \returns True if pointer has incomplete type
static bool checkArithmeticIncompletePointerType(Sema &S, SourceLocation Loc,
                                                 Expr *Operand) {
  if ((Operand->getType()->isPointerType() &&
       !Operand->getType()->isDependentType()) ||
      Operand->getType()->isObjCObjectPointerType()) {
    QualType PointeeTy = Operand->getType()->getPointeeType();
    if (S.RequireCompleteType(
          Loc, PointeeTy,
          S.PDiag(diag::err_typecheck_arithmetic_incomplete_type)
            << PointeeTy << Operand->getSourceRange()))
      return true;
  }
  return false;
}

/// \brief Check the validity of an arithmetic pointer operand.
///
/// If the operand has pointer type, this code will check for pointer types
/// which are invalid in arithmetic operations. These will be diagnosed
/// appropriately, including whether or not the use is supported as an
/// extension.
///
/// \returns True when the operand is valid to use (even if as an extension).
static bool checkArithmeticOpPointerOperand(Sema &S, SourceLocation Loc,
                                            Expr *Operand) {
  if (!Operand->getType()->isAnyPointerType()) return true;

  QualType PointeeTy = Operand->getType()->getPointeeType();
  if (PointeeTy->isVoidType()) {
    diagnoseArithmeticOnVoidPointer(S, Loc, Operand);
    return !S.getLangOptions().CPlusPlus;
  }
  if (PointeeTy->isFunctionType()) {
    diagnoseArithmeticOnFunctionPointer(S, Loc, Operand);
    return !S.getLangOptions().CPlusPlus;
  }

  if (checkArithmeticIncompletePointerType(S, Loc, Operand)) return false;

  return true;
}

/// \brief Check the validity of a binary arithmetic operation w.r.t. pointer
/// operands.
///
/// This routine will diagnose any invalid arithmetic on pointer operands much
/// like \see checkArithmeticOpPointerOperand. However, it has special logic
/// for emitting a single diagnostic even for operations where both LHS and RHS
/// are (potentially problematic) pointers.
///
/// \returns True when the operand is valid to use (even if as an extension).
static bool checkArithmeticBinOpPointerOperands(Sema &S, SourceLocation Loc,
                                                Expr *LHSExpr, Expr *RHSExpr) {
  bool isLHSPointer = LHSExpr->getType()->isAnyPointerType();
  bool isRHSPointer = RHSExpr->getType()->isAnyPointerType();
  if (!isLHSPointer && !isRHSPointer) return true;

  QualType LHSPointeeTy, RHSPointeeTy;
  if (isLHSPointer) LHSPointeeTy = LHSExpr->getType()->getPointeeType();
  if (isRHSPointer) RHSPointeeTy = RHSExpr->getType()->getPointeeType();

  // Check for arithmetic on pointers to incomplete types.
  bool isLHSVoidPtr = isLHSPointer && LHSPointeeTy->isVoidType();
  bool isRHSVoidPtr = isRHSPointer && RHSPointeeTy->isVoidType();
  if (isLHSVoidPtr || isRHSVoidPtr) {
    if (!isRHSVoidPtr) diagnoseArithmeticOnVoidPointer(S, Loc, LHSExpr);
    else if (!isLHSVoidPtr) diagnoseArithmeticOnVoidPointer(S, Loc, RHSExpr);
    else diagnoseArithmeticOnTwoVoidPointers(S, Loc, LHSExpr, RHSExpr);

    return !S.getLangOptions().CPlusPlus;
  }

  bool isLHSFuncPtr = isLHSPointer && LHSPointeeTy->isFunctionType();
  bool isRHSFuncPtr = isRHSPointer && RHSPointeeTy->isFunctionType();
  if (isLHSFuncPtr || isRHSFuncPtr) {
    if (!isRHSFuncPtr) diagnoseArithmeticOnFunctionPointer(S, Loc, LHSExpr);
    else if (!isLHSFuncPtr) diagnoseArithmeticOnFunctionPointer(S, Loc,
                                                                RHSExpr);
    else diagnoseArithmeticOnTwoFunctionPointers(S, Loc, LHSExpr, RHSExpr);

    return !S.getLangOptions().CPlusPlus;
  }

  if (checkArithmeticIncompletePointerType(S, Loc, LHSExpr)) return false;
  if (checkArithmeticIncompletePointerType(S, Loc, RHSExpr)) return false;

  return true;
}

/// \brief Check bad cases where we step over interface counts.
static bool checkArithmethicPointerOnNonFragileABI(Sema &S,
                                                   SourceLocation OpLoc,
                                                   Expr *Op) {
  assert(Op->getType()->isAnyPointerType());
  QualType PointeeTy = Op->getType()->getPointeeType();
  if (!PointeeTy->isObjCObjectType() || !S.LangOpts.ObjCNonFragileABI)
    return true;

  S.Diag(OpLoc, diag::err_arithmetic_nonfragile_interface)
    << PointeeTy << Op->getSourceRange();
  return false;
}

/// \brief Emit error when two pointers are incompatible.
static void diagnosePointerIncompatibility(Sema &S, SourceLocation Loc,
                                           Expr *LHSExpr, Expr *RHSExpr) {
  assert(LHSExpr->getType()->isAnyPointerType());
  assert(RHSExpr->getType()->isAnyPointerType());
  S.Diag(Loc, diag::err_typecheck_sub_ptr_compatible)
    << LHSExpr->getType() << RHSExpr->getType() << LHSExpr->getSourceRange()
    << RHSExpr->getSourceRange();
}

QualType Sema::CheckAdditionOperands( // C99 6.5.6
  ExprResult &LHS, ExprResult &RHS, SourceLocation Loc, QualType* CompLHSTy) {
  checkArithmeticNull(*this, LHS, RHS, Loc, /*isCompare=*/false);

  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    QualType compType = CheckVectorOperands(LHS, RHS, Loc, CompLHSTy);
    if (CompLHSTy) *CompLHSTy = compType;
    return compType;
  }

  QualType compType = UsualArithmeticConversions(LHS, RHS, CompLHSTy);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  // handle the common case first (both operands are arithmetic).
  if (LHS.get()->getType()->isArithmeticType() &&
      RHS.get()->getType()->isArithmeticType()) {
    if (CompLHSTy) *CompLHSTy = compType;
    return compType;
  }

  // Put any potential pointer into PExp
  Expr* PExp = LHS.get(), *IExp = RHS.get();
  if (IExp->getType()->isAnyPointerType())
    std::swap(PExp, IExp);

  if (!PExp->getType()->isAnyPointerType())
    return InvalidOperands(Loc, LHS, RHS);

  if (!IExp->getType()->isIntegerType())
    return InvalidOperands(Loc, LHS, RHS);

  if (!checkArithmeticOpPointerOperand(*this, Loc, PExp))
    return QualType();

  // Diagnose bad cases where we step over interface counts.
  if (!checkArithmethicPointerOnNonFragileABI(*this, Loc, PExp))
    return QualType();

  // Check array bounds for pointer arithemtic
  CheckArrayAccess(PExp, IExp);

  if (CompLHSTy) {
    QualType LHSTy = Context.isPromotableBitField(LHS.get());
    if (LHSTy.isNull()) {
      LHSTy = LHS.get()->getType();
      if (LHSTy->isPromotableIntegerType())
        LHSTy = Context.getPromotedIntegerType(LHSTy);
    }
    *CompLHSTy = LHSTy;
  }

  return PExp->getType();
}

// C99 6.5.6
QualType Sema::CheckSubtractionOperands(ExprResult &LHS, ExprResult &RHS,
                                        SourceLocation Loc,
                                        QualType* CompLHSTy) {
  checkArithmeticNull(*this, LHS, RHS, Loc, /*isCompare=*/false);

  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    QualType compType = CheckVectorOperands(LHS, RHS, Loc, CompLHSTy);
    if (CompLHSTy) *CompLHSTy = compType;
    return compType;
  }

  QualType compType = UsualArithmeticConversions(LHS, RHS, CompLHSTy);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  // Enforce type constraints: C99 6.5.6p3.

  // Handle the common case first (both operands are arithmetic).
  if (LHS.get()->getType()->isArithmeticType() &&
      RHS.get()->getType()->isArithmeticType()) {
    if (CompLHSTy) *CompLHSTy = compType;
    return compType;
  }

  // Either ptr - int   or   ptr - ptr.
  if (LHS.get()->getType()->isAnyPointerType()) {
    QualType lpointee = LHS.get()->getType()->getPointeeType();

    // Diagnose bad cases where we step over interface counts.
    if (!checkArithmethicPointerOnNonFragileABI(*this, Loc, LHS.get()))
      return QualType();

    // The result type of a pointer-int computation is the pointer type.
    if (RHS.get()->getType()->isIntegerType()) {
      if (!checkArithmeticOpPointerOperand(*this, Loc, LHS.get()))
        return QualType();

      Expr *IExpr = RHS.get()->IgnoreParenCasts();
      UnaryOperator negRex(IExpr, UO_Minus, IExpr->getType(), VK_RValue,
                           OK_Ordinary, IExpr->getExprLoc());
      // Check array bounds for pointer arithemtic
      CheckArrayAccess(LHS.get()->IgnoreParenCasts(), &negRex);

      if (CompLHSTy) *CompLHSTy = LHS.get()->getType();
      return LHS.get()->getType();
    }

    // Handle pointer-pointer subtractions.
    if (const PointerType *RHSPTy
          = RHS.get()->getType()->getAs<PointerType>()) {
      QualType rpointee = RHSPTy->getPointeeType();

      if (getLangOptions().CPlusPlus) {
        // Pointee types must be the same: C++ [expr.add]
        if (!Context.hasSameUnqualifiedType(lpointee, rpointee)) {
          diagnosePointerIncompatibility(*this, Loc, LHS.get(), RHS.get());
        }
      } else {
        // Pointee types must be compatible C99 6.5.6p3
        if (!Context.typesAreCompatible(
                Context.getCanonicalType(lpointee).getUnqualifiedType(),
                Context.getCanonicalType(rpointee).getUnqualifiedType())) {
          diagnosePointerIncompatibility(*this, Loc, LHS.get(), RHS.get());
          return QualType();
        }
      }

      if (!checkArithmeticBinOpPointerOperands(*this, Loc,
                                               LHS.get(), RHS.get()))
        return QualType();

      if (CompLHSTy) *CompLHSTy = LHS.get()->getType();
      return Context.getPointerDiffType();
    }
  }

  return InvalidOperands(Loc, LHS, RHS);
}

static bool isScopedEnumerationType(QualType T) {
  if (const EnumType *ET = dyn_cast<EnumType>(T))
    return ET->getDecl()->isScoped();
  return false;
}

static void DiagnoseBadShiftValues(Sema& S, ExprResult &LHS, ExprResult &RHS,
                                   SourceLocation Loc, unsigned Opc,
                                   QualType LHSType) {
  llvm::APSInt Right;
  // Check right/shifter operand
  if (RHS.get()->isValueDependent() ||
      !RHS.get()->isIntegerConstantExpr(Right, S.Context))
    return;

  if (Right.isNegative()) {
    S.DiagRuntimeBehavior(Loc, RHS.get(),
                          S.PDiag(diag::warn_shift_negative)
                            << RHS.get()->getSourceRange());
    return;
  }
  llvm::APInt LeftBits(Right.getBitWidth(),
                       S.Context.getTypeSize(LHS.get()->getType()));
  if (Right.uge(LeftBits)) {
    S.DiagRuntimeBehavior(Loc, RHS.get(),
                          S.PDiag(diag::warn_shift_gt_typewidth)
                            << RHS.get()->getSourceRange());
    return;
  }
  if (Opc != BO_Shl)
    return;

  // When left shifting an ICE which is signed, we can check for overflow which
  // according to C++ has undefined behavior ([expr.shift] 5.8/2). Unsigned
  // integers have defined behavior modulo one more than the maximum value
  // representable in the result type, so never warn for those.
  llvm::APSInt Left;
  if (LHS.get()->isValueDependent() ||
      !LHS.get()->isIntegerConstantExpr(Left, S.Context) ||
      LHSType->hasUnsignedIntegerRepresentation())
    return;
  llvm::APInt ResultBits =
      static_cast<llvm::APInt&>(Right) + Left.getMinSignedBits();
  if (LeftBits.uge(ResultBits))
    return;
  llvm::APSInt Result = Left.extend(ResultBits.getLimitedValue());
  Result = Result.shl(Right);

  // Print the bit representation of the signed integer as an unsigned
  // hexadecimal number.
  llvm::SmallString<40> HexResult;
  Result.toString(HexResult, 16, /*Signed =*/false, /*Literal =*/true);

  // If we are only missing a sign bit, this is less likely to result in actual
  // bugs -- if the result is cast back to an unsigned type, it will have the
  // expected value. Thus we place this behind a different warning that can be
  // turned off separately if needed.
  if (LeftBits == ResultBits - 1) {
    S.Diag(Loc, diag::warn_shift_result_sets_sign_bit)
        << HexResult.str() << LHSType
        << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
    return;
  }

  S.Diag(Loc, diag::warn_shift_result_gt_typewidth)
    << HexResult.str() << Result.getMinSignedBits() << LHSType
    << Left.getBitWidth() << LHS.get()->getSourceRange()
    << RHS.get()->getSourceRange();
}

// C99 6.5.7
QualType Sema::CheckShiftOperands(ExprResult &LHS, ExprResult &RHS,
                                  SourceLocation Loc, unsigned Opc,
                                  bool IsCompAssign) {
  checkArithmeticNull(*this, LHS, RHS, Loc, /*isCompare=*/false);

  // C99 6.5.7p2: Each of the operands shall have integer type.
  if (!LHS.get()->getType()->hasIntegerRepresentation() || 
      !RHS.get()->getType()->hasIntegerRepresentation())
    return InvalidOperands(Loc, LHS, RHS);

  // C++0x: Don't allow scoped enums. FIXME: Use something better than
  // hasIntegerRepresentation() above instead of this.
  if (isScopedEnumerationType(LHS.get()->getType()) ||
      isScopedEnumerationType(RHS.get()->getType())) {
    return InvalidOperands(Loc, LHS, RHS);
  }

  // Vector shifts promote their scalar inputs to vector type.
  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType())
    return CheckVectorOperands(LHS, RHS, Loc, IsCompAssign);

  // Shifts don't perform usual arithmetic conversions, they just do integer
  // promotions on each operand. C99 6.5.7p3

  // For the LHS, do usual unary conversions, but then reset them away
  // if this is a compound assignment.
  ExprResult OldLHS = LHS;
  LHS = UsualUnaryConversions(LHS.take());
  if (LHS.isInvalid())
    return QualType();
  QualType LHSType = LHS.get()->getType();
  if (IsCompAssign) LHS = OldLHS;

  // The RHS is simpler.
  RHS = UsualUnaryConversions(RHS.take());
  if (RHS.isInvalid())
    return QualType();

  // Sanity-check shift operands
  DiagnoseBadShiftValues(*this, LHS, RHS, Loc, Opc, LHSType);

  // "The type of the result is that of the promoted left operand."
  return LHSType;
}

static bool IsWithinTemplateSpecialization(Decl *D) {
  if (DeclContext *DC = D->getDeclContext()) {
    if (isa<ClassTemplateSpecializationDecl>(DC))
      return true;
    if (FunctionDecl *FD = dyn_cast<FunctionDecl>(DC))
      return FD->isFunctionTemplateSpecialization();
  }
  return false;
}

/// If two different enums are compared, raise a warning.
static void checkEnumComparison(Sema &S, SourceLocation Loc, ExprResult &LHS,
                                ExprResult &RHS) {
  QualType LHSStrippedType = LHS.get()->IgnoreParenImpCasts()->getType();
  QualType RHSStrippedType = RHS.get()->IgnoreParenImpCasts()->getType();

  const EnumType *LHSEnumType = LHSStrippedType->getAs<EnumType>();
  if (!LHSEnumType)
    return;
  const EnumType *RHSEnumType = RHSStrippedType->getAs<EnumType>();
  if (!RHSEnumType)
    return;

  // Ignore anonymous enums.
  if (!LHSEnumType->getDecl()->getIdentifier())
    return;
  if (!RHSEnumType->getDecl()->getIdentifier())
    return;

  if (S.Context.hasSameUnqualifiedType(LHSStrippedType, RHSStrippedType))
    return;

  S.Diag(Loc, diag::warn_comparison_of_mixed_enum_types)
      << LHSStrippedType << RHSStrippedType
      << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
}

/// \brief Diagnose bad pointer comparisons.
static void diagnoseDistinctPointerComparison(Sema &S, SourceLocation Loc,
                                              ExprResult &LHS, ExprResult &RHS,
                                              bool IsError) {
  S.Diag(Loc, IsError ? diag::err_typecheck_comparison_of_distinct_pointers
                      : diag::ext_typecheck_comparison_of_distinct_pointers)
    << LHS.get()->getType() << RHS.get()->getType()
    << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
}

/// \brief Returns false if the pointers are converted to a composite type,
/// true otherwise.
static bool convertPointersToCompositeType(Sema &S, SourceLocation Loc,
                                           ExprResult &LHS, ExprResult &RHS) {
  // C++ [expr.rel]p2:
  //   [...] Pointer conversions (4.10) and qualification
  //   conversions (4.4) are performed on pointer operands (or on
  //   a pointer operand and a null pointer constant) to bring
  //   them to their composite pointer type. [...]
  //
  // C++ [expr.eq]p1 uses the same notion for (in)equality
  // comparisons of pointers.

  // C++ [expr.eq]p2:
  //   In addition, pointers to members can be compared, or a pointer to
  //   member and a null pointer constant. Pointer to member conversions
  //   (4.11) and qualification conversions (4.4) are performed to bring
  //   them to a common type. If one operand is a null pointer constant,
  //   the common type is the type of the other operand. Otherwise, the
  //   common type is a pointer to member type similar (4.4) to the type
  //   of one of the operands, with a cv-qualification signature (4.4)
  //   that is the union of the cv-qualification signatures of the operand
  //   types.

  QualType LHSType = LHS.get()->getType();
  QualType RHSType = RHS.get()->getType();
  assert((LHSType->isPointerType() && RHSType->isPointerType()) ||
         (LHSType->isMemberPointerType() && RHSType->isMemberPointerType()));

  bool NonStandardCompositeType = false;
  bool *BoolPtr = S.isSFINAEContext() ? 0 : &NonStandardCompositeType;
  QualType T = S.FindCompositePointerType(Loc, LHS, RHS, BoolPtr);
  if (T.isNull()) {
    diagnoseDistinctPointerComparison(S, Loc, LHS, RHS, /*isError*/true);
    return true;
  }

  if (NonStandardCompositeType)
    S.Diag(Loc, diag::ext_typecheck_comparison_of_distinct_pointers_nonstandard)
      << LHSType << RHSType << T << LHS.get()->getSourceRange()
      << RHS.get()->getSourceRange();

  LHS = S.ImpCastExprToType(LHS.take(), T, CK_BitCast);
  RHS = S.ImpCastExprToType(RHS.take(), T, CK_BitCast);
  return false;
}

static void diagnoseFunctionPointerToVoidComparison(Sema &S, SourceLocation Loc,
                                                    ExprResult &LHS,
                                                    ExprResult &RHS,
                                                    bool IsError) {
  S.Diag(Loc, IsError ? diag::err_typecheck_comparison_of_fptr_to_void
                      : diag::ext_typecheck_comparison_of_fptr_to_void)
    << LHS.get()->getType() << RHS.get()->getType()
    << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
}

// C99 6.5.8, C++ [expr.rel]
QualType Sema::CheckCompareOperands(ExprResult &LHS, ExprResult &RHS,
                                    SourceLocation Loc, unsigned OpaqueOpc,
                                    bool IsRelational) {
  checkArithmeticNull(*this, LHS, RHS, Loc, /*isCompare=*/true);

  BinaryOperatorKind Opc = (BinaryOperatorKind) OpaqueOpc;

  // Handle vector comparisons separately.
  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType())
    return CheckVectorCompareOperands(LHS, RHS, Loc, IsRelational);

  QualType LHSType = LHS.get()->getType();
  QualType RHSType = RHS.get()->getType();

  Expr *LHSStripped = LHS.get()->IgnoreParenImpCasts();
  Expr *RHSStripped = RHS.get()->IgnoreParenImpCasts();

  checkEnumComparison(*this, Loc, LHS, RHS);

  if (!LHSType->hasFloatingRepresentation() &&
      !(LHSType->isBlockPointerType() && IsRelational) &&
      !LHS.get()->getLocStart().isMacroID() &&
      !RHS.get()->getLocStart().isMacroID()) {
    // For non-floating point types, check for self-comparisons of the form
    // x == x, x != x, x < x, etc.  These always evaluate to a constant, and
    // often indicate logic errors in the program.
    //
    // NOTE: Don't warn about comparison expressions resulting from macro
    // expansion. Also don't warn about comparisons which are only self
    // comparisons within a template specialization. The warnings should catch
    // obvious cases in the definition of the template anyways. The idea is to
    // warn when the typed comparison operator will always evaluate to the same
    // result.
    if (DeclRefExpr* DRL = dyn_cast<DeclRefExpr>(LHSStripped)) {
      if (DeclRefExpr* DRR = dyn_cast<DeclRefExpr>(RHSStripped)) {
        if (DRL->getDecl() == DRR->getDecl() &&
            !IsWithinTemplateSpecialization(DRL->getDecl())) {
          DiagRuntimeBehavior(Loc, 0, PDiag(diag::warn_comparison_always)
                              << 0 // self-
                              << (Opc == BO_EQ
                                  || Opc == BO_LE
                                  || Opc == BO_GE));
        } else if (LHSType->isArrayType() && RHSType->isArrayType() &&
                   !DRL->getDecl()->getType()->isReferenceType() &&
                   !DRR->getDecl()->getType()->isReferenceType()) {
            // what is it always going to eval to?
            char always_evals_to;
            switch(Opc) {
            case BO_EQ: // e.g. array1 == array2
              always_evals_to = 0; // false
              break;
            case BO_NE: // e.g. array1 != array2
              always_evals_to = 1; // true
              break;
            default:
              // best we can say is 'a constant'
              always_evals_to = 2; // e.g. array1 <= array2
              break;
            }
            DiagRuntimeBehavior(Loc, 0, PDiag(diag::warn_comparison_always)
                                << 1 // array
                                << always_evals_to);
        }
      }
    }

    if (isa<CastExpr>(LHSStripped))
      LHSStripped = LHSStripped->IgnoreParenCasts();
    if (isa<CastExpr>(RHSStripped))
      RHSStripped = RHSStripped->IgnoreParenCasts();

    // Warn about comparisons against a string constant (unless the other
    // operand is null), the user probably wants strcmp.
    Expr *literalString = 0;
    Expr *literalStringStripped = 0;
    if ((isa<StringLiteral>(LHSStripped) || isa<ObjCEncodeExpr>(LHSStripped)) &&
        !RHSStripped->isNullPointerConstant(Context,
                                            Expr::NPC_ValueDependentIsNull)) {
      literalString = LHS.get();
      literalStringStripped = LHSStripped;
    } else if ((isa<StringLiteral>(RHSStripped) ||
                isa<ObjCEncodeExpr>(RHSStripped)) &&
               !LHSStripped->isNullPointerConstant(Context,
                                            Expr::NPC_ValueDependentIsNull)) {
      literalString = RHS.get();
      literalStringStripped = RHSStripped;
    }

    if (literalString) {
      std::string resultComparison;
      switch (Opc) {
      case BO_LT: resultComparison = ") < 0"; break;
      case BO_GT: resultComparison = ") > 0"; break;
      case BO_LE: resultComparison = ") <= 0"; break;
      case BO_GE: resultComparison = ") >= 0"; break;
      case BO_EQ: resultComparison = ") == 0"; break;
      case BO_NE: resultComparison = ") != 0"; break;
      default: llvm_unreachable("Invalid comparison operator");
      }

      DiagRuntimeBehavior(Loc, 0,
        PDiag(diag::warn_stringcompare)
          << isa<ObjCEncodeExpr>(literalStringStripped)
          << literalString->getSourceRange());
    }
  }

  // C99 6.5.8p3 / C99 6.5.9p4
  if (LHS.get()->getType()->isArithmeticType() &&
      RHS.get()->getType()->isArithmeticType()) {
    UsualArithmeticConversions(LHS, RHS);
    if (LHS.isInvalid() || RHS.isInvalid())
      return QualType();
  }
  else {
    LHS = UsualUnaryConversions(LHS.take());
    if (LHS.isInvalid())
      return QualType();

    RHS = UsualUnaryConversions(RHS.take());
    if (RHS.isInvalid())
      return QualType();
  }

  LHSType = LHS.get()->getType();
  RHSType = RHS.get()->getType();

  // The result of comparisons is 'bool' in C++, 'int' in C.
  QualType ResultTy = Context.getLogicalOperationType();

  if (IsRelational) {
    if (LHSType->isRealType() && RHSType->isRealType())
      return ResultTy;
  } else {
    // Check for comparisons of floating point operands using != and ==.
    if (LHSType->hasFloatingRepresentation())
      CheckFloatComparison(Loc, LHS.get(), RHS.get());

    if (LHSType->isArithmeticType() && RHSType->isArithmeticType())
      return ResultTy;
  }

  bool LHSIsNull = LHS.get()->isNullPointerConstant(Context,
                                              Expr::NPC_ValueDependentIsNull);
  bool RHSIsNull = RHS.get()->isNullPointerConstant(Context,
                                              Expr::NPC_ValueDependentIsNull);

  // All of the following pointer-related warnings are GCC extensions, except
  // when handling null pointer constants. 
  if (LHSType->isPointerType() && RHSType->isPointerType()) { // C99 6.5.8p2
    QualType LCanPointeeTy =
      LHSType->castAs<PointerType>()->getPointeeType().getCanonicalType();
    QualType RCanPointeeTy =
      RHSType->castAs<PointerType>()->getPointeeType().getCanonicalType();

    if (getLangOptions().CPlusPlus) {
      if (LCanPointeeTy == RCanPointeeTy)
        return ResultTy;
      if (!IsRelational &&
          (LCanPointeeTy->isVoidType() || RCanPointeeTy->isVoidType())) {
        // Valid unless comparison between non-null pointer and function pointer
        // This is a gcc extension compatibility comparison.
        // In a SFINAE context, we treat this as a hard error to maintain
        // conformance with the C++ standard.
        if ((LCanPointeeTy->isFunctionType() || RCanPointeeTy->isFunctionType())
            && !LHSIsNull && !RHSIsNull) {
          diagnoseFunctionPointerToVoidComparison(
              *this, Loc, LHS, RHS, /*isError*/ isSFINAEContext());
          
          if (isSFINAEContext())
            return QualType();
          
          RHS = ImpCastExprToType(RHS.take(), LHSType, CK_BitCast);
          return ResultTy;
        }
      }

      if (convertPointersToCompositeType(*this, Loc, LHS, RHS))
        return QualType();
      else
        return ResultTy;
    }
    // C99 6.5.9p2 and C99 6.5.8p2
    if (Context.typesAreCompatible(LCanPointeeTy.getUnqualifiedType(),
                                   RCanPointeeTy.getUnqualifiedType())) {
      // Valid unless a relational comparison of function pointers
      if (IsRelational && LCanPointeeTy->isFunctionType()) {
        Diag(Loc, diag::ext_typecheck_ordered_comparison_of_function_pointers)
          << LHSType << RHSType << LHS.get()->getSourceRange()
          << RHS.get()->getSourceRange();
      }
    } else if (!IsRelational &&
               (LCanPointeeTy->isVoidType() || RCanPointeeTy->isVoidType())) {
      // Valid unless comparison between non-null pointer and function pointer
      if ((LCanPointeeTy->isFunctionType() || RCanPointeeTy->isFunctionType())
          && !LHSIsNull && !RHSIsNull)
        diagnoseFunctionPointerToVoidComparison(*this, Loc, LHS, RHS,
                                                /*isError*/false);
    } else {
      // Invalid
      diagnoseDistinctPointerComparison(*this, Loc, LHS, RHS, /*isError*/false);
    }
    if (LCanPointeeTy != RCanPointeeTy) {
      if (LHSIsNull && !RHSIsNull)
        LHS = ImpCastExprToType(LHS.take(), RHSType, CK_BitCast);
      else
        RHS = ImpCastExprToType(RHS.take(), LHSType, CK_BitCast);
    }
    return ResultTy;
  }

  if (getLangOptions().CPlusPlus) {
    // Comparison of nullptr_t with itself.
    if (LHSType->isNullPtrType() && RHSType->isNullPtrType())
      return ResultTy;
    
    // Comparison of pointers with null pointer constants and equality
    // comparisons of member pointers to null pointer constants.
    if (RHSIsNull &&
        ((LHSType->isAnyPointerType() || LHSType->isNullPtrType()) ||
         (!IsRelational && 
          (LHSType->isMemberPointerType() || LHSType->isBlockPointerType())))) {
      RHS = ImpCastExprToType(RHS.take(), LHSType, 
                        LHSType->isMemberPointerType()
                          ? CK_NullToMemberPointer
                          : CK_NullToPointer);
      return ResultTy;
    }
    if (LHSIsNull &&
        ((RHSType->isAnyPointerType() || RHSType->isNullPtrType()) ||
         (!IsRelational && 
          (RHSType->isMemberPointerType() || RHSType->isBlockPointerType())))) {
      LHS = ImpCastExprToType(LHS.take(), RHSType, 
                        RHSType->isMemberPointerType()
                          ? CK_NullToMemberPointer
                          : CK_NullToPointer);
      return ResultTy;
    }

    // Comparison of member pointers.
    if (!IsRelational &&
        LHSType->isMemberPointerType() && RHSType->isMemberPointerType()) {
      if (convertPointersToCompositeType(*this, Loc, LHS, RHS))
        return QualType();
      else
        return ResultTy;
    }

    // Handle scoped enumeration types specifically, since they don't promote
    // to integers.
    if (LHS.get()->getType()->isEnumeralType() &&
        Context.hasSameUnqualifiedType(LHS.get()->getType(),
                                       RHS.get()->getType()))
      return ResultTy;
  }

  // Handle block pointer types.
  if (!IsRelational && LHSType->isBlockPointerType() &&
      RHSType->isBlockPointerType()) {
    QualType lpointee = LHSType->castAs<BlockPointerType>()->getPointeeType();
    QualType rpointee = RHSType->castAs<BlockPointerType>()->getPointeeType();

    if (!LHSIsNull && !RHSIsNull &&
        !Context.typesAreCompatible(lpointee, rpointee)) {
      Diag(Loc, diag::err_typecheck_comparison_of_distinct_blocks)
        << LHSType << RHSType << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
    }
    RHS = ImpCastExprToType(RHS.take(), LHSType, CK_BitCast);
    return ResultTy;
  }

  // Allow block pointers to be compared with null pointer constants.
  if (!IsRelational
      && ((LHSType->isBlockPointerType() && RHSType->isPointerType())
          || (LHSType->isPointerType() && RHSType->isBlockPointerType()))) {
    if (!LHSIsNull && !RHSIsNull) {
      if (!((RHSType->isPointerType() && RHSType->castAs<PointerType>()
             ->getPointeeType()->isVoidType())
            || (LHSType->isPointerType() && LHSType->castAs<PointerType>()
                ->getPointeeType()->isVoidType())))
        Diag(Loc, diag::err_typecheck_comparison_of_distinct_blocks)
          << LHSType << RHSType << LHS.get()->getSourceRange()
          << RHS.get()->getSourceRange();
    }
    if (LHSIsNull && !RHSIsNull)
      LHS = ImpCastExprToType(LHS.take(), RHSType,
                              RHSType->isPointerType() ? CK_BitCast
                                : CK_AnyPointerToBlockPointerCast);
    else
      RHS = ImpCastExprToType(RHS.take(), LHSType,
                              LHSType->isPointerType() ? CK_BitCast
                                : CK_AnyPointerToBlockPointerCast);
    return ResultTy;
  }

  if (LHSType->isObjCObjectPointerType() ||
      RHSType->isObjCObjectPointerType()) {
    const PointerType *LPT = LHSType->getAs<PointerType>();
    const PointerType *RPT = RHSType->getAs<PointerType>();
    if (LPT || RPT) {
      bool LPtrToVoid = LPT ? LPT->getPointeeType()->isVoidType() : false;
      bool RPtrToVoid = RPT ? RPT->getPointeeType()->isVoidType() : false;

      if (!LPtrToVoid && !RPtrToVoid &&
          !Context.typesAreCompatible(LHSType, RHSType)) {
        diagnoseDistinctPointerComparison(*this, Loc, LHS, RHS,
                                          /*isError*/false);
      }
      if (LHSIsNull && !RHSIsNull)
        LHS = ImpCastExprToType(LHS.take(), RHSType,
                                RPT ? CK_BitCast :CK_CPointerToObjCPointerCast);
      else
        RHS = ImpCastExprToType(RHS.take(), LHSType,
                                LPT ? CK_BitCast :CK_CPointerToObjCPointerCast);
      return ResultTy;
    }
    if (LHSType->isObjCObjectPointerType() &&
        RHSType->isObjCObjectPointerType()) {
      if (!Context.areComparableObjCPointerTypes(LHSType, RHSType))
        diagnoseDistinctPointerComparison(*this, Loc, LHS, RHS,
                                          /*isError*/false);
      if (LHSIsNull && !RHSIsNull)
        LHS = ImpCastExprToType(LHS.take(), RHSType, CK_BitCast);
      else
        RHS = ImpCastExprToType(RHS.take(), LHSType, CK_BitCast);
      return ResultTy;
    }
  }
  if ((LHSType->isAnyPointerType() && RHSType->isIntegerType()) ||
      (LHSType->isIntegerType() && RHSType->isAnyPointerType())) {
    unsigned DiagID = 0;
    bool isError = false;
    if ((LHSIsNull && LHSType->isIntegerType()) ||
        (RHSIsNull && RHSType->isIntegerType())) {
      if (IsRelational && !getLangOptions().CPlusPlus)
        DiagID = diag::ext_typecheck_ordered_comparison_of_pointer_and_zero;
    } else if (IsRelational && !getLangOptions().CPlusPlus)
      DiagID = diag::ext_typecheck_ordered_comparison_of_pointer_integer;
    else if (getLangOptions().CPlusPlus) {
      DiagID = diag::err_typecheck_comparison_of_pointer_integer;
      isError = true;
    } else
      DiagID = diag::ext_typecheck_comparison_of_pointer_integer;

    if (DiagID) {
      Diag(Loc, DiagID)
        << LHSType << RHSType << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
      if (isError)
        return QualType();
    }
    
    if (LHSType->isIntegerType())
      LHS = ImpCastExprToType(LHS.take(), RHSType,
                        LHSIsNull ? CK_NullToPointer : CK_IntegralToPointer);
    else
      RHS = ImpCastExprToType(RHS.take(), LHSType,
                        RHSIsNull ? CK_NullToPointer : CK_IntegralToPointer);
    return ResultTy;
  }
  
  // Handle block pointers.
  if (!IsRelational && RHSIsNull
      && LHSType->isBlockPointerType() && RHSType->isIntegerType()) {
    RHS = ImpCastExprToType(RHS.take(), LHSType, CK_NullToPointer);
    return ResultTy;
  }
  if (!IsRelational && LHSIsNull
      && LHSType->isIntegerType() && RHSType->isBlockPointerType()) {
    LHS = ImpCastExprToType(LHS.take(), RHSType, CK_NullToPointer);
    return ResultTy;
  }

  return InvalidOperands(Loc, LHS, RHS);
}

/// CheckVectorCompareOperands - vector comparisons are a clang extension that
/// operates on extended vector types.  Instead of producing an IntTy result,
/// like a scalar comparison, a vector comparison produces a vector of integer
/// types.
QualType Sema::CheckVectorCompareOperands(ExprResult &LHS, ExprResult &RHS,
                                          SourceLocation Loc,
                                          bool IsRelational) {
  // Check to make sure we're operating on vectors of the same type and width,
  // Allowing one side to be a scalar of element type.
  QualType vType = CheckVectorOperands(LHS, RHS, Loc, /*isCompAssign*/false);
  if (vType.isNull())
    return vType;

  QualType LHSType = LHS.get()->getType();
  QualType RHSType = RHS.get()->getType();

  // If AltiVec, the comparison results in a numeric type, i.e.
  // bool for C++, int for C
  if (vType->getAs<VectorType>()->getVectorKind() == VectorType::AltiVecVector)
    return Context.getLogicalOperationType();

  // For non-floating point types, check for self-comparisons of the form
  // x == x, x != x, x < x, etc.  These always evaluate to a constant, and
  // often indicate logic errors in the program.
  if (!LHSType->hasFloatingRepresentation()) {
    if (DeclRefExpr* DRL = dyn_cast<DeclRefExpr>(LHS.get()->IgnoreParens()))
      if (DeclRefExpr* DRR = dyn_cast<DeclRefExpr>(RHS.get()->IgnoreParens()))
        if (DRL->getDecl() == DRR->getDecl())
          DiagRuntimeBehavior(Loc, 0,
                              PDiag(diag::warn_comparison_always)
                                << 0 // self-
                                << 2 // "a constant"
                              );
  }

  // Check for comparisons of floating point operands using != and ==.
  if (!IsRelational && LHSType->hasFloatingRepresentation()) {
    assert (RHSType->hasFloatingRepresentation());
    CheckFloatComparison(Loc, LHS.get(), RHS.get());
  }

  // Return the type for the comparison, which is the same as vector type for
  // integer vectors, or an integer type of identical size and number of
  // elements for floating point vectors.
  if (LHSType->hasIntegerRepresentation())
    return LHSType;

  const VectorType *VTy = LHSType->getAs<VectorType>();
  unsigned TypeSize = Context.getTypeSize(VTy->getElementType());
  if (TypeSize == Context.getTypeSize(Context.IntTy))
    return Context.getExtVectorType(Context.IntTy, VTy->getNumElements());
  if (TypeSize == Context.getTypeSize(Context.LongTy))
    return Context.getExtVectorType(Context.LongTy, VTy->getNumElements());

  assert(TypeSize == Context.getTypeSize(Context.LongLongTy) &&
         "Unhandled vector element size in vector compare");
  return Context.getExtVectorType(Context.LongLongTy, VTy->getNumElements());
}

inline QualType Sema::CheckBitwiseOperands(
  ExprResult &LHS, ExprResult &RHS, SourceLocation Loc, bool IsCompAssign) {
  checkArithmeticNull(*this, LHS, RHS, Loc, /*isCompare=*/false);

  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() &&
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckVectorOperands(LHS, RHS, Loc, IsCompAssign);
    
    return InvalidOperands(Loc, LHS, RHS);
  }

  ExprResult LHSResult = Owned(LHS), RHSResult = Owned(RHS);
  QualType compType = UsualArithmeticConversions(LHSResult, RHSResult,
                                                 IsCompAssign);
  if (LHSResult.isInvalid() || RHSResult.isInvalid())
    return QualType();
  LHS = LHSResult.take();
  RHS = RHSResult.take();

  if (LHS.get()->getType()->isIntegralOrUnscopedEnumerationType() &&
      RHS.get()->getType()->isIntegralOrUnscopedEnumerationType())
    return compType;
  return InvalidOperands(Loc, LHS, RHS);
}

inline QualType Sema::CheckLogicalOperands( // C99 6.5.[13,14]
  ExprResult &LHS, ExprResult &RHS, SourceLocation Loc, unsigned Opc) {
  
  // Diagnose cases where the user write a logical and/or but probably meant a
  // bitwise one.  We do this when the LHS is a non-bool integer and the RHS
  // is a constant.
  if (LHS.get()->getType()->isIntegerType() &&
      !LHS.get()->getType()->isBooleanType() &&
      RHS.get()->getType()->isIntegerType() && !RHS.get()->isValueDependent() &&
      // Don't warn in macros or template instantiations.
      !Loc.isMacroID() && ActiveTemplateInstantiations.empty()) {
    // If the RHS can be constant folded, and if it constant folds to something
    // that isn't 0 or 1 (which indicate a potential logical operation that
    // happened to fold to true/false) then warn.
    // Parens on the RHS are ignored.
    Expr::EvalResult Result;
    if (RHS.get()->Evaluate(Result, Context) && !Result.HasSideEffects)
      if ((getLangOptions().Bool && !RHS.get()->getType()->isBooleanType()) ||
          (Result.Val.getInt() != 0 && Result.Val.getInt() != 1)) {
        Diag(Loc, diag::warn_logical_instead_of_bitwise)
          << RHS.get()->getSourceRange()
          << (Opc == BO_LAnd ? "&&" : "||");
        // Suggest replacing the logical operator with the bitwise version
        Diag(Loc, diag::note_logical_instead_of_bitwise_change_operator)
            << (Opc == BO_LAnd ? "&" : "|")
            << FixItHint::CreateReplacement(SourceRange(
                Loc, Lexer::getLocForEndOfToken(Loc, 0, getSourceManager(),
                                                getLangOptions())),
                                            Opc == BO_LAnd ? "&" : "|");
        if (Opc == BO_LAnd)
          // Suggest replacing "Foo() && kNonZero" with "Foo()"
          Diag(Loc, diag::note_logical_instead_of_bitwise_remove_constant)
              << FixItHint::CreateRemoval(
                  SourceRange(
                      Lexer::getLocForEndOfToken(LHS.get()->getLocEnd(),
                                                 0, getSourceManager(),
                                                 getLangOptions()),
                      RHS.get()->getLocEnd()));
      }
  }
  
  if (!Context.getLangOptions().CPlusPlus) {
    LHS = UsualUnaryConversions(LHS.take());
    if (LHS.isInvalid())
      return QualType();

    RHS = UsualUnaryConversions(RHS.take());
    if (RHS.isInvalid())
      return QualType();

    if (!LHS.get()->getType()->isScalarType() ||
        !RHS.get()->getType()->isScalarType())
      return InvalidOperands(Loc, LHS, RHS);

    return Context.IntTy;
  }

  // The following is safe because we only use this method for
  // non-overloadable operands.

  // C++ [expr.log.and]p1
  // C++ [expr.log.or]p1
  // The operands are both contextually converted to type bool.
  ExprResult LHSRes = PerformContextuallyConvertToBool(LHS.get());
  if (LHSRes.isInvalid())
    return InvalidOperands(Loc, LHS, RHS);
  LHS = move(LHSRes);

  ExprResult RHSRes = PerformContextuallyConvertToBool(RHS.get());
  if (RHSRes.isInvalid())
    return InvalidOperands(Loc, LHS, RHS);
  RHS = move(RHSRes);

  // C++ [expr.log.and]p2
  // C++ [expr.log.or]p2
  // The result is a bool.
  return Context.BoolTy;
}

/// IsReadonlyProperty - Verify that otherwise a valid l-value expression
/// is a read-only property; return true if so. A readonly property expression
/// depends on various declarations and thus must be treated specially.
///
static bool IsReadonlyProperty(Expr *E, Sema &S) {
  if (E->getStmtClass() == Expr::ObjCPropertyRefExprClass) {
    const ObjCPropertyRefExpr* PropExpr = cast<ObjCPropertyRefExpr>(E);
    if (PropExpr->isImplicitProperty()) return false;

    ObjCPropertyDecl *PDecl = PropExpr->getExplicitProperty();
    QualType BaseType = PropExpr->isSuperReceiver() ? 
                            PropExpr->getSuperReceiverType() :  
                            PropExpr->getBase()->getType();
      
    if (const ObjCObjectPointerType *OPT =
          BaseType->getAsObjCInterfacePointerType())
      if (ObjCInterfaceDecl *IFace = OPT->getInterfaceDecl())
        if (S.isPropertyReadonly(PDecl, IFace))
          return true;
  }
  return false;
}

static bool IsConstProperty(Expr *E, Sema &S) {
  if (E->getStmtClass() == Expr::ObjCPropertyRefExprClass) {
    const ObjCPropertyRefExpr* PropExpr = cast<ObjCPropertyRefExpr>(E);
    if (PropExpr->isImplicitProperty()) return false;
    
    ObjCPropertyDecl *PDecl = PropExpr->getExplicitProperty();
    QualType T = PDecl->getType();
    if (T->isReferenceType())
      T = T->getAs<ReferenceType>()->getPointeeType();
    CanQualType CT = S.Context.getCanonicalType(T);
    return CT.isConstQualified();
  }
  return false;
}

static bool IsReadonlyMessage(Expr *E, Sema &S) {
  if (E->getStmtClass() != Expr::MemberExprClass) 
    return false;
  const MemberExpr *ME = cast<MemberExpr>(E);
  NamedDecl *Member = ME->getMemberDecl();
  if (isa<FieldDecl>(Member)) {
    Expr *Base = ME->getBase()->IgnoreParenImpCasts();
    if (Base->getStmtClass() != Expr::ObjCMessageExprClass)
      return false;
    return cast<ObjCMessageExpr>(Base)->getMethodDecl() != 0;
  }
  return false;
}

/// CheckForModifiableLvalue - Verify that E is a modifiable lvalue.  If not,
/// emit an error and return true.  If so, return false.
static bool CheckForModifiableLvalue(Expr *E, SourceLocation Loc, Sema &S) {
  SourceLocation OrigLoc = Loc;
  Expr::isModifiableLvalueResult IsLV = E->isModifiableLvalue(S.Context,
                                                              &Loc);
  if (IsLV == Expr::MLV_Valid && IsReadonlyProperty(E, S))
    IsLV = Expr::MLV_ReadonlyProperty;
  else if (Expr::MLV_ConstQualified && IsConstProperty(E, S))
    IsLV = Expr::MLV_Valid;
  else if (IsLV == Expr::MLV_ClassTemporary && IsReadonlyMessage(E, S))
    IsLV = Expr::MLV_InvalidMessageExpression;
  if (IsLV == Expr::MLV_Valid)
    return false;

  unsigned Diag = 0;
  bool NeedType = false;
  switch (IsLV) { // C99 6.5.16p2
  case Expr::MLV_ConstQualified:
    Diag = diag::err_typecheck_assign_const;

    // In ARC, use some specialized diagnostics for occasions where we
    // infer 'const'.  These are always pseudo-strong variables.
    if (S.getLangOptions().ObjCAutoRefCount) {
      DeclRefExpr *declRef = dyn_cast<DeclRefExpr>(E->IgnoreParenCasts());
      if (declRef && isa<VarDecl>(declRef->getDecl())) {
        VarDecl *var = cast<VarDecl>(declRef->getDecl());

        // Use the normal diagnostic if it's pseudo-__strong but the
        // user actually wrote 'const'.
        if (var->isARCPseudoStrong() &&
            (!var->getTypeSourceInfo() ||
             !var->getTypeSourceInfo()->getType().isConstQualified())) {
          // There are two pseudo-strong cases:
          //  - self
          ObjCMethodDecl *method = S.getCurMethodDecl();
          if (method && var == method->getSelfDecl())
            Diag = diag::err_typecheck_arr_assign_self;

          //  - fast enumeration variables
          else
            Diag = diag::err_typecheck_arr_assign_enumeration;

          SourceRange Assign;
          if (Loc != OrigLoc)
            Assign = SourceRange(OrigLoc, OrigLoc);
          S.Diag(Loc, Diag) << E->getSourceRange() << Assign;
          // We need to preserve the AST regardless, so migration tool 
          // can do its job.
          return false;
        }
      }
    }

    break;
  case Expr::MLV_ArrayType:
    Diag = diag::err_typecheck_array_not_modifiable_lvalue;
    NeedType = true;
    break;
  case Expr::MLV_NotObjectType:
    Diag = diag::err_typecheck_non_object_not_modifiable_lvalue;
    NeedType = true;
    break;
  case Expr::MLV_LValueCast:
    Diag = diag::err_typecheck_lvalue_casts_not_supported;
    break;
  case Expr::MLV_Valid:
    llvm_unreachable("did not take early return for MLV_Valid");
  case Expr::MLV_InvalidExpression:
  case Expr::MLV_MemberFunction:
  case Expr::MLV_ClassTemporary:
    Diag = diag::err_typecheck_expression_not_modifiable_lvalue;
    break;
  case Expr::MLV_IncompleteType:
  case Expr::MLV_IncompleteVoidType:
    return S.RequireCompleteType(Loc, E->getType(),
              S.PDiag(diag::err_typecheck_incomplete_type_not_modifiable_lvalue)
                  << E->getSourceRange());
  case Expr::MLV_DuplicateVectorComponents:
    Diag = diag::err_typecheck_duplicate_vector_components_not_mlvalue;
    break;
  case Expr::MLV_NotBlockQualified:
    Diag = diag::err_block_decl_ref_not_modifiable_lvalue;
    break;
  case Expr::MLV_ReadonlyProperty:
    Diag = diag::error_readonly_property_assignment;
    break;
  case Expr::MLV_NoSetterProperty:
    Diag = diag::error_nosetter_property_assignment;
    break;
  case Expr::MLV_InvalidMessageExpression:
    Diag = diag::error_readonly_message_assignment;
    break;
  case Expr::MLV_SubObjCPropertySetting:
    Diag = diag::error_no_subobject_property_setting;
    break;
  }

  SourceRange Assign;
  if (Loc != OrigLoc)
    Assign = SourceRange(OrigLoc, OrigLoc);
  if (NeedType)
    S.Diag(Loc, Diag) << E->getType() << E->getSourceRange() << Assign;
  else
    S.Diag(Loc, Diag) << E->getSourceRange() << Assign;
  return true;
}



// C99 6.5.16.1
QualType Sema::CheckAssignmentOperands(Expr *LHSExpr, ExprResult &RHS,
                                       SourceLocation Loc,
                                       QualType CompoundType) {
  // Verify that LHS is a modifiable lvalue, and emit error if not.
  if (CheckForModifiableLvalue(LHSExpr, Loc, *this))
    return QualType();

  QualType LHSType = LHSExpr->getType();
  QualType RHSType = CompoundType.isNull() ? RHS.get()->getType() :
                                             CompoundType;
  AssignConvertType ConvTy;
  if (CompoundType.isNull()) {
    QualType LHSTy(LHSType);
    // Simple assignment "x = y".
    if (LHSExpr->getObjectKind() == OK_ObjCProperty) {
      ExprResult LHSResult = Owned(LHSExpr);
      ConvertPropertyForLValue(LHSResult, RHS, LHSTy);
      if (LHSResult.isInvalid())
        return QualType();
      LHSExpr = LHSResult.take();
    }
    ConvTy = CheckSingleAssignmentConstraints(LHSTy, RHS);
    if (RHS.isInvalid())
      return QualType();
    // Special case of NSObject attributes on c-style pointer types.
    if (ConvTy == IncompatiblePointer &&
        ((Context.isObjCNSObjectType(LHSType) &&
          RHSType->isObjCObjectPointerType()) ||
         (Context.isObjCNSObjectType(RHSType) &&
          LHSType->isObjCObjectPointerType())))
      ConvTy = Compatible;

    if (ConvTy == Compatible &&
        getLangOptions().ObjCNonFragileABI &&
        LHSType->isObjCObjectType())
      Diag(Loc, diag::err_assignment_requires_nonfragile_object)
        << LHSType;

    // If the RHS is a unary plus or minus, check to see if they = and + are
    // right next to each other.  If so, the user may have typo'd "x =+ 4"
    // instead of "x += 4".
    Expr *RHSCheck = RHS.get();
    if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(RHSCheck))
      RHSCheck = ICE->getSubExpr();
    if (UnaryOperator *UO = dyn_cast<UnaryOperator>(RHSCheck)) {
      if ((UO->getOpcode() == UO_Plus ||
           UO->getOpcode() == UO_Minus) &&
          Loc.isFileID() && UO->getOperatorLoc().isFileID() &&
          // Only if the two operators are exactly adjacent.
          Loc.getLocWithOffset(1) == UO->getOperatorLoc() &&
          // And there is a space or other character before the subexpr of the
          // unary +/-.  We don't want to warn on "x=-1".
          Loc.getLocWithOffset(2) != UO->getSubExpr()->getLocStart() &&
          UO->getSubExpr()->getLocStart().isFileID()) {
        Diag(Loc, diag::warn_not_compound_assign)
          << (UO->getOpcode() == UO_Plus ? "+" : "-")
          << SourceRange(UO->getOperatorLoc(), UO->getOperatorLoc());
      }
    }

    if (ConvTy == Compatible) {
      if (LHSType.getObjCLifetime() == Qualifiers::OCL_Strong)
        checkRetainCycles(LHSExpr, RHS.get());
      else if (getLangOptions().ObjCAutoRefCount)
        checkUnsafeExprAssigns(Loc, LHSExpr, RHS.get());
    }
  } else {
    // Compound assignment "x += y"
    ConvTy = CheckAssignmentConstraints(Loc, LHSType, RHSType);
  }

  if (DiagnoseAssignmentResult(ConvTy, Loc, LHSType, RHSType,
                               RHS.get(), AA_Assigning))
    return QualType();

  CheckForNullPointerDereference(*this, LHSExpr);

  // C99 6.5.16p3: The type of an assignment expression is the type of the
  // left operand unless the left operand has qualified type, in which case
  // it is the unqualified version of the type of the left operand.
  // C99 6.5.16.1p2: In simple assignment, the value of the right operand
  // is converted to the type of the assignment expression (above).
  // C++ 5.17p1: the type of the assignment expression is that of its left
  // operand.
  return (getLangOptions().CPlusPlus
          ? LHSType : LHSType.getUnqualifiedType());
}

// C99 6.5.17
static QualType CheckCommaOperands(Sema &S, ExprResult &LHS, ExprResult &RHS,
                                   SourceLocation Loc) {
  S.DiagnoseUnusedExprResult(LHS.get());

  LHS = S.CheckPlaceholderExpr(LHS.take());
  RHS = S.CheckPlaceholderExpr(RHS.take());
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  // C's comma performs lvalue conversion (C99 6.3.2.1) on both its
  // operands, but not unary promotions.
  // C++'s comma does not do any conversions at all (C++ [expr.comma]p1).

  // So we treat the LHS as a ignored value, and in C++ we allow the
  // containing site to determine what should be done with the RHS.
  LHS = S.IgnoredValueConversions(LHS.take());
  if (LHS.isInvalid())
    return QualType();

  if (!S.getLangOptions().CPlusPlus) {
    RHS = S.DefaultFunctionArrayLvalueConversion(RHS.take());
    if (RHS.isInvalid())
      return QualType();
    if (!RHS.get()->getType()->isVoidType())
      S.RequireCompleteType(Loc, RHS.get()->getType(),
                            diag::err_incomplete_type);
  }

  return RHS.get()->getType();
}

/// CheckIncrementDecrementOperand - unlike most "Check" methods, this routine
/// doesn't need to call UsualUnaryConversions or UsualArithmeticConversions.
static QualType CheckIncrementDecrementOperand(Sema &S, Expr *Op,
                                               ExprValueKind &VK,
                                               SourceLocation OpLoc,
                                               bool IsInc, bool IsPrefix) {
  if (Op->isTypeDependent())
    return S.Context.DependentTy;

  QualType ResType = Op->getType();
  assert(!ResType.isNull() && "no type for increment/decrement expression");

  if (S.getLangOptions().CPlusPlus && ResType->isBooleanType()) {
    // Decrement of bool is not allowed.
    if (!IsInc) {
      S.Diag(OpLoc, diag::err_decrement_bool) << Op->getSourceRange();
      return QualType();
    }
    // Increment of bool sets it to true, but is deprecated.
    S.Diag(OpLoc, diag::warn_increment_bool) << Op->getSourceRange();
  } else if (ResType->isRealType()) {
    // OK!
  } else if (ResType->isAnyPointerType()) {
    // C99 6.5.2.4p2, 6.5.6p2
    if (!checkArithmeticOpPointerOperand(S, OpLoc, Op))
      return QualType();

    // Diagnose bad cases where we step over interface counts.
    else if (!checkArithmethicPointerOnNonFragileABI(S, OpLoc, Op))
      return QualType();
  } else if (ResType->isAnyComplexType()) {
    // C99 does not support ++/-- on complex types, we allow as an extension.
    S.Diag(OpLoc, diag::ext_integer_increment_complex)
      << ResType << Op->getSourceRange();
  } else if (ResType->isPlaceholderType()) {
    ExprResult PR = S.CheckPlaceholderExpr(Op);
    if (PR.isInvalid()) return QualType();
    return CheckIncrementDecrementOperand(S, PR.take(), VK, OpLoc,
                                          IsInc, IsPrefix);
  } else if (S.getLangOptions().AltiVec && ResType->isVectorType()) {
    // OK! ( C/C++ Language Extensions for CBEA(Version 2.6) 10.3 )
  } else {
    S.Diag(OpLoc, diag::err_typecheck_illegal_increment_decrement)
      << ResType << int(IsInc) << Op->getSourceRange();
    return QualType();
  }
  // At this point, we know we have a real, complex or pointer type.
  // Now make sure the operand is a modifiable lvalue.
  if (CheckForModifiableLvalue(Op, OpLoc, S))
    return QualType();
  // In C++, a prefix increment is the same type as the operand. Otherwise
  // (in C or with postfix), the increment is the unqualified type of the
  // operand.
  if (IsPrefix && S.getLangOptions().CPlusPlus) {
    VK = VK_LValue;
    return ResType;
  } else {
    VK = VK_RValue;
    return ResType.getUnqualifiedType();
  }
}

ExprResult Sema::ConvertPropertyForRValue(Expr *E) {
  assert(E->getValueKind() == VK_LValue &&
         E->getObjectKind() == OK_ObjCProperty);
  const ObjCPropertyRefExpr *PRE = E->getObjCProperty();

  QualType T = E->getType();
  QualType ReceiverType;
  if (PRE->isObjectReceiver())
    ReceiverType = PRE->getBase()->getType();
  else if (PRE->isSuperReceiver())
    ReceiverType = PRE->getSuperReceiverType();
  else
    ReceiverType = Context.getObjCInterfaceType(PRE->getClassReceiver());
    
  ExprValueKind VK = VK_RValue;
  if (PRE->isImplicitProperty()) {
    if (ObjCMethodDecl *GetterMethod = 
          PRE->getImplicitPropertyGetter()) {
      T = getMessageSendResultType(ReceiverType, GetterMethod, 
                                   PRE->isClassReceiver(), 
                                   PRE->isSuperReceiver());
      VK = Expr::getValueKindForType(GetterMethod->getResultType());
    }
    else {
      Diag(PRE->getLocation(), diag::err_getter_not_found)
            << PRE->getBase()->getType();
    }
  }
  else {
    // lvalue-ness of an explicit property is determined by
    // getter type.
    QualType ResT = PRE->getGetterResultType();
    VK = Expr::getValueKindForType(ResT);
  }
    
  E = ImplicitCastExpr::Create(Context, T, CK_GetObjCProperty,
                               E, 0, VK);
  
  ExprResult Result = MaybeBindToTemporary(E);
  if (!Result.isInvalid())
    E = Result.take();

  return Owned(E);
}

void Sema::ConvertPropertyForLValue(ExprResult &LHS, ExprResult &RHS,
                                    QualType &LHSTy) {
  assert(LHS.get()->getValueKind() == VK_LValue &&
         LHS.get()->getObjectKind() == OK_ObjCProperty);
  const ObjCPropertyRefExpr *PropRef = LHS.get()->getObjCProperty();

  bool Consumed = false;

  if (PropRef->isImplicitProperty()) {
    // If using property-dot syntax notation for assignment, and there is a
    // setter, RHS expression is being passed to the setter argument. So,
    // type conversion (and comparison) is RHS to setter's argument type.
    if (const ObjCMethodDecl *SetterMD = PropRef->getImplicitPropertySetter()) {
      ObjCMethodDecl::param_const_iterator P = SetterMD->param_begin();
      LHSTy = (*P)->getType();
      Consumed = (getLangOptions().ObjCAutoRefCount &&
                  (*P)->hasAttr<NSConsumedAttr>());

    // Otherwise, if the getter returns an l-value, just call that.
    } else {
      QualType Result = PropRef->getImplicitPropertyGetter()->getResultType();
      ExprValueKind VK = Expr::getValueKindForType(Result);
      if (VK == VK_LValue) {
        LHS = ImplicitCastExpr::Create(Context, LHS.get()->getType(),
                                        CK_GetObjCProperty, LHS.take(), 0, VK);
        return;
      }
    }
  } else if (getLangOptions().ObjCAutoRefCount) {
    const ObjCMethodDecl *setter
      = PropRef->getExplicitProperty()->getSetterMethodDecl();
    if (setter) {
      ObjCMethodDecl::param_const_iterator P = setter->param_begin();
      LHSTy = (*P)->getType();
      Consumed = (*P)->hasAttr<NSConsumedAttr>();
    }
  }

  if ((getLangOptions().CPlusPlus && LHSTy->isRecordType()) ||
      getLangOptions().ObjCAutoRefCount) {
    InitializedEntity Entity = 
      InitializedEntity::InitializeParameter(Context, LHSTy, Consumed);
    ExprResult ArgE = PerformCopyInitialization(Entity, SourceLocation(), RHS);
    if (!ArgE.isInvalid()) {
      RHS = ArgE;
      if (getLangOptions().ObjCAutoRefCount && !PropRef->isSuperReceiver())
        checkRetainCycles(const_cast<Expr*>(PropRef->getBase()), RHS.get());
    }
  }
}
  

/// getPrimaryDecl - Helper function for CheckAddressOfOperand().
/// This routine allows us to typecheck complex/recursive expressions
/// where the declaration is needed for type checking. We only need to
/// handle cases when the expression references a function designator
/// or is an lvalue. Here are some examples:
///  - &(x) => x
///  - &*****f => f for f a function designator.
///  - &s.xx => s
///  - &s.zz[1].yy -> s, if zz is an array
///  - *(x + 1) -> x, if x is an array
///  - &"123"[2] -> 0
///  - & __real__ x -> x
static ValueDecl *getPrimaryDecl(Expr *E) {
  switch (E->getStmtClass()) {
  case Stmt::DeclRefExprClass:
    return cast<DeclRefExpr>(E)->getDecl();
  case Stmt::MemberExprClass:
    // If this is an arrow operator, the address is an offset from
    // the base's value, so the object the base refers to is
    // irrelevant.
    if (cast<MemberExpr>(E)->isArrow())
      return 0;
    // Otherwise, the expression refers to a part of the base
    return getPrimaryDecl(cast<MemberExpr>(E)->getBase());
  case Stmt::ArraySubscriptExprClass: {
    // FIXME: This code shouldn't be necessary!  We should catch the implicit
    // promotion of register arrays earlier.
    Expr* Base = cast<ArraySubscriptExpr>(E)->getBase();
    if (ImplicitCastExpr* ICE = dyn_cast<ImplicitCastExpr>(Base)) {
      if (ICE->getSubExpr()->getType()->isArrayType())
        return getPrimaryDecl(ICE->getSubExpr());
    }
    return 0;
  }
  case Stmt::UnaryOperatorClass: {
    UnaryOperator *UO = cast<UnaryOperator>(E);

    switch(UO->getOpcode()) {
    case UO_Real:
    case UO_Imag:
    case UO_Extension:
      return getPrimaryDecl(UO->getSubExpr());
    default:
      return 0;
    }
  }
  case Stmt::ParenExprClass:
    return getPrimaryDecl(cast<ParenExpr>(E)->getSubExpr());
  case Stmt::ImplicitCastExprClass:
    // If the result of an implicit cast is an l-value, we care about
    // the sub-expression; otherwise, the result here doesn't matter.
    return getPrimaryDecl(cast<ImplicitCastExpr>(E)->getSubExpr());
  default:
    return 0;
  }
}

namespace {
  enum {
    AO_Bit_Field = 0,
    AO_Vector_Element = 1,
    AO_Property_Expansion = 2,
    AO_Register_Variable = 3,
    AO_No_Error = 4
  };
}
/// \brief Diagnose invalid operand for address of operations.
///
/// \param Type The type of operand which cannot have its address taken.
static void diagnoseAddressOfInvalidType(Sema &S, SourceLocation Loc,
                                         Expr *E, unsigned Type) {
  S.Diag(Loc, diag::err_typecheck_address_of) << Type << E->getSourceRange();
}

/// CheckAddressOfOperand - The operand of & must be either a function
/// designator or an lvalue designating an object. If it is an lvalue, the
/// object cannot be declared with storage class register or be a bit field.
/// Note: The usual conversions are *not* applied to the operand of the &
/// operator (C99 6.3.2.1p[2-4]), and its result is never an lvalue.
/// In C++, the operand might be an overloaded function name, in which case
/// we allow the '&' but retain the overloaded-function type.
static QualType CheckAddressOfOperand(Sema &S, Expr *OrigOp,
                                      SourceLocation OpLoc) {
  if (OrigOp->isTypeDependent())
    return S.Context.DependentTy;
  if (OrigOp->getType() == S.Context.OverloadTy) {
    if (!isa<OverloadExpr>(OrigOp->IgnoreParens())) {
      S.Diag(OpLoc, diag::err_typecheck_invalid_lvalue_addrof)
        << OrigOp->getSourceRange();
      return QualType();
    }
                  
    return S.Context.OverloadTy;
  }
  if (OrigOp->getType() == S.Context.UnknownAnyTy)
    return S.Context.UnknownAnyTy;
  if (OrigOp->getType() == S.Context.BoundMemberTy) {
    S.Diag(OpLoc, diag::err_invalid_form_pointer_member_function)
      << OrigOp->getSourceRange();
    return QualType();
  }

  assert(!OrigOp->getType()->isPlaceholderType());

  // Make sure to ignore parentheses in subsequent checks
  Expr *op = OrigOp->IgnoreParens();

  if (S.getLangOptions().C99) {
    // Implement C99-only parts of addressof rules.
    if (UnaryOperator* uOp = dyn_cast<UnaryOperator>(op)) {
      if (uOp->getOpcode() == UO_Deref)
        // Per C99 6.5.3.2, the address of a deref always returns a valid result
        // (assuming the deref expression is valid).
        return uOp->getSubExpr()->getType();
    }
    // Technically, there should be a check for array subscript
    // expressions here, but the result of one is always an lvalue anyway.
  }
  ValueDecl *dcl = getPrimaryDecl(op);
  Expr::LValueClassification lval = op->ClassifyLValue(S.Context);
  unsigned AddressOfError = AO_No_Error;

  if (lval == Expr::LV_ClassTemporary) { 
    bool sfinae = S.isSFINAEContext();
    S.Diag(OpLoc, sfinae ? diag::err_typecheck_addrof_class_temporary
                         : diag::ext_typecheck_addrof_class_temporary)
      << op->getType() << op->getSourceRange();
    if (sfinae)
      return QualType();
  } else if (isa<ObjCSelectorExpr>(op)) {
    return S.Context.getPointerType(op->getType());
  } else if (lval == Expr::LV_MemberFunction) {
    // If it's an instance method, make a member pointer.
    // The expression must have exactly the form &A::foo.

    // If the underlying expression isn't a decl ref, give up.
    if (!isa<DeclRefExpr>(op)) {
      S.Diag(OpLoc, diag::err_invalid_form_pointer_member_function)
        << OrigOp->getSourceRange();
      return QualType();
    }
    DeclRefExpr *DRE = cast<DeclRefExpr>(op);
    CXXMethodDecl *MD = cast<CXXMethodDecl>(DRE->getDecl());

    // The id-expression was parenthesized.
    if (OrigOp != DRE) {
      S.Diag(OpLoc, diag::err_parens_pointer_member_function)
        << OrigOp->getSourceRange();

    // The method was named without a qualifier.
    } else if (!DRE->getQualifier()) {
      S.Diag(OpLoc, diag::err_unqualified_pointer_member_function)
        << op->getSourceRange();
    }

    return S.Context.getMemberPointerType(op->getType(),
              S.Context.getTypeDeclType(MD->getParent()).getTypePtr());
  } else if (lval != Expr::LV_Valid && lval != Expr::LV_IncompleteVoidType) {
    // C99 6.5.3.2p1
    // The operand must be either an l-value or a function designator
    if (!op->getType()->isFunctionType()) {
      // FIXME: emit more specific diag...
      S.Diag(OpLoc, diag::err_typecheck_invalid_lvalue_addrof)
        << op->getSourceRange();
      return QualType();
    }
  } else if (op->getObjectKind() == OK_BitField) { // C99 6.5.3.2p1
    // The operand cannot be a bit-field
    AddressOfError = AO_Bit_Field;
  } else if (op->getObjectKind() == OK_VectorComponent) {
    // The operand cannot be an element of a vector
    AddressOfError = AO_Vector_Element;
  } else if (op->getObjectKind() == OK_ObjCProperty) {
    // cannot take address of a property expression.
    AddressOfError = AO_Property_Expansion;
  } else if (dcl) { // C99 6.5.3.2p1
    // We have an lvalue with a decl. Make sure the decl is not declared
    // with the register storage-class specifier.
    if (const VarDecl *vd = dyn_cast<VarDecl>(dcl)) {
      // in C++ it is not error to take address of a register
      // variable (c++03 7.1.1P3)
      if (vd->getStorageClass() == SC_Register &&
          !S.getLangOptions().CPlusPlus) {
        AddressOfError = AO_Register_Variable;
      }
    } else if (isa<FunctionTemplateDecl>(dcl)) {
      return S.Context.OverloadTy;
    } else if (isa<FieldDecl>(dcl) || isa<IndirectFieldDecl>(dcl)) {
      // Okay: we can take the address of a field.
      // Could be a pointer to member, though, if there is an explicit
      // scope qualifier for the class.
      if (isa<DeclRefExpr>(op) && cast<DeclRefExpr>(op)->getQualifier()) {
        DeclContext *Ctx = dcl->getDeclContext();
        if (Ctx && Ctx->isRecord()) {
          if (dcl->getType()->isReferenceType()) {
            S.Diag(OpLoc,
                   diag::err_cannot_form_pointer_to_member_of_reference_type)
              << dcl->getDeclName() << dcl->getType();
            return QualType();
          }

          while (cast<RecordDecl>(Ctx)->isAnonymousStructOrUnion())
            Ctx = Ctx->getParent();
          return S.Context.getMemberPointerType(op->getType(),
                S.Context.getTypeDeclType(cast<RecordDecl>(Ctx)).getTypePtr());
        }
      }
    } else if (!isa<FunctionDecl>(dcl) && !isa<NonTypeTemplateParmDecl>(dcl))
      llvm_unreachable("Unknown/unexpected decl type");
  }

  if (AddressOfError != AO_No_Error) {
    diagnoseAddressOfInvalidType(S, OpLoc, op, AddressOfError);
    return QualType();
  }

  if (lval == Expr::LV_IncompleteVoidType) {
    // Taking the address of a void variable is technically illegal, but we
    // allow it in cases which are otherwise valid.
    // Example: "extern void x; void* y = &x;".
    S.Diag(OpLoc, diag::ext_typecheck_addrof_void) << op->getSourceRange();
  }

  // If the operand has type "type", the result has type "pointer to type".
  if (op->getType()->isObjCObjectType())
    return S.Context.getObjCObjectPointerType(op->getType());
  return S.Context.getPointerType(op->getType());
}

/// CheckIndirectionOperand - Type check unary indirection (prefix '*').
static QualType CheckIndirectionOperand(Sema &S, Expr *Op, ExprValueKind &VK,
                                        SourceLocation OpLoc) {
  if (Op->isTypeDependent())
    return S.Context.DependentTy;

  ExprResult ConvResult = S.UsualUnaryConversions(Op);
  if (ConvResult.isInvalid())
    return QualType();
  Op = ConvResult.take();
  QualType OpTy = Op->getType();
  QualType Result;

  if (isa<CXXReinterpretCastExpr>(Op)) {
    QualType OpOrigType = Op->IgnoreParenCasts()->getType();
    S.CheckCompatibleReinterpretCast(OpOrigType, OpTy, /*IsDereference*/true,
                                     Op->getSourceRange());
  }

  // Note that per both C89 and C99, indirection is always legal, even if OpTy
  // is an incomplete type or void.  It would be possible to warn about
  // dereferencing a void pointer, but it's completely well-defined, and such a
  // warning is unlikely to catch any mistakes.
  if (const PointerType *PT = OpTy->getAs<PointerType>())
    Result = PT->getPointeeType();
  else if (const ObjCObjectPointerType *OPT =
             OpTy->getAs<ObjCObjectPointerType>())
    Result = OPT->getPointeeType();
  else {
    ExprResult PR = S.CheckPlaceholderExpr(Op);
    if (PR.isInvalid()) return QualType();
    if (PR.take() != Op)
      return CheckIndirectionOperand(S, PR.take(), VK, OpLoc);
  }

  if (Result.isNull()) {
    S.Diag(OpLoc, diag::err_typecheck_indirection_requires_pointer)
      << OpTy << Op->getSourceRange();
    return QualType();
  }

  // Dereferences are usually l-values...
  VK = VK_LValue;

  // ...except that certain expressions are never l-values in C.
  if (!S.getLangOptions().CPlusPlus && Result.isCForbiddenLValueType())
    VK = VK_RValue;
  
  return Result;
}

static inline BinaryOperatorKind ConvertTokenKindToBinaryOpcode(
  tok::TokenKind Kind) {
  BinaryOperatorKind Opc;
  switch (Kind) {
  default: llvm_unreachable("Unknown binop!");
  case tok::periodstar:           Opc = BO_PtrMemD; break;
  case tok::arrowstar:            Opc = BO_PtrMemI; break;
  case tok::star:                 Opc = BO_Mul; break;
  case tok::slash:                Opc = BO_Div; break;
  case tok::percent:              Opc = BO_Rem; break;
  case tok::plus:                 Opc = BO_Add; break;
  case tok::minus:                Opc = BO_Sub; break;
  case tok::lessless:             Opc = BO_Shl; break;
  case tok::greatergreater:       Opc = BO_Shr; break;
  case tok::lessequal:            Opc = BO_LE; break;
  case tok::less:                 Opc = BO_LT; break;
  case tok::greaterequal:         Opc = BO_GE; break;
  case tok::greater:              Opc = BO_GT; break;
  case tok::exclaimequal:         Opc = BO_NE; break;
  case tok::equalequal:           Opc = BO_EQ; break;
  case tok::amp:                  Opc = BO_And; break;
  case tok::caret:                Opc = BO_Xor; break;
  case tok::pipe:                 Opc = BO_Or; break;
  case tok::ampamp:               Opc = BO_LAnd; break;
  case tok::pipepipe:             Opc = BO_LOr; break;
  case tok::equal:                Opc = BO_Assign; break;
  case tok::starequal:            Opc = BO_MulAssign; break;
  case tok::slashequal:           Opc = BO_DivAssign; break;
  case tok::percentequal:         Opc = BO_RemAssign; break;
  case tok::plusequal:            Opc = BO_AddAssign; break;
  case tok::minusequal:           Opc = BO_SubAssign; break;
  case tok::lesslessequal:        Opc = BO_ShlAssign; break;
  case tok::greatergreaterequal:  Opc = BO_ShrAssign; break;
  case tok::ampequal:             Opc = BO_AndAssign; break;
  case tok::caretequal:           Opc = BO_XorAssign; break;
  case tok::pipeequal:            Opc = BO_OrAssign; break;
  case tok::comma:                Opc = BO_Comma; break;
  }
  return Opc;
}

static inline UnaryOperatorKind ConvertTokenKindToUnaryOpcode(
  tok::TokenKind Kind) {
  UnaryOperatorKind Opc;
  switch (Kind) {
  default: llvm_unreachable("Unknown unary op!");
  case tok::plusplus:     Opc = UO_PreInc; break;
  case tok::minusminus:   Opc = UO_PreDec; break;
  case tok::amp:          Opc = UO_AddrOf; break;
  case tok::star:         Opc = UO_Deref; break;
  case tok::plus:         Opc = UO_Plus; break;
  case tok::minus:        Opc = UO_Minus; break;
  case tok::tilde:        Opc = UO_Not; break;
  case tok::exclaim:      Opc = UO_LNot; break;
  case tok::kw___real:    Opc = UO_Real; break;
  case tok::kw___imag:    Opc = UO_Imag; break;
  case tok::kw___extension__: Opc = UO_Extension; break;
  }
  return Opc;
}

/// DiagnoseSelfAssignment - Emits a warning if a value is assigned to itself.
/// This warning is only emitted for builtin assignment operations. It is also
/// suppressed in the event of macro expansions.
static void DiagnoseSelfAssignment(Sema &S, Expr *LHSExpr, Expr *RHSExpr,
                                   SourceLocation OpLoc) {
  if (!S.ActiveTemplateInstantiations.empty())
    return;
  if (OpLoc.isInvalid() || OpLoc.isMacroID())
    return;
  LHSExpr = LHSExpr->IgnoreParenImpCasts();
  RHSExpr = RHSExpr->IgnoreParenImpCasts();
  const DeclRefExpr *LHSDeclRef = dyn_cast<DeclRefExpr>(LHSExpr);
  const DeclRefExpr *RHSDeclRef = dyn_cast<DeclRefExpr>(RHSExpr);
  if (!LHSDeclRef || !RHSDeclRef ||
      LHSDeclRef->getLocation().isMacroID() ||
      RHSDeclRef->getLocation().isMacroID())
    return;
  const ValueDecl *LHSDecl =
    cast<ValueDecl>(LHSDeclRef->getDecl()->getCanonicalDecl());
  const ValueDecl *RHSDecl =
    cast<ValueDecl>(RHSDeclRef->getDecl()->getCanonicalDecl());
  if (LHSDecl != RHSDecl)
    return;
  if (LHSDecl->getType().isVolatileQualified())
    return;
  if (const ReferenceType *RefTy = LHSDecl->getType()->getAs<ReferenceType>())
    if (RefTy->getPointeeType().isVolatileQualified())
      return;

  S.Diag(OpLoc, diag::warn_self_assignment)
      << LHSDeclRef->getType()
      << LHSExpr->getSourceRange() << RHSExpr->getSourceRange();
}

/// CreateBuiltinBinOp - Creates a new built-in binary operation with
/// operator @p Opc at location @c TokLoc. This routine only supports
/// built-in operations; ActOnBinOp handles overloaded operators.
ExprResult Sema::CreateBuiltinBinOp(SourceLocation OpLoc,
                                    BinaryOperatorKind Opc,
                                    Expr *LHSExpr, Expr *RHSExpr) {
  ExprResult LHS = Owned(LHSExpr), RHS = Owned(RHSExpr);
  QualType ResultTy;     // Result type of the binary operator.
  // The following two variables are used for compound assignment operators
  QualType CompLHSTy;    // Type of LHS after promotions for computation
  QualType CompResultTy; // Type of computation result
  ExprValueKind VK = VK_RValue;
  ExprObjectKind OK = OK_Ordinary;

  // Check if a 'foo<int>' involved in a binary op, identifies a single 
  // function unambiguously (i.e. an lvalue ala 13.4)
  // But since an assignment can trigger target based overload, exclude it in 
  // our blind search. i.e:
  // template<class T> void f(); template<class T, class U> void f(U);
  // f<int> == 0;  // resolve f<int> blindly
  // void (*p)(int); p = f<int>;  // resolve f<int> using target
  if (Opc != BO_Assign) { 
    ExprResult resolvedLHS = CheckPlaceholderExpr(LHS.get());
    if (!resolvedLHS.isUsable()) return ExprError();
    LHS = move(resolvedLHS);

    ExprResult resolvedRHS = CheckPlaceholderExpr(RHS.get());
    if (!resolvedRHS.isUsable()) return ExprError();
    RHS = move(resolvedRHS);
  }

  switch (Opc) {
  case BO_Assign:
    ResultTy = CheckAssignmentOperands(LHS.get(), RHS, OpLoc, QualType());
    if (getLangOptions().CPlusPlus &&
        LHS.get()->getObjectKind() != OK_ObjCProperty) {
      VK = LHS.get()->getValueKind();
      OK = LHS.get()->getObjectKind();
    }
    if (!ResultTy.isNull())
      DiagnoseSelfAssignment(*this, LHS.get(), RHS.get(), OpLoc);
    break;
  case BO_PtrMemD:
  case BO_PtrMemI:
    ResultTy = CheckPointerToMemberOperands(LHS, RHS, VK, OpLoc,
                                            Opc == BO_PtrMemI);
    break;
  case BO_Mul:
  case BO_Div:
    ResultTy = CheckMultiplyDivideOperands(LHS, RHS, OpLoc, false,
                                           Opc == BO_Div);
    break;
  case BO_Rem:
    ResultTy = CheckRemainderOperands(LHS, RHS, OpLoc);
    break;
  case BO_Add:
    ResultTy = CheckAdditionOperands(LHS, RHS, OpLoc);
    break;
  case BO_Sub:
    ResultTy = CheckSubtractionOperands(LHS, RHS, OpLoc);
    break;
  case BO_Shl:
  case BO_Shr:
    ResultTy = CheckShiftOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_LE:
  case BO_LT:
  case BO_GE:
  case BO_GT:
    ResultTy = CheckCompareOperands(LHS, RHS, OpLoc, Opc, true);
    break;
  case BO_EQ:
  case BO_NE:
    ResultTy = CheckCompareOperands(LHS, RHS, OpLoc, Opc, false);
    break;
  case BO_And:
  case BO_Xor:
  case BO_Or:
    ResultTy = CheckBitwiseOperands(LHS, RHS, OpLoc);
    break;
  case BO_LAnd:
  case BO_LOr:
    ResultTy = CheckLogicalOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_MulAssign:
  case BO_DivAssign:
    CompResultTy = CheckMultiplyDivideOperands(LHS, RHS, OpLoc, true,
                                               Opc == BO_DivAssign);
    CompLHSTy = CompResultTy;
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy = CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy);
    break;
  case BO_RemAssign:
    CompResultTy = CheckRemainderOperands(LHS, RHS, OpLoc, true);
    CompLHSTy = CompResultTy;
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy = CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy);
    break;
  case BO_AddAssign:
    CompResultTy = CheckAdditionOperands(LHS, RHS, OpLoc, &CompLHSTy);
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy = CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy);
    break;
  case BO_SubAssign:
    CompResultTy = CheckSubtractionOperands(LHS, RHS, OpLoc, &CompLHSTy);
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy = CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy);
    break;
  case BO_ShlAssign:
  case BO_ShrAssign:
    CompResultTy = CheckShiftOperands(LHS, RHS, OpLoc, Opc, true);
    CompLHSTy = CompResultTy;
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy = CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy);
    break;
  case BO_AndAssign:
  case BO_XorAssign:
  case BO_OrAssign:
    CompResultTy = CheckBitwiseOperands(LHS, RHS, OpLoc, true);
    CompLHSTy = CompResultTy;
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy = CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy);
    break;
  case BO_Comma:
    ResultTy = CheckCommaOperands(*this, LHS, RHS, OpLoc);
    if (getLangOptions().CPlusPlus && !RHS.isInvalid()) {
      VK = RHS.get()->getValueKind();
      OK = RHS.get()->getObjectKind();
    }
    break;
  }
  if (ResultTy.isNull() || LHS.isInvalid() || RHS.isInvalid())
    return ExprError();

  // Check for array bounds violations for both sides of the BinaryOperator
  CheckArrayAccess(LHS.get());
  CheckArrayAccess(RHS.get());

  if (CompResultTy.isNull())
    return Owned(new (Context) BinaryOperator(LHS.take(), RHS.take(), Opc,
                                              ResultTy, VK, OK, OpLoc));
  if (getLangOptions().CPlusPlus && LHS.get()->getObjectKind() !=
      OK_ObjCProperty) {
    VK = VK_LValue;
    OK = LHS.get()->getObjectKind();
  }
  return Owned(new (Context) CompoundAssignOperator(LHS.take(), RHS.take(), Opc,
                                                    ResultTy, VK, OK, CompLHSTy,
                                                    CompResultTy, OpLoc));
}

/// DiagnoseBitwisePrecedence - Emit a warning when bitwise and comparison
/// operators are mixed in a way that suggests that the programmer forgot that
/// comparison operators have higher precedence. The most typical example of
/// such code is "flags & 0x0020 != 0", which is equivalent to "flags & 1".
static void DiagnoseBitwisePrecedence(Sema &Self, BinaryOperatorKind Opc,
                                      SourceLocation OpLoc, Expr *LHSExpr,
                                      Expr *RHSExpr) {
  typedef BinaryOperator BinOp;
  BinOp::Opcode LHSopc = static_cast<BinOp::Opcode>(-1),
                RHSopc = static_cast<BinOp::Opcode>(-1);
  if (BinOp *BO = dyn_cast<BinOp>(LHSExpr))
    LHSopc = BO->getOpcode();
  if (BinOp *BO = dyn_cast<BinOp>(RHSExpr))
    RHSopc = BO->getOpcode();

  // Subs are not binary operators.
  if (LHSopc == -1 && RHSopc == -1)
    return;

  // Bitwise operations are sometimes used as eager logical ops.
  // Don't diagnose this.
  if ((BinOp::isComparisonOp(LHSopc) || BinOp::isBitwiseOp(LHSopc)) &&
      (BinOp::isComparisonOp(RHSopc) || BinOp::isBitwiseOp(RHSopc)))
    return;

  bool isLeftComp = BinOp::isComparisonOp(LHSopc);
  bool isRightComp = BinOp::isComparisonOp(RHSopc);
  if (!isLeftComp && !isRightComp) return;

  SourceRange DiagRange = isLeftComp ? SourceRange(LHSExpr->getLocStart(),
                                                   OpLoc)
                                     : SourceRange(OpLoc, RHSExpr->getLocEnd());
  std::string OpStr = isLeftComp ? BinOp::getOpcodeStr(LHSopc)
                                 : BinOp::getOpcodeStr(RHSopc);
  SourceRange ParensRange = isLeftComp ?
      SourceRange(cast<BinOp>(LHSExpr)->getRHS()->getLocStart(),
                  RHSExpr->getLocEnd())
    : SourceRange(LHSExpr->getLocStart(),
                  cast<BinOp>(RHSExpr)->getLHS()->getLocStart());

  Self.Diag(OpLoc, diag::warn_precedence_bitwise_rel)
    << DiagRange << BinOp::getOpcodeStr(Opc) << OpStr;
  SuggestParentheses(Self, OpLoc,
    Self.PDiag(diag::note_precedence_bitwise_silence) << OpStr,
    RHSExpr->getSourceRange());
  SuggestParentheses(Self, OpLoc,
    Self.PDiag(diag::note_precedence_bitwise_first) << BinOp::getOpcodeStr(Opc),
    ParensRange);
}

/// \brief It accepts a '&' expr that is inside a '|' one.
/// Emit a diagnostic together with a fixit hint that wraps the '&' expression
/// in parentheses.
static void
EmitDiagnosticForBitwiseAndInBitwiseOr(Sema &Self, SourceLocation OpLoc,
                                       BinaryOperator *Bop) {
  assert(Bop->getOpcode() == BO_And);
  Self.Diag(Bop->getOperatorLoc(), diag::warn_bitwise_and_in_bitwise_or)
      << Bop->getSourceRange() << OpLoc;
  SuggestParentheses(Self, Bop->getOperatorLoc(),
    Self.PDiag(diag::note_bitwise_and_in_bitwise_or_silence),
    Bop->getSourceRange());
}

/// \brief It accepts a '&&' expr that is inside a '||' one.
/// Emit a diagnostic together with a fixit hint that wraps the '&&' expression
/// in parentheses.
static void
EmitDiagnosticForLogicalAndInLogicalOr(Sema &Self, SourceLocation OpLoc,
                                       BinaryOperator *Bop) {
  assert(Bop->getOpcode() == BO_LAnd);
  Self.Diag(Bop->getOperatorLoc(), diag::warn_logical_and_in_logical_or)
      << Bop->getSourceRange() << OpLoc;
  SuggestParentheses(Self, Bop->getOperatorLoc(),
    Self.PDiag(diag::note_logical_and_in_logical_or_silence),
    Bop->getSourceRange());
}

/// \brief Returns true if the given expression can be evaluated as a constant
/// 'true'.
static bool EvaluatesAsTrue(Sema &S, Expr *E) {
  bool Res;
  return E->EvaluateAsBooleanCondition(Res, S.getASTContext()) && Res;
}

/// \brief Returns true if the given expression can be evaluated as a constant
/// 'false'.
static bool EvaluatesAsFalse(Sema &S, Expr *E) {
  bool Res;
  return E->EvaluateAsBooleanCondition(Res, S.getASTContext()) && !Res;
}

/// \brief Look for '&&' in the left hand of a '||' expr.
static void DiagnoseLogicalAndInLogicalOrLHS(Sema &S, SourceLocation OpLoc,
                                             Expr *LHSExpr, Expr *RHSExpr) {
  if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(LHSExpr)) {
    if (Bop->getOpcode() == BO_LAnd) {
      // If it's "a && b || 0" don't warn since the precedence doesn't matter.
      if (EvaluatesAsFalse(S, RHSExpr))
        return;
      // If it's "1 && a || b" don't warn since the precedence doesn't matter.
      if (!EvaluatesAsTrue(S, Bop->getLHS()))
        return EmitDiagnosticForLogicalAndInLogicalOr(S, OpLoc, Bop);
    } else if (Bop->getOpcode() == BO_LOr) {
      if (BinaryOperator *RBop = dyn_cast<BinaryOperator>(Bop->getRHS())) {
        // If it's "a || b && 1 || c" we didn't warn earlier for
        // "a || b && 1", but warn now.
        if (RBop->getOpcode() == BO_LAnd && EvaluatesAsTrue(S, RBop->getRHS()))
          return EmitDiagnosticForLogicalAndInLogicalOr(S, OpLoc, RBop);
      }
    }
  }
}

/// \brief Look for '&&' in the right hand of a '||' expr.
static void DiagnoseLogicalAndInLogicalOrRHS(Sema &S, SourceLocation OpLoc,
                                             Expr *LHSExpr, Expr *RHSExpr) {
  if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(RHSExpr)) {
    if (Bop->getOpcode() == BO_LAnd) {
      // If it's "0 || a && b" don't warn since the precedence doesn't matter.
      if (EvaluatesAsFalse(S, LHSExpr))
        return;
      // If it's "a || b && 1" don't warn since the precedence doesn't matter.
      if (!EvaluatesAsTrue(S, Bop->getRHS()))
        return EmitDiagnosticForLogicalAndInLogicalOr(S, OpLoc, Bop);
    }
  }
}

/// \brief Look for '&' in the left or right hand of a '|' expr.
static void DiagnoseBitwiseAndInBitwiseOr(Sema &S, SourceLocation OpLoc,
                                             Expr *OrArg) {
  if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(OrArg)) {
    if (Bop->getOpcode() == BO_And)
      return EmitDiagnosticForBitwiseAndInBitwiseOr(S, OpLoc, Bop);
  }
}

/// DiagnoseBinOpPrecedence - Emit warnings for expressions with tricky
/// precedence.
static void DiagnoseBinOpPrecedence(Sema &Self, BinaryOperatorKind Opc,
                                    SourceLocation OpLoc, Expr *LHSExpr,
                                    Expr *RHSExpr){
  // Diagnose "arg1 'bitwise' arg2 'eq' arg3".
  if (BinaryOperator::isBitwiseOp(Opc))
    DiagnoseBitwisePrecedence(Self, Opc, OpLoc, LHSExpr, RHSExpr);

  // Diagnose "arg1 & arg2 | arg3"
  if (Opc == BO_Or && !OpLoc.isMacroID()/* Don't warn in macros. */) {
    DiagnoseBitwiseAndInBitwiseOr(Self, OpLoc, LHSExpr);
    DiagnoseBitwiseAndInBitwiseOr(Self, OpLoc, RHSExpr);
  }

  // Warn about arg1 || arg2 && arg3, as GCC 4.3+ does.
  // We don't warn for 'assert(a || b && "bad")' since this is safe.
  if (Opc == BO_LOr && !OpLoc.isMacroID()/* Don't warn in macros. */) {
    DiagnoseLogicalAndInLogicalOrLHS(Self, OpLoc, LHSExpr, RHSExpr);
    DiagnoseLogicalAndInLogicalOrRHS(Self, OpLoc, LHSExpr, RHSExpr);
  }
}

// Binary Operators.  'Tok' is the token for the operator.
ExprResult Sema::ActOnBinOp(Scope *S, SourceLocation TokLoc,
                            tok::TokenKind Kind,
                            Expr *LHSExpr, Expr *RHSExpr) {
  BinaryOperatorKind Opc = ConvertTokenKindToBinaryOpcode(Kind);
  assert((LHSExpr != 0) && "ActOnBinOp(): missing left expression");
  assert((RHSExpr != 0) && "ActOnBinOp(): missing right expression");

  // Emit warnings for tricky precedence issues, e.g. "bitfield & 0x4 == 0"
  DiagnoseBinOpPrecedence(*this, Opc, TokLoc, LHSExpr, RHSExpr);

  return BuildBinOp(S, TokLoc, Opc, LHSExpr, RHSExpr);
}

ExprResult Sema::BuildBinOp(Scope *S, SourceLocation OpLoc,
                            BinaryOperatorKind Opc,
                            Expr *LHSExpr, Expr *RHSExpr) {
  if (getLangOptions().CPlusPlus) {
    bool UseBuiltinOperator;

    if (LHSExpr->isTypeDependent() || RHSExpr->isTypeDependent()) {
      UseBuiltinOperator = false;
    } else if (Opc == BO_Assign &&
               LHSExpr->getObjectKind() == OK_ObjCProperty) {
      UseBuiltinOperator = true;
    } else {
      UseBuiltinOperator = !LHSExpr->getType()->isOverloadableType() &&
                           !RHSExpr->getType()->isOverloadableType();
    }

    if (!UseBuiltinOperator) {
      // Find all of the overloaded operators visible from this
      // point. We perform both an operator-name lookup from the local
      // scope and an argument-dependent lookup based on the types of
      // the arguments.
      UnresolvedSet<16> Functions;
      OverloadedOperatorKind OverOp
        = BinaryOperator::getOverloadedOperator(Opc);
      if (S && OverOp != OO_None)
        LookupOverloadedOperatorName(OverOp, S, LHSExpr->getType(),
                                     RHSExpr->getType(), Functions);

      // Build the (potentially-overloaded, potentially-dependent)
      // binary operation.
      return CreateOverloadedBinOp(OpLoc, Opc, Functions, LHSExpr, RHSExpr);
    }
  }

  // Build a built-in binary operation.
  return CreateBuiltinBinOp(OpLoc, Opc, LHSExpr, RHSExpr);
}

ExprResult Sema::CreateBuiltinUnaryOp(SourceLocation OpLoc,
                                      UnaryOperatorKind Opc,
                                      Expr *InputExpr) {
  ExprResult Input = Owned(InputExpr);
  ExprValueKind VK = VK_RValue;
  ExprObjectKind OK = OK_Ordinary;
  QualType resultType;
  switch (Opc) {
  case UO_PreInc:
  case UO_PreDec:
  case UO_PostInc:
  case UO_PostDec:
    resultType = CheckIncrementDecrementOperand(*this, Input.get(), VK, OpLoc,
                                                Opc == UO_PreInc ||
                                                Opc == UO_PostInc,
                                                Opc == UO_PreInc ||
                                                Opc == UO_PreDec);
    break;
  case UO_AddrOf:
    resultType = CheckAddressOfOperand(*this, Input.get(), OpLoc);
    break;
  case UO_Deref: {
    ExprResult resolved = CheckPlaceholderExpr(Input.get());
    if (!resolved.isUsable()) return ExprError();
    Input = move(resolved);
    Input = DefaultFunctionArrayLvalueConversion(Input.take());
    resultType = CheckIndirectionOperand(*this, Input.get(), VK, OpLoc);
    break;
  }
  case UO_Plus:
  case UO_Minus:
    Input = UsualUnaryConversions(Input.take());
    if (Input.isInvalid()) return ExprError();
    resultType = Input.get()->getType();
    if (resultType->isDependentType())
      break;
    if (resultType->isArithmeticType() || // C99 6.5.3.3p1
        resultType->isVectorType()) 
      break;
    else if (getLangOptions().CPlusPlus && // C++ [expr.unary.op]p6-7
             resultType->isEnumeralType())
      break;
    else if (getLangOptions().CPlusPlus && // C++ [expr.unary.op]p6
             Opc == UO_Plus &&
             resultType->isPointerType())
      break;
    else if (resultType->isPlaceholderType()) {
      Input = CheckPlaceholderExpr(Input.take());
      if (Input.isInvalid()) return ExprError();
      return CreateBuiltinUnaryOp(OpLoc, Opc, Input.take());
    }

    return ExprError(Diag(OpLoc, diag::err_typecheck_unary_expr)
      << resultType << Input.get()->getSourceRange());

  case UO_Not: // bitwise complement
    Input = UsualUnaryConversions(Input.take());
    if (Input.isInvalid()) return ExprError();
    resultType = Input.get()->getType();
    if (resultType->isDependentType())
      break;
    // C99 6.5.3.3p1. We allow complex int and float as a GCC extension.
    if (resultType->isComplexType() || resultType->isComplexIntegerType())
      // C99 does not support '~' for complex conjugation.
      Diag(OpLoc, diag::ext_integer_complement_complex)
        << resultType << Input.get()->getSourceRange();
    else if (resultType->hasIntegerRepresentation())
      break;
    else if (resultType->isPlaceholderType()) {
      Input = CheckPlaceholderExpr(Input.take());
      if (Input.isInvalid()) return ExprError();
      return CreateBuiltinUnaryOp(OpLoc, Opc, Input.take());
    } else {
      return ExprError(Diag(OpLoc, diag::err_typecheck_unary_expr)
        << resultType << Input.get()->getSourceRange());
    }
    break;

  case UO_LNot: // logical negation
    // Unlike +/-/~, integer promotions aren't done here (C99 6.5.3.3p5).
    Input = DefaultFunctionArrayLvalueConversion(Input.take());
    if (Input.isInvalid()) return ExprError();
    resultType = Input.get()->getType();

    // Though we still have to promote half FP to float...
    if (resultType->isHalfType()) {
      Input = ImpCastExprToType(Input.take(), Context.FloatTy, CK_FloatingCast).take();
      resultType = Context.FloatTy;
    }

    if (resultType->isDependentType())
      break;
    if (resultType->isScalarType()) {
      // C99 6.5.3.3p1: ok, fallthrough;
      if (Context.getLangOptions().CPlusPlus) {
        // C++03 [expr.unary.op]p8, C++0x [expr.unary.op]p9:
        // operand contextually converted to bool.
        Input = ImpCastExprToType(Input.take(), Context.BoolTy,
                                  ScalarTypeToBooleanCastKind(resultType));
      }
    } else if (resultType->isPlaceholderType()) {
      Input = CheckPlaceholderExpr(Input.take());
      if (Input.isInvalid()) return ExprError();
      return CreateBuiltinUnaryOp(OpLoc, Opc, Input.take());
    } else {
      return ExprError(Diag(OpLoc, diag::err_typecheck_unary_expr)
        << resultType << Input.get()->getSourceRange());
    }
    
    // LNot always has type int. C99 6.5.3.3p5.
    // In C++, it's bool. C++ 5.3.1p8
    resultType = Context.getLogicalOperationType();
    break;
  case UO_Real:
  case UO_Imag:
    resultType = CheckRealImagOperand(*this, Input, OpLoc, Opc == UO_Real);
    // _Real and _Imag map ordinary l-values into ordinary l-values.
    if (Input.isInvalid()) return ExprError();
    if (Input.get()->getValueKind() != VK_RValue &&
        Input.get()->getObjectKind() == OK_Ordinary)
      VK = Input.get()->getValueKind();
    break;
  case UO_Extension:
    resultType = Input.get()->getType();
    VK = Input.get()->getValueKind();
    OK = Input.get()->getObjectKind();
    break;
  }
  if (resultType.isNull() || Input.isInvalid())
    return ExprError();

  // Check for array bounds violations in the operand of the UnaryOperator,
  // except for the '*' and '&' operators that have to be handled specially
  // by CheckArrayAccess (as there are special cases like &array[arraysize]
  // that are explicitly defined as valid by the standard).
  if (Opc != UO_AddrOf && Opc != UO_Deref)
    CheckArrayAccess(Input.get());

  return Owned(new (Context) UnaryOperator(Input.take(), Opc, resultType,
                                           VK, OK, OpLoc));
}

ExprResult Sema::BuildUnaryOp(Scope *S, SourceLocation OpLoc,
                              UnaryOperatorKind Opc, Expr *Input) {
  if (getLangOptions().CPlusPlus && Input->getType()->isOverloadableType() &&
      UnaryOperator::getOverloadedOperator(Opc) != OO_None) {
    // Find all of the overloaded operators visible from this
    // point. We perform both an operator-name lookup from the local
    // scope and an argument-dependent lookup based on the types of
    // the arguments.
    UnresolvedSet<16> Functions;
    OverloadedOperatorKind OverOp = UnaryOperator::getOverloadedOperator(Opc);
    if (S && OverOp != OO_None)
      LookupOverloadedOperatorName(OverOp, S, Input->getType(), QualType(),
                                   Functions);

    return CreateOverloadedUnaryOp(OpLoc, Opc, Functions, Input);
  }

  return CreateBuiltinUnaryOp(OpLoc, Opc, Input);
}

// Unary Operators.  'Tok' is the token for the operator.
ExprResult Sema::ActOnUnaryOp(Scope *S, SourceLocation OpLoc,
                              tok::TokenKind Op, Expr *Input) {
  return BuildUnaryOp(S, OpLoc, ConvertTokenKindToUnaryOpcode(Op), Input);
}

/// ActOnAddrLabel - Parse the GNU address of label extension: "&&foo".
ExprResult Sema::ActOnAddrLabel(SourceLocation OpLoc, SourceLocation LabLoc,
                                LabelDecl *TheDecl) {
  TheDecl->setUsed();
  // Create the AST node.  The address of a label always has type 'void*'.
  return Owned(new (Context) AddrLabelExpr(OpLoc, LabLoc, TheDecl,
                                       Context.getPointerType(Context.VoidTy)));
}

/// Given the last statement in a statement-expression, check whether
/// the result is a producing expression (like a call to an
/// ns_returns_retained function) and, if so, rebuild it to hoist the
/// release out of the full-expression.  Otherwise, return null.
/// Cannot fail.
static Expr *maybeRebuildARCConsumingStmt(Stmt *Statement) {
  // Should always be wrapped with one of these.
  ExprWithCleanups *cleanups = dyn_cast<ExprWithCleanups>(Statement);
  if (!cleanups) return 0;

  ImplicitCastExpr *cast = dyn_cast<ImplicitCastExpr>(cleanups->getSubExpr());
  if (!cast || cast->getCastKind() != CK_ARCConsumeObject)
    return 0;

  // Splice out the cast.  This shouldn't modify any interesting
  // features of the statement.
  Expr *producer = cast->getSubExpr();
  assert(producer->getType() == cast->getType());
  assert(producer->getValueKind() == cast->getValueKind());
  cleanups->setSubExpr(producer);
  return cleanups;
}

ExprResult
Sema::ActOnStmtExpr(SourceLocation LPLoc, Stmt *SubStmt,
                    SourceLocation RPLoc) { // "({..})"
  assert(SubStmt && isa<CompoundStmt>(SubStmt) && "Invalid action invocation!");
  CompoundStmt *Compound = cast<CompoundStmt>(SubStmt);

  bool isFileScope
    = (getCurFunctionOrMethodDecl() == 0) && (getCurBlock() == 0);
  if (isFileScope)
    return ExprError(Diag(LPLoc, diag::err_stmtexpr_file_scope));

  // FIXME: there are a variety of strange constraints to enforce here, for
  // example, it is not possible to goto into a stmt expression apparently.
  // More semantic analysis is needed.

  // If there are sub stmts in the compound stmt, take the type of the last one
  // as the type of the stmtexpr.
  QualType Ty = Context.VoidTy;
  bool StmtExprMayBindToTemp = false;
  if (!Compound->body_empty()) {
    Stmt *LastStmt = Compound->body_back();
    LabelStmt *LastLabelStmt = 0;
    // If LastStmt is a label, skip down through into the body.
    while (LabelStmt *Label = dyn_cast<LabelStmt>(LastStmt)) {
      LastLabelStmt = Label;
      LastStmt = Label->getSubStmt();
    }

    if (Expr *LastE = dyn_cast<Expr>(LastStmt)) {
      // Do function/array conversion on the last expression, but not
      // lvalue-to-rvalue.  However, initialize an unqualified type.
      ExprResult LastExpr = DefaultFunctionArrayConversion(LastE);
      if (LastExpr.isInvalid())
        return ExprError();
      Ty = LastExpr.get()->getType().getUnqualifiedType();

      if (!Ty->isDependentType() && !LastExpr.get()->isTypeDependent()) {
        // In ARC, if the final expression ends in a consume, splice
        // the consume out and bind it later.  In the alternate case
        // (when dealing with a retainable type), the result
        // initialization will create a produce.  In both cases the
        // result will be +1, and we'll need to balance that out with
        // a bind.
        if (Expr *rebuiltLastStmt
              = maybeRebuildARCConsumingStmt(LastExpr.get())) {
          LastExpr = rebuiltLastStmt;
        } else {
          LastExpr = PerformCopyInitialization(
                            InitializedEntity::InitializeResult(LPLoc, 
                                                                Ty,
                                                                false),
                                                   SourceLocation(),
                                               LastExpr);
        }

        if (LastExpr.isInvalid())
          return ExprError();
        if (LastExpr.get() != 0) {
          if (!LastLabelStmt)
            Compound->setLastStmt(LastExpr.take());
          else
            LastLabelStmt->setSubStmt(LastExpr.take());
          StmtExprMayBindToTemp = true;
        }
      }
    }
  }

  // FIXME: Check that expression type is complete/non-abstract; statement
  // expressions are not lvalues.
  Expr *ResStmtExpr = new (Context) StmtExpr(Compound, Ty, LPLoc, RPLoc);
  if (StmtExprMayBindToTemp)
    return MaybeBindToTemporary(ResStmtExpr);
  return Owned(ResStmtExpr);
}

ExprResult Sema::BuildBuiltinOffsetOf(SourceLocation BuiltinLoc,
                                      TypeSourceInfo *TInfo,
                                      OffsetOfComponent *CompPtr,
                                      unsigned NumComponents,
                                      SourceLocation RParenLoc) {
  QualType ArgTy = TInfo->getType();
  bool Dependent = ArgTy->isDependentType();
  SourceRange TypeRange = TInfo->getTypeLoc().getLocalSourceRange();
  
  // We must have at least one component that refers to the type, and the first
  // one is known to be a field designator.  Verify that the ArgTy represents
  // a struct/union/class.
  if (!Dependent && !ArgTy->isRecordType())
    return ExprError(Diag(BuiltinLoc, diag::err_offsetof_record_type) 
                       << ArgTy << TypeRange);
  
  // Type must be complete per C99 7.17p3 because a declaring a variable
  // with an incomplete type would be ill-formed.
  if (!Dependent 
      && RequireCompleteType(BuiltinLoc, ArgTy,
                             PDiag(diag::err_offsetof_incomplete_type)
                               << TypeRange))
    return ExprError();
  
  // offsetof with non-identifier designators (e.g. "offsetof(x, a.b[c])") are a
  // GCC extension, diagnose them.
  // FIXME: This diagnostic isn't actually visible because the location is in
  // a system header!
  if (NumComponents != 1)
    Diag(BuiltinLoc, diag::ext_offsetof_extended_field_designator)
      << SourceRange(CompPtr[1].LocStart, CompPtr[NumComponents-1].LocEnd);
  
  bool DidWarnAboutNonPOD = false;
  QualType CurrentType = ArgTy;
  typedef OffsetOfExpr::OffsetOfNode OffsetOfNode;
  SmallVector<OffsetOfNode, 4> Comps;
  SmallVector<Expr*, 4> Exprs;
  for (unsigned i = 0; i != NumComponents; ++i) {
    const OffsetOfComponent &OC = CompPtr[i];
    if (OC.isBrackets) {
      // Offset of an array sub-field.  TODO: Should we allow vector elements?
      if (!CurrentType->isDependentType()) {
        const ArrayType *AT = Context.getAsArrayType(CurrentType);
        if(!AT)
          return ExprError(Diag(OC.LocEnd, diag::err_offsetof_array_type)
                           << CurrentType);
        CurrentType = AT->getElementType();
      } else
        CurrentType = Context.DependentTy;
      
      // The expression must be an integral expression.
      // FIXME: An integral constant expression?
      Expr *Idx = static_cast<Expr*>(OC.U.E);
      if (!Idx->isTypeDependent() && !Idx->isValueDependent() &&
          !Idx->getType()->isIntegerType())
        return ExprError(Diag(Idx->getLocStart(),
                              diag::err_typecheck_subscript_not_integer)
                         << Idx->getSourceRange());
      
      // Record this array index.
      Comps.push_back(OffsetOfNode(OC.LocStart, Exprs.size(), OC.LocEnd));
      Exprs.push_back(Idx);
      continue;
    }
    
    // Offset of a field.
    if (CurrentType->isDependentType()) {
      // We have the offset of a field, but we can't look into the dependent
      // type. Just record the identifier of the field.
      Comps.push_back(OffsetOfNode(OC.LocStart, OC.U.IdentInfo, OC.LocEnd));
      CurrentType = Context.DependentTy;
      continue;
    }
    
    // We need to have a complete type to look into.
    if (RequireCompleteType(OC.LocStart, CurrentType,
                            diag::err_offsetof_incomplete_type))
      return ExprError();
    
    // Look for the designated field.
    const RecordType *RC = CurrentType->getAs<RecordType>();
    if (!RC) 
      return ExprError(Diag(OC.LocEnd, diag::err_offsetof_record_type)
                       << CurrentType);
    RecordDecl *RD = RC->getDecl();
    
    // C++ [lib.support.types]p5:
    //   The macro offsetof accepts a restricted set of type arguments in this
    //   International Standard. type shall be a POD structure or a POD union
    //   (clause 9).
    if (CXXRecordDecl *CRD = dyn_cast<CXXRecordDecl>(RD)) {
      if (!CRD->isPOD() && !DidWarnAboutNonPOD &&
          DiagRuntimeBehavior(BuiltinLoc, 0,
                              PDiag(diag::warn_offsetof_non_pod_type)
                              << SourceRange(CompPtr[0].LocStart, OC.LocEnd)
                              << CurrentType))
        DidWarnAboutNonPOD = true;
    }
    
    // Look for the field.
    LookupResult R(*this, OC.U.IdentInfo, OC.LocStart, LookupMemberName);
    LookupQualifiedName(R, RD);
    FieldDecl *MemberDecl = R.getAsSingle<FieldDecl>();
    IndirectFieldDecl *IndirectMemberDecl = 0;
    if (!MemberDecl) {
      if ((IndirectMemberDecl = R.getAsSingle<IndirectFieldDecl>()))
        MemberDecl = IndirectMemberDecl->getAnonField();
    }

    if (!MemberDecl)
      return ExprError(Diag(BuiltinLoc, diag::err_no_member)
                       << OC.U.IdentInfo << RD << SourceRange(OC.LocStart, 
                                                              OC.LocEnd));
    
    // C99 7.17p3:
    //   (If the specified member is a bit-field, the behavior is undefined.)
    //
    // We diagnose this as an error.
    if (MemberDecl->isBitField()) {
      Diag(OC.LocEnd, diag::err_offsetof_bitfield)
        << MemberDecl->getDeclName()
        << SourceRange(BuiltinLoc, RParenLoc);
      Diag(MemberDecl->getLocation(), diag::note_bitfield_decl);
      return ExprError();
    }

    RecordDecl *Parent = MemberDecl->getParent();
    if (IndirectMemberDecl)
      Parent = cast<RecordDecl>(IndirectMemberDecl->getDeclContext());

    // If the member was found in a base class, introduce OffsetOfNodes for
    // the base class indirections.
    CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                       /*DetectVirtual=*/false);
    if (IsDerivedFrom(CurrentType, Context.getTypeDeclType(Parent), Paths)) {
      CXXBasePath &Path = Paths.front();
      for (CXXBasePath::iterator B = Path.begin(), BEnd = Path.end();
           B != BEnd; ++B)
        Comps.push_back(OffsetOfNode(B->Base));
    }

    if (IndirectMemberDecl) {
      for (IndirectFieldDecl::chain_iterator FI =
           IndirectMemberDecl->chain_begin(),
           FEnd = IndirectMemberDecl->chain_end(); FI != FEnd; FI++) {
        assert(isa<FieldDecl>(*FI));
        Comps.push_back(OffsetOfNode(OC.LocStart,
                                     cast<FieldDecl>(*FI), OC.LocEnd));
      }
    } else
      Comps.push_back(OffsetOfNode(OC.LocStart, MemberDecl, OC.LocEnd));

    CurrentType = MemberDecl->getType().getNonReferenceType(); 
  }
  
  return Owned(OffsetOfExpr::Create(Context, Context.getSizeType(), BuiltinLoc, 
                                    TInfo, Comps.data(), Comps.size(),
                                    Exprs.data(), Exprs.size(), RParenLoc));  
}

ExprResult Sema::ActOnBuiltinOffsetOf(Scope *S,
                                      SourceLocation BuiltinLoc,
                                      SourceLocation TypeLoc,
                                      ParsedType ParsedArgTy,
                                      OffsetOfComponent *CompPtr,
                                      unsigned NumComponents,
                                      SourceLocation RParenLoc) {
  
  TypeSourceInfo *ArgTInfo;
  QualType ArgTy = GetTypeFromParser(ParsedArgTy, &ArgTInfo);
  if (ArgTy.isNull())
    return ExprError();

  if (!ArgTInfo)
    ArgTInfo = Context.getTrivialTypeSourceInfo(ArgTy, TypeLoc);

  return BuildBuiltinOffsetOf(BuiltinLoc, ArgTInfo, CompPtr, NumComponents, 
                              RParenLoc);
}


ExprResult Sema::ActOnChooseExpr(SourceLocation BuiltinLoc,
                                 Expr *CondExpr,
                                 Expr *LHSExpr, Expr *RHSExpr,
                                 SourceLocation RPLoc) {
  assert((CondExpr && LHSExpr && RHSExpr) && "Missing type argument(s)");

  ExprValueKind VK = VK_RValue;
  ExprObjectKind OK = OK_Ordinary;
  QualType resType;
  bool ValueDependent = false;
  if (CondExpr->isTypeDependent() || CondExpr->isValueDependent()) {
    resType = Context.DependentTy;
    ValueDependent = true;
  } else {
    // The conditional expression is required to be a constant expression.
    llvm::APSInt condEval(32);
    SourceLocation ExpLoc;
    if (!CondExpr->isIntegerConstantExpr(condEval, Context, &ExpLoc))
      return ExprError(Diag(ExpLoc,
                       diag::err_typecheck_choose_expr_requires_constant)
        << CondExpr->getSourceRange());

    // If the condition is > zero, then the AST type is the same as the LSHExpr.
    Expr *ActiveExpr = condEval.getZExtValue() ? LHSExpr : RHSExpr;

    resType = ActiveExpr->getType();
    ValueDependent = ActiveExpr->isValueDependent();
    VK = ActiveExpr->getValueKind();
    OK = ActiveExpr->getObjectKind();
  }

  return Owned(new (Context) ChooseExpr(BuiltinLoc, CondExpr, LHSExpr, RHSExpr,
                                        resType, VK, OK, RPLoc,
                                        resType->isDependentType(),
                                        ValueDependent));
}

//===----------------------------------------------------------------------===//
// Clang Extensions.
//===----------------------------------------------------------------------===//

/// ActOnBlockStart - This callback is invoked when a block literal is started.
void Sema::ActOnBlockStart(SourceLocation CaretLoc, Scope *CurScope) {
  BlockDecl *Block = BlockDecl::Create(Context, CurContext, CaretLoc);
  PushBlockScope(CurScope, Block);
  CurContext->addDecl(Block);
  if (CurScope)
    PushDeclContext(CurScope, Block);
  else
    CurContext = Block;
}

void Sema::ActOnBlockArguments(Declarator &ParamInfo, Scope *CurScope) {
  assert(ParamInfo.getIdentifier()==0 && "block-id should have no identifier!");
  assert(ParamInfo.getContext() == Declarator::BlockLiteralContext);
  BlockScopeInfo *CurBlock = getCurBlock();

  TypeSourceInfo *Sig = GetTypeForDeclarator(ParamInfo, CurScope);
  QualType T = Sig->getType();

  // GetTypeForDeclarator always produces a function type for a block
  // literal signature.  Furthermore, it is always a FunctionProtoType
  // unless the function was written with a typedef.
  assert(T->isFunctionType() &&
         "GetTypeForDeclarator made a non-function block signature");

  // Look for an explicit signature in that function type.
  FunctionProtoTypeLoc ExplicitSignature;

  TypeLoc tmp = Sig->getTypeLoc().IgnoreParens();
  if (isa<FunctionProtoTypeLoc>(tmp)) {
    ExplicitSignature = cast<FunctionProtoTypeLoc>(tmp);

    // Check whether that explicit signature was synthesized by
    // GetTypeForDeclarator.  If so, don't save that as part of the
    // written signature.
    if (ExplicitSignature.getLocalRangeBegin() ==
        ExplicitSignature.getLocalRangeEnd()) {
      // This would be much cheaper if we stored TypeLocs instead of
      // TypeSourceInfos.
      TypeLoc Result = ExplicitSignature.getResultLoc();
      unsigned Size = Result.getFullDataSize();
      Sig = Context.CreateTypeSourceInfo(Result.getType(), Size);
      Sig->getTypeLoc().initializeFullCopy(Result, Size);

      ExplicitSignature = FunctionProtoTypeLoc();
    }
  }

  CurBlock->TheDecl->setSignatureAsWritten(Sig);
  CurBlock->FunctionType = T;

  const FunctionType *Fn = T->getAs<FunctionType>();
  QualType RetTy = Fn->getResultType();
  bool isVariadic =
    (isa<FunctionProtoType>(Fn) && cast<FunctionProtoType>(Fn)->isVariadic());

  CurBlock->TheDecl->setIsVariadic(isVariadic);

  // Don't allow returning a objc interface by value.
  if (RetTy->isObjCObjectType()) {
    Diag(ParamInfo.getSourceRange().getBegin(),
         diag::err_object_cannot_be_passed_returned_by_value) << 0 << RetTy;
    return;
  }

  // Context.DependentTy is used as a placeholder for a missing block
  // return type.  TODO:  what should we do with declarators like:
  //   ^ * { ... }
  // If the answer is "apply template argument deduction"....
  if (RetTy != Context.DependentTy)
    CurBlock->ReturnType = RetTy;

  // Push block parameters from the declarator if we had them.
  SmallVector<ParmVarDecl*, 8> Params;
  if (ExplicitSignature) {
    for (unsigned I = 0, E = ExplicitSignature.getNumArgs(); I != E; ++I) {
      ParmVarDecl *Param = ExplicitSignature.getArg(I);
      if (Param->getIdentifier() == 0 &&
          !Param->isImplicit() &&
          !Param->isInvalidDecl() &&
          !getLangOptions().CPlusPlus)
        Diag(Param->getLocation(), diag::err_parameter_name_omitted);
      Params.push_back(Param);
    }

  // Fake up parameter variables if we have a typedef, like
  //   ^ fntype { ... }
  } else if (const FunctionProtoType *Fn = T->getAs<FunctionProtoType>()) {
    for (FunctionProtoType::arg_type_iterator
           I = Fn->arg_type_begin(), E = Fn->arg_type_end(); I != E; ++I) {
      ParmVarDecl *Param =
        BuildParmVarDeclForTypedef(CurBlock->TheDecl,
                                   ParamInfo.getSourceRange().getBegin(),
                                   *I);
      Params.push_back(Param);
    }
  }

  // Set the parameters on the block decl.
  if (!Params.empty()) {
    CurBlock->TheDecl->setParams(Params);
    CheckParmsForFunctionDef(CurBlock->TheDecl->param_begin(),
                             CurBlock->TheDecl->param_end(),
                             /*CheckParameterNames=*/false);
  }
  
  // Finally we can process decl attributes.
  ProcessDeclAttributes(CurScope, CurBlock->TheDecl, ParamInfo);

  if (!isVariadic && CurBlock->TheDecl->getAttr<SentinelAttr>()) {
    Diag(ParamInfo.getAttributes()->getLoc(),
         diag::warn_attribute_sentinel_not_variadic) << 1;
    // FIXME: remove the attribute.
  }

  // Put the parameter variables in scope.  We can bail out immediately
  // if we don't have any.
  if (Params.empty())
    return;

  for (BlockDecl::param_iterator AI = CurBlock->TheDecl->param_begin(),
         E = CurBlock->TheDecl->param_end(); AI != E; ++AI) {
    (*AI)->setOwningFunction(CurBlock->TheDecl);

    // If this has an identifier, add it to the scope stack.
    if ((*AI)->getIdentifier()) {
      CheckShadow(CurBlock->TheScope, *AI);

      PushOnScopeChains(*AI, CurBlock->TheScope);
    }
  }
}

/// ActOnBlockError - If there is an error parsing a block, this callback
/// is invoked to pop the information about the block from the action impl.
void Sema::ActOnBlockError(SourceLocation CaretLoc, Scope *CurScope) {
  // Pop off CurBlock, handle nested blocks.
  PopDeclContext();
  PopFunctionOrBlockScope();
}

/// ActOnBlockStmtExpr - This is called when the body of a block statement
/// literal was successfully completed.  ^(int x){...}
ExprResult Sema::ActOnBlockStmtExpr(SourceLocation CaretLoc,
                                    Stmt *Body, Scope *CurScope) {
  // If blocks are disabled, emit an error.
  if (!LangOpts.Blocks)
    Diag(CaretLoc, diag::err_blocks_disable);

  BlockScopeInfo *BSI = cast<BlockScopeInfo>(FunctionScopes.back());
  
  PopDeclContext();

  QualType RetTy = Context.VoidTy;
  if (!BSI->ReturnType.isNull())
    RetTy = BSI->ReturnType;

  bool NoReturn = BSI->TheDecl->getAttr<NoReturnAttr>();
  QualType BlockTy;

  // Set the captured variables on the block.
  BSI->TheDecl->setCaptures(Context, BSI->Captures.begin(), BSI->Captures.end(),
                            BSI->CapturesCXXThis);

  // If the user wrote a function type in some form, try to use that.
  if (!BSI->FunctionType.isNull()) {
    const FunctionType *FTy = BSI->FunctionType->getAs<FunctionType>();

    FunctionType::ExtInfo Ext = FTy->getExtInfo();
    if (NoReturn && !Ext.getNoReturn()) Ext = Ext.withNoReturn(true);
    
    // Turn protoless block types into nullary block types.
    if (isa<FunctionNoProtoType>(FTy)) {
      FunctionProtoType::ExtProtoInfo EPI;
      EPI.ExtInfo = Ext;
      BlockTy = Context.getFunctionType(RetTy, 0, 0, EPI);

    // Otherwise, if we don't need to change anything about the function type,
    // preserve its sugar structure.
    } else if (FTy->getResultType() == RetTy &&
               (!NoReturn || FTy->getNoReturnAttr())) {
      BlockTy = BSI->FunctionType;

    // Otherwise, make the minimal modifications to the function type.
    } else {
      const FunctionProtoType *FPT = cast<FunctionProtoType>(FTy);
      FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
      EPI.TypeQuals = 0; // FIXME: silently?
      EPI.ExtInfo = Ext;
      BlockTy = Context.getFunctionType(RetTy,
                                        FPT->arg_type_begin(),
                                        FPT->getNumArgs(),
                                        EPI);
    }

  // If we don't have a function type, just build one from nothing.
  } else {
    FunctionProtoType::ExtProtoInfo EPI;
    EPI.ExtInfo = FunctionType::ExtInfo().withNoReturn(NoReturn);
    BlockTy = Context.getFunctionType(RetTy, 0, 0, EPI);
  }

  DiagnoseUnusedParameters(BSI->TheDecl->param_begin(),
                           BSI->TheDecl->param_end());
  BlockTy = Context.getBlockPointerType(BlockTy);

  // If needed, diagnose invalid gotos and switches in the block.
  if (getCurFunction()->NeedsScopeChecking() &&
      !hasAnyUnrecoverableErrorsInThisFunction())
    DiagnoseInvalidJumps(cast<CompoundStmt>(Body));

  BSI->TheDecl->setBody(cast<CompoundStmt>(Body));

  for (BlockDecl::capture_const_iterator ci = BSI->TheDecl->capture_begin(),
       ce = BSI->TheDecl->capture_end(); ci != ce; ++ci) {
    const VarDecl *variable = ci->getVariable();
    QualType T = variable->getType();
    QualType::DestructionKind destructKind = T.isDestructedType();
    if (destructKind != QualType::DK_none)
      getCurFunction()->setHasBranchProtectedScope();
  }

  computeNRVO(Body, getCurBlock());
  
  BlockExpr *Result = new (Context) BlockExpr(BSI->TheDecl, BlockTy);
  const AnalysisBasedWarnings::Policy &WP = AnalysisWarnings.getDefaultPolicy();
  PopFunctionOrBlockScope(&WP, Result->getBlockDecl(), Result);

  return Owned(Result);
}

ExprResult Sema::ActOnVAArg(SourceLocation BuiltinLoc,
                                        Expr *E, ParsedType Ty,
                                        SourceLocation RPLoc) {
  TypeSourceInfo *TInfo;
  GetTypeFromParser(Ty, &TInfo);
  return BuildVAArgExpr(BuiltinLoc, E, TInfo, RPLoc);
}

ExprResult Sema::BuildVAArgExpr(SourceLocation BuiltinLoc,
                                Expr *E, TypeSourceInfo *TInfo,
                                SourceLocation RPLoc) {
  Expr *OrigExpr = E;

  // Get the va_list type
  QualType VaListType = Context.getBuiltinVaListType();
  if (VaListType->isArrayType()) {
    // Deal with implicit array decay; for example, on x86-64,
    // va_list is an array, but it's supposed to decay to
    // a pointer for va_arg.
    VaListType = Context.getArrayDecayedType(VaListType);
    // Make sure the input expression also decays appropriately.
    ExprResult Result = UsualUnaryConversions(E);
    if (Result.isInvalid())
      return ExprError();
    E = Result.take();
  } else {
    // Otherwise, the va_list argument must be an l-value because
    // it is modified by va_arg.
    if (!E->isTypeDependent() &&
        CheckForModifiableLvalue(E, BuiltinLoc, *this))
      return ExprError();
  }

  if (!E->isTypeDependent() &&
      !Context.hasSameType(VaListType, E->getType())) {
    return ExprError(Diag(E->getLocStart(),
                         diag::err_first_argument_to_va_arg_not_of_type_va_list)
      << OrigExpr->getType() << E->getSourceRange());
  }

  if (!TInfo->getType()->isDependentType()) {
    if (RequireCompleteType(TInfo->getTypeLoc().getBeginLoc(), TInfo->getType(),
          PDiag(diag::err_second_parameter_to_va_arg_incomplete)
          << TInfo->getTypeLoc().getSourceRange()))
      return ExprError();

    if (RequireNonAbstractType(TInfo->getTypeLoc().getBeginLoc(),
          TInfo->getType(),
          PDiag(diag::err_second_parameter_to_va_arg_abstract)
          << TInfo->getTypeLoc().getSourceRange()))
      return ExprError();

    if (!TInfo->getType().isPODType(Context)) {
      Diag(TInfo->getTypeLoc().getBeginLoc(),
           TInfo->getType()->isObjCLifetimeType()
             ? diag::warn_second_parameter_to_va_arg_ownership_qualified
             : diag::warn_second_parameter_to_va_arg_not_pod)
        << TInfo->getType()
        << TInfo->getTypeLoc().getSourceRange();
    }

    // Check for va_arg where arguments of the given type will be promoted
    // (i.e. this va_arg is guaranteed to have undefined behavior).
    QualType PromoteType;
    if (TInfo->getType()->isPromotableIntegerType()) {
      PromoteType = Context.getPromotedIntegerType(TInfo->getType());
      if (Context.typesAreCompatible(PromoteType, TInfo->getType()))
        PromoteType = QualType();
    }
    if (TInfo->getType()->isSpecificBuiltinType(BuiltinType::Float))
      PromoteType = Context.DoubleTy;
    if (!PromoteType.isNull())
      Diag(TInfo->getTypeLoc().getBeginLoc(),
          diag::warn_second_parameter_to_va_arg_never_compatible)
        << TInfo->getType()
        << PromoteType
        << TInfo->getTypeLoc().getSourceRange();
  }

  QualType T = TInfo->getType().getNonLValueExprType(Context);
  return Owned(new (Context) VAArgExpr(BuiltinLoc, E, TInfo, RPLoc, T));
}

ExprResult Sema::ActOnGNUNullExpr(SourceLocation TokenLoc) {
  // The type of __null will be int or long, depending on the size of
  // pointers on the target.
  QualType Ty;
  unsigned pw = Context.getTargetInfo().getPointerWidth(0);
  if (pw == Context.getTargetInfo().getIntWidth())
    Ty = Context.IntTy;
  else if (pw == Context.getTargetInfo().getLongWidth())
    Ty = Context.LongTy;
  else if (pw == Context.getTargetInfo().getLongLongWidth())
    Ty = Context.LongLongTy;
  else {
    llvm_unreachable("I don't know size of pointer!");
  }

  return Owned(new (Context) GNUNullExpr(Ty, TokenLoc));
}

static void MakeObjCStringLiteralFixItHint(Sema& SemaRef, QualType DstType,
                                           Expr *SrcExpr, FixItHint &Hint) {
  if (!SemaRef.getLangOptions().ObjC1)
    return;

  const ObjCObjectPointerType *PT = DstType->getAs<ObjCObjectPointerType>();
  if (!PT)
    return;

  // Check if the destination is of type 'id'.
  if (!PT->isObjCIdType()) {
    // Check if the destination is the 'NSString' interface.
    const ObjCInterfaceDecl *ID = PT->getInterfaceDecl();
    if (!ID || !ID->getIdentifier()->isStr("NSString"))
      return;
  }

  // Strip off any parens and casts.
  StringLiteral *SL = dyn_cast<StringLiteral>(SrcExpr->IgnoreParenCasts());
  if (!SL || !SL->isAscii())
    return;

  Hint = FixItHint::CreateInsertion(SL->getLocStart(), "@");
}

bool Sema::DiagnoseAssignmentResult(AssignConvertType ConvTy,
                                    SourceLocation Loc,
                                    QualType DstType, QualType SrcType,
                                    Expr *SrcExpr, AssignmentAction Action,
                                    bool *Complained) {
  if (Complained)
    *Complained = false;

  // Decode the result (notice that AST's are still created for extensions).
  bool CheckInferredResultType = false;
  bool isInvalid = false;
  unsigned DiagKind;
  FixItHint Hint;
  ConversionFixItGenerator ConvHints;
  bool MayHaveConvFixit = false;

  switch (ConvTy) {
  default: llvm_unreachable("Unknown conversion type");
  case Compatible: return false;
  case PointerToInt:
    DiagKind = diag::ext_typecheck_convert_pointer_int;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    break;
  case IntToPointer:
    DiagKind = diag::ext_typecheck_convert_int_pointer;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    break;
  case IncompatiblePointer:
    MakeObjCStringLiteralFixItHint(*this, DstType, SrcExpr, Hint);
    DiagKind = diag::ext_typecheck_convert_incompatible_pointer;
    CheckInferredResultType = DstType->isObjCObjectPointerType() &&
      SrcType->isObjCObjectPointerType();
    if (Hint.isNull() && !CheckInferredResultType) {
      ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    }
    MayHaveConvFixit = true;
    break;
  case IncompatiblePointerSign:
    DiagKind = diag::ext_typecheck_convert_incompatible_pointer_sign;
    break;
  case FunctionVoidPointer:
    DiagKind = diag::ext_typecheck_convert_pointer_void_func;
    break;
  case IncompatiblePointerDiscardsQualifiers: {
    // Perform array-to-pointer decay if necessary.
    if (SrcType->isArrayType()) SrcType = Context.getArrayDecayedType(SrcType);

    Qualifiers lhq = SrcType->getPointeeType().getQualifiers();
    Qualifiers rhq = DstType->getPointeeType().getQualifiers();
    if (lhq.getAddressSpace() != rhq.getAddressSpace()) {
      DiagKind = diag::err_typecheck_incompatible_address_space;
      break;


    } else if (lhq.getObjCLifetime() != rhq.getObjCLifetime()) {
      DiagKind = diag::err_typecheck_incompatible_ownership;
      break;
    }

    llvm_unreachable("unknown error case for discarding qualifiers!");
    // fallthrough
  }
  case CompatiblePointerDiscardsQualifiers:
    // If the qualifiers lost were because we were applying the
    // (deprecated) C++ conversion from a string literal to a char*
    // (or wchar_t*), then there was no error (C++ 4.2p2).  FIXME:
    // Ideally, this check would be performed in
    // checkPointerTypesForAssignment. However, that would require a
    // bit of refactoring (so that the second argument is an
    // expression, rather than a type), which should be done as part
    // of a larger effort to fix checkPointerTypesForAssignment for
    // C++ semantics.
    if (getLangOptions().CPlusPlus &&
        IsStringLiteralToNonConstPointerConversion(SrcExpr, DstType))
      return false;
    DiagKind = diag::ext_typecheck_convert_discards_qualifiers;
    break;
  case IncompatibleNestedPointerQualifiers:
    DiagKind = diag::ext_nested_pointer_qualifier_mismatch;
    break;
  case IntToBlockPointer:
    DiagKind = diag::err_int_to_block_pointer;
    break;
  case IncompatibleBlockPointer:
    DiagKind = diag::err_typecheck_convert_incompatible_block_pointer;
    break;
  case IncompatibleObjCQualifiedId:
    // FIXME: Diagnose the problem in ObjCQualifiedIdTypesAreCompatible, since
    // it can give a more specific diagnostic.
    DiagKind = diag::warn_incompatible_qualified_id;
    break;
  case IncompatibleVectors:
    DiagKind = diag::warn_incompatible_vectors;
    break;
  case IncompatibleObjCWeakRef:
    DiagKind = diag::err_arc_weak_unavailable_assign;
    break;
  case Incompatible:
    DiagKind = diag::err_typecheck_convert_incompatible;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    isInvalid = true;
    break;
  }

  QualType FirstType, SecondType;
  switch (Action) {
  case AA_Assigning:
  case AA_Initializing:
    // The destination type comes first.
    FirstType = DstType;
    SecondType = SrcType;
    break;

  case AA_Returning:
  case AA_Passing:
  case AA_Converting:
  case AA_Sending:
  case AA_Casting:
    // The source type comes first.
    FirstType = SrcType;
    SecondType = DstType;
    break;
  }

  PartialDiagnostic FDiag = PDiag(DiagKind);
  FDiag << FirstType << SecondType << Action << SrcExpr->getSourceRange();

  // If we can fix the conversion, suggest the FixIts.
  assert(ConvHints.isNull() || Hint.isNull());
  if (!ConvHints.isNull()) {
    for (llvm::SmallVector<FixItHint, 1>::iterator
        HI = ConvHints.Hints.begin(), HE = ConvHints.Hints.end();
        HI != HE; ++HI)
      FDiag << *HI;
  } else {
    FDiag << Hint;
  }
  if (MayHaveConvFixit) { FDiag << (unsigned) (ConvHints.Kind); }

  Diag(Loc, FDiag);

  if (CheckInferredResultType)
    EmitRelatedResultTypeNote(SrcExpr);
  
  if (Complained)
    *Complained = true;
  return isInvalid;
}

bool Sema::VerifyIntegerConstantExpression(const Expr *E, llvm::APSInt *Result){
  llvm::APSInt ICEResult;
  if (E->isIntegerConstantExpr(ICEResult, Context)) {
    if (Result)
      *Result = ICEResult;
    return false;
  }

  Expr::EvalResult EvalResult;

  if (!E->Evaluate(EvalResult, Context) || !EvalResult.Val.isInt() ||
      EvalResult.HasSideEffects) {
    Diag(E->getExprLoc(), diag::err_expr_not_ice) << E->getSourceRange();

    if (EvalResult.Diag) {
      // We only show the note if it's not the usual "invalid subexpression"
      // or if it's actually in a subexpression.
      if (EvalResult.Diag != diag::note_invalid_subexpr_in_ice ||
          E->IgnoreParens() != EvalResult.DiagExpr->IgnoreParens())
        Diag(EvalResult.DiagLoc, EvalResult.Diag);
    }

    return true;
  }

  Diag(E->getExprLoc(), diag::ext_expr_not_ice) <<
    E->getSourceRange();

  if (EvalResult.Diag &&
      Diags.getDiagnosticLevel(diag::ext_expr_not_ice, EvalResult.DiagLoc)
          != DiagnosticsEngine::Ignored)
    Diag(EvalResult.DiagLoc, EvalResult.Diag);

  if (Result)
    *Result = EvalResult.Val.getInt();
  return false;
}

void
Sema::PushExpressionEvaluationContext(ExpressionEvaluationContext NewContext) {
  ExprEvalContexts.push_back(
             ExpressionEvaluationContextRecord(NewContext,
                                               ExprTemporaries.size(),
                                               ExprNeedsCleanups));
  ExprNeedsCleanups = false;
}

void Sema::PopExpressionEvaluationContext() {
  // Pop the current expression evaluation context off the stack.
  ExpressionEvaluationContextRecord Rec = ExprEvalContexts.back();
  ExprEvalContexts.pop_back();

  if (Rec.Context == PotentiallyPotentiallyEvaluated) {
    if (Rec.PotentiallyReferenced) {
      // Mark any remaining declarations in the current position of the stack
      // as "referenced". If they were not meant to be referenced, semantic
      // analysis would have eliminated them (e.g., in ActOnCXXTypeId).
      for (PotentiallyReferencedDecls::iterator
             I = Rec.PotentiallyReferenced->begin(),
             IEnd = Rec.PotentiallyReferenced->end();
           I != IEnd; ++I)
        MarkDeclarationReferenced(I->first, I->second);
    }

    if (Rec.PotentiallyDiagnosed) {
      // Emit any pending diagnostics.
      for (PotentiallyEmittedDiagnostics::iterator
                I = Rec.PotentiallyDiagnosed->begin(),
             IEnd = Rec.PotentiallyDiagnosed->end();
           I != IEnd; ++I)
        Diag(I->first, I->second);
    }
  }

  // When are coming out of an unevaluated context, clear out any
  // temporaries that we may have created as part of the evaluation of
  // the expression in that context: they aren't relevant because they
  // will never be constructed.
  if (Rec.Context == Unevaluated) {
    ExprTemporaries.erase(ExprTemporaries.begin() + Rec.NumTemporaries,
                          ExprTemporaries.end());
    ExprNeedsCleanups = Rec.ParentNeedsCleanups;

  // Otherwise, merge the contexts together.
  } else {
    ExprNeedsCleanups |= Rec.ParentNeedsCleanups;
  }

  // Destroy the popped expression evaluation record.
  Rec.Destroy();
}

void Sema::DiscardCleanupsInEvaluationContext() {
  ExprTemporaries.erase(
              ExprTemporaries.begin() + ExprEvalContexts.back().NumTemporaries,
              ExprTemporaries.end());
  ExprNeedsCleanups = false;
}

/// \brief Note that the given declaration was referenced in the source code.
///
/// This routine should be invoke whenever a given declaration is referenced
/// in the source code, and where that reference occurred. If this declaration
/// reference means that the the declaration is used (C++ [basic.def.odr]p2,
/// C99 6.9p3), then the declaration will be marked as used.
///
/// \param Loc the location where the declaration was referenced.
///
/// \param D the declaration that has been referenced by the source code.
void Sema::MarkDeclarationReferenced(SourceLocation Loc, Decl *D) {
  assert(D && "No declaration?");

  D->setReferenced();

  if (D->isUsed(false))
    return;

  // Mark a parameter or variable declaration "used", regardless of whether
  // we're in a template or not. The reason for this is that unevaluated
  // expressions (e.g. (void)sizeof()) constitute a use for warning purposes
  // (-Wunused-variables and -Wunused-parameters)
  if (isa<ParmVarDecl>(D) ||
      (isa<VarDecl>(D) && D->getDeclContext()->isFunctionOrMethod())) {
    D->setUsed();
    return;
  }

  if (!isa<VarDecl>(D) && !isa<FunctionDecl>(D))
    return;

  // Do not mark anything as "used" within a dependent context; wait for
  // an instantiation.
  if (CurContext->isDependentContext())
    return;

  switch (ExprEvalContexts.back().Context) {
    case Unevaluated:
      // We are in an expression that is not potentially evaluated; do nothing.
      return;

    case PotentiallyEvaluated:
      // We are in a potentially-evaluated expression, so this declaration is
      // "used"; handle this below.
      break;

    case PotentiallyPotentiallyEvaluated:
      // We are in an expression that may be potentially evaluated; queue this
      // declaration reference until we know whether the expression is
      // potentially evaluated.
      ExprEvalContexts.back().addReferencedDecl(Loc, D);
      return;
      
    case PotentiallyEvaluatedIfUsed:
      // Referenced declarations will only be used if the construct in the
      // containing expression is used.
      return;
  }

  // Note that this declaration has been used.
  if (CXXConstructorDecl *Constructor = dyn_cast<CXXConstructorDecl>(D)) {
    if (Constructor->isDefaulted()) {
      if (Constructor->isDefaultConstructor()) {
        if (Constructor->isTrivial())
          return;
        if (!Constructor->isUsed(false))
          DefineImplicitDefaultConstructor(Loc, Constructor);
      } else if (Constructor->isCopyConstructor()) {
        if (!Constructor->isUsed(false))
          DefineImplicitCopyConstructor(Loc, Constructor);
      } else if (Constructor->isMoveConstructor()) {
        if (!Constructor->isUsed(false))
          DefineImplicitMoveConstructor(Loc, Constructor);
      }
    }

    MarkVTableUsed(Loc, Constructor->getParent());
  } else if (CXXDestructorDecl *Destructor = dyn_cast<CXXDestructorDecl>(D)) {
    if (Destructor->isDefaulted() && !Destructor->isUsed(false))
      DefineImplicitDestructor(Loc, Destructor);
    if (Destructor->isVirtual())
      MarkVTableUsed(Loc, Destructor->getParent());
  } else if (CXXMethodDecl *MethodDecl = dyn_cast<CXXMethodDecl>(D)) {
    if (MethodDecl->isDefaulted() && MethodDecl->isOverloadedOperator() &&
        MethodDecl->getOverloadedOperator() == OO_Equal) {
      if (!MethodDecl->isUsed(false)) {
        if (MethodDecl->isCopyAssignmentOperator())
          DefineImplicitCopyAssignment(Loc, MethodDecl);
        else
          DefineImplicitMoveAssignment(Loc, MethodDecl);
      }
    } else if (MethodDecl->isVirtual())
      MarkVTableUsed(Loc, MethodDecl->getParent());
  }
  if (FunctionDecl *Function = dyn_cast<FunctionDecl>(D)) {
    // Recursive functions should be marked when used from another function.
    if (CurContext == Function) return;

    // Implicit instantiation of function templates and member functions of
    // class templates.
    if (Function->isImplicitlyInstantiable()) {
      bool AlreadyInstantiated = false;
      if (FunctionTemplateSpecializationInfo *SpecInfo
                                = Function->getTemplateSpecializationInfo()) {
        if (SpecInfo->getPointOfInstantiation().isInvalid())
          SpecInfo->setPointOfInstantiation(Loc);
        else if (SpecInfo->getTemplateSpecializationKind()
                   == TSK_ImplicitInstantiation)
          AlreadyInstantiated = true;
      } else if (MemberSpecializationInfo *MSInfo
                                  = Function->getMemberSpecializationInfo()) {
        if (MSInfo->getPointOfInstantiation().isInvalid())
          MSInfo->setPointOfInstantiation(Loc);
        else if (MSInfo->getTemplateSpecializationKind()
                   == TSK_ImplicitInstantiation)
          AlreadyInstantiated = true;
      }

      if (!AlreadyInstantiated) {
        if (isa<CXXRecordDecl>(Function->getDeclContext()) &&
            cast<CXXRecordDecl>(Function->getDeclContext())->isLocalClass())
          PendingLocalImplicitInstantiations.push_back(std::make_pair(Function,
                                                                      Loc));
        else
          PendingInstantiations.push_back(std::make_pair(Function, Loc));
      }
    } else {
      // Walk redefinitions, as some of them may be instantiable.
      for (FunctionDecl::redecl_iterator i(Function->redecls_begin()),
           e(Function->redecls_end()); i != e; ++i) {
        if (!i->isUsed(false) && i->isImplicitlyInstantiable())
          MarkDeclarationReferenced(Loc, *i);
      }
    }

    // Keep track of used but undefined functions.
    if (!Function->isPure() && !Function->hasBody() &&
        Function->getLinkage() != ExternalLinkage) {
      SourceLocation &old = UndefinedInternals[Function->getCanonicalDecl()];
      if (old.isInvalid()) old = Loc;
    }

    Function->setUsed(true);
    return;
  }

  if (VarDecl *Var = dyn_cast<VarDecl>(D)) {
    // Implicit instantiation of static data members of class templates.
    if (Var->isStaticDataMember() &&
        Var->getInstantiatedFromStaticDataMember()) {
      MemberSpecializationInfo *MSInfo = Var->getMemberSpecializationInfo();
      assert(MSInfo && "Missing member specialization information?");
      if (MSInfo->getPointOfInstantiation().isInvalid() &&
          MSInfo->getTemplateSpecializationKind()== TSK_ImplicitInstantiation) {
        MSInfo->setPointOfInstantiation(Loc);
        // This is a modification of an existing AST node. Notify listeners.
        if (ASTMutationListener *L = getASTMutationListener())
          L->StaticDataMemberInstantiated(Var);
        PendingInstantiations.push_back(std::make_pair(Var, Loc));
      }
    }

    // Keep track of used but undefined variables.  We make a hole in
    // the warning for static const data members with in-line
    // initializers.
    if (Var->hasDefinition() == VarDecl::DeclarationOnly
        && Var->getLinkage() != ExternalLinkage
        && !(Var->isStaticDataMember() && Var->hasInit())) {
      SourceLocation &old = UndefinedInternals[Var->getCanonicalDecl()];
      if (old.isInvalid()) old = Loc;
    }

    D->setUsed(true);
    return;
  }
}

namespace {
  // Mark all of the declarations referenced
  // FIXME: Not fully implemented yet! We need to have a better understanding
  // of when we're entering
  class MarkReferencedDecls : public RecursiveASTVisitor<MarkReferencedDecls> {
    Sema &S;
    SourceLocation Loc;

  public:
    typedef RecursiveASTVisitor<MarkReferencedDecls> Inherited;

    MarkReferencedDecls(Sema &S, SourceLocation Loc) : S(S), Loc(Loc) { }

    bool TraverseTemplateArgument(const TemplateArgument &Arg);
    bool TraverseRecordType(RecordType *T);
  };
}

bool MarkReferencedDecls::TraverseTemplateArgument(
  const TemplateArgument &Arg) {
  if (Arg.getKind() == TemplateArgument::Declaration) {
    S.MarkDeclarationReferenced(Loc, Arg.getAsDecl());
  }

  return Inherited::TraverseTemplateArgument(Arg);
}

bool MarkReferencedDecls::TraverseRecordType(RecordType *T) {
  if (ClassTemplateSpecializationDecl *Spec
                  = dyn_cast<ClassTemplateSpecializationDecl>(T->getDecl())) {
    const TemplateArgumentList &Args = Spec->getTemplateArgs();
    return TraverseTemplateArguments(Args.data(), Args.size());
  }

  return true;
}

void Sema::MarkDeclarationsReferencedInType(SourceLocation Loc, QualType T) {
  MarkReferencedDecls Marker(*this, Loc);
  Marker.TraverseType(Context.getCanonicalType(T));
}

namespace {
  /// \brief Helper class that marks all of the declarations referenced by
  /// potentially-evaluated subexpressions as "referenced".
  class EvaluatedExprMarker : public EvaluatedExprVisitor<EvaluatedExprMarker> {
    Sema &S;
    
  public:
    typedef EvaluatedExprVisitor<EvaluatedExprMarker> Inherited;
    
    explicit EvaluatedExprMarker(Sema &S) : Inherited(S.Context), S(S) { }
    
    void VisitDeclRefExpr(DeclRefExpr *E) {
      S.MarkDeclarationReferenced(E->getLocation(), E->getDecl());
    }
    
    void VisitMemberExpr(MemberExpr *E) {
      S.MarkDeclarationReferenced(E->getMemberLoc(), E->getMemberDecl());
      Inherited::VisitMemberExpr(E);
    }
    
    void VisitCXXNewExpr(CXXNewExpr *E) {
      if (E->getConstructor())
        S.MarkDeclarationReferenced(E->getLocStart(), E->getConstructor());
      if (E->getOperatorNew())
        S.MarkDeclarationReferenced(E->getLocStart(), E->getOperatorNew());
      if (E->getOperatorDelete())
        S.MarkDeclarationReferenced(E->getLocStart(), E->getOperatorDelete());
      Inherited::VisitCXXNewExpr(E);
    }
    
    void VisitCXXDeleteExpr(CXXDeleteExpr *E) {
      if (E->getOperatorDelete())
        S.MarkDeclarationReferenced(E->getLocStart(), E->getOperatorDelete());
      QualType Destroyed = S.Context.getBaseElementType(E->getDestroyedType());
      if (const RecordType *DestroyedRec = Destroyed->getAs<RecordType>()) {
        CXXRecordDecl *Record = cast<CXXRecordDecl>(DestroyedRec->getDecl());
        S.MarkDeclarationReferenced(E->getLocStart(), 
                                    S.LookupDestructor(Record));
      }
      
      Inherited::VisitCXXDeleteExpr(E);
    }
    
    void VisitCXXConstructExpr(CXXConstructExpr *E) {
      S.MarkDeclarationReferenced(E->getLocStart(), E->getConstructor());
      Inherited::VisitCXXConstructExpr(E);
    }
    
    void VisitBlockDeclRefExpr(BlockDeclRefExpr *E) {
      S.MarkDeclarationReferenced(E->getLocation(), E->getDecl());
    }
    
    void VisitCXXDefaultArgExpr(CXXDefaultArgExpr *E) {
      Visit(E->getExpr());
    }
  };
}

/// \brief Mark any declarations that appear within this expression or any
/// potentially-evaluated subexpressions as "referenced".
void Sema::MarkDeclarationsReferencedInExpr(Expr *E) {
  EvaluatedExprMarker(*this).Visit(E);
}

/// \brief Emit a diagnostic that describes an effect on the run-time behavior
/// of the program being compiled.
///
/// This routine emits the given diagnostic when the code currently being
/// type-checked is "potentially evaluated", meaning that there is a
/// possibility that the code will actually be executable. Code in sizeof()
/// expressions, code used only during overload resolution, etc., are not
/// potentially evaluated. This routine will suppress such diagnostics or,
/// in the absolutely nutty case of potentially potentially evaluated
/// expressions (C++ typeid), queue the diagnostic to potentially emit it
/// later.
///
/// This routine should be used for all diagnostics that describe the run-time
/// behavior of a program, such as passing a non-POD value through an ellipsis.
/// Failure to do so will likely result in spurious diagnostics or failures
/// during overload resolution or within sizeof/alignof/typeof/typeid.
bool Sema::DiagRuntimeBehavior(SourceLocation Loc, const Stmt *Statement,
                               const PartialDiagnostic &PD) {
  switch (ExprEvalContexts.back().Context) {
  case Unevaluated:
    // The argument will never be evaluated, so don't complain.
    break;

  case PotentiallyEvaluated:
  case PotentiallyEvaluatedIfUsed:
    if (Statement && getCurFunctionOrMethodDecl()) {
      FunctionScopes.back()->PossiblyUnreachableDiags.
        push_back(sema::PossiblyUnreachableDiag(PD, Loc, Statement));
    }
    else
      Diag(Loc, PD);
      
    return true;

  case PotentiallyPotentiallyEvaluated:
    ExprEvalContexts.back().addDiagnostic(Loc, PD);
    break;
  }

  return false;
}

bool Sema::CheckCallReturnType(QualType ReturnType, SourceLocation Loc,
                               CallExpr *CE, FunctionDecl *FD) {
  if (ReturnType->isVoidType() || !ReturnType->isIncompleteType())
    return false;

  PartialDiagnostic Note =
    FD ? PDiag(diag::note_function_with_incomplete_return_type_declared_here)
    << FD->getDeclName() : PDiag();
  SourceLocation NoteLoc = FD ? FD->getLocation() : SourceLocation();

  if (RequireCompleteType(Loc, ReturnType,
                          FD ?
                          PDiag(diag::err_call_function_incomplete_return)
                            << CE->getSourceRange() << FD->getDeclName() :
                          PDiag(diag::err_call_incomplete_return)
                            << CE->getSourceRange(),
                          std::make_pair(NoteLoc, Note)))
    return true;

  return false;
}

// Diagnose the s/=/==/ and s/\|=/!=/ typos. Note that adding parentheses
// will prevent this condition from triggering, which is what we want.
void Sema::DiagnoseAssignmentAsCondition(Expr *E) {
  SourceLocation Loc;

  unsigned diagnostic = diag::warn_condition_is_assignment;
  bool IsOrAssign = false;

  if (BinaryOperator *Op = dyn_cast<BinaryOperator>(E)) {
    if (Op->getOpcode() != BO_Assign && Op->getOpcode() != BO_OrAssign)
      return;

    IsOrAssign = Op->getOpcode() == BO_OrAssign;

    // Greylist some idioms by putting them into a warning subcategory.
    if (ObjCMessageExpr *ME
          = dyn_cast<ObjCMessageExpr>(Op->getRHS()->IgnoreParenCasts())) {
      Selector Sel = ME->getSelector();

      // self = [<foo> init...]
      if (isSelfExpr(Op->getLHS()) && Sel.getNameForSlot(0).startswith("init"))
        diagnostic = diag::warn_condition_is_idiomatic_assignment;

      // <foo> = [<bar> nextObject]
      else if (Sel.isUnarySelector() && Sel.getNameForSlot(0) == "nextObject")
        diagnostic = diag::warn_condition_is_idiomatic_assignment;
    }

    Loc = Op->getOperatorLoc();
  } else if (CXXOperatorCallExpr *Op = dyn_cast<CXXOperatorCallExpr>(E)) {
    if (Op->getOperator() != OO_Equal && Op->getOperator() != OO_PipeEqual)
      return;

    IsOrAssign = Op->getOperator() == OO_PipeEqual;
    Loc = Op->getOperatorLoc();
  } else {
    // Not an assignment.
    return;
  }

  Diag(Loc, diagnostic) << E->getSourceRange();

  SourceLocation Open = E->getSourceRange().getBegin();
  SourceLocation Close = PP.getLocForEndOfToken(E->getSourceRange().getEnd());
  Diag(Loc, diag::note_condition_assign_silence)
        << FixItHint::CreateInsertion(Open, "(")
        << FixItHint::CreateInsertion(Close, ")");

  if (IsOrAssign)
    Diag(Loc, diag::note_condition_or_assign_to_comparison)
      << FixItHint::CreateReplacement(Loc, "!=");
  else
    Diag(Loc, diag::note_condition_assign_to_comparison)
      << FixItHint::CreateReplacement(Loc, "==");
}

/// \brief Redundant parentheses over an equality comparison can indicate
/// that the user intended an assignment used as condition.
void Sema::DiagnoseEqualityWithExtraParens(ParenExpr *ParenE) {
  // Don't warn if the parens came from a macro.
  SourceLocation parenLoc = ParenE->getLocStart();
  if (parenLoc.isInvalid() || parenLoc.isMacroID())
    return;
  // Don't warn for dependent expressions.
  if (ParenE->isTypeDependent())
    return;

  Expr *E = ParenE->IgnoreParens();

  if (BinaryOperator *opE = dyn_cast<BinaryOperator>(E))
    if (opE->getOpcode() == BO_EQ &&
        opE->getLHS()->IgnoreParenImpCasts()->isModifiableLvalue(Context)
                                                           == Expr::MLV_Valid) {
      SourceLocation Loc = opE->getOperatorLoc();
      
      Diag(Loc, diag::warn_equality_with_extra_parens) << E->getSourceRange();
      Diag(Loc, diag::note_equality_comparison_silence)
        << FixItHint::CreateRemoval(ParenE->getSourceRange().getBegin())
        << FixItHint::CreateRemoval(ParenE->getSourceRange().getEnd());
      Diag(Loc, diag::note_equality_comparison_to_assign)
        << FixItHint::CreateReplacement(Loc, "=");
    }
}

ExprResult Sema::CheckBooleanCondition(Expr *E, SourceLocation Loc) {
  DiagnoseAssignmentAsCondition(E);
  if (ParenExpr *parenE = dyn_cast<ParenExpr>(E))
    DiagnoseEqualityWithExtraParens(parenE);

  ExprResult result = CheckPlaceholderExpr(E);
  if (result.isInvalid()) return ExprError();
  E = result.take();

  if (!E->isTypeDependent()) {
    if (getLangOptions().CPlusPlus)
      return CheckCXXBooleanCondition(E); // C++ 6.4p4

    ExprResult ERes = DefaultFunctionArrayLvalueConversion(E);
    if (ERes.isInvalid())
      return ExprError();
    E = ERes.take();

    QualType T = E->getType();
    if (!T->isScalarType()) { // C99 6.8.4.1p1
      Diag(Loc, diag::err_typecheck_statement_requires_scalar)
        << T << E->getSourceRange();
      return ExprError();
    }
  }

  return Owned(E);
}

ExprResult Sema::ActOnBooleanCondition(Scope *S, SourceLocation Loc,
                                       Expr *SubExpr) {
  if (!SubExpr)
    return ExprError();

  return CheckBooleanCondition(SubExpr, Loc);
}

namespace {
  /// A visitor for rebuilding a call to an __unknown_any expression
  /// to have an appropriate type.
  struct RebuildUnknownAnyFunction
    : StmtVisitor<RebuildUnknownAnyFunction, ExprResult> {

    Sema &S;

    RebuildUnknownAnyFunction(Sema &S) : S(S) {}

    ExprResult VisitStmt(Stmt *S) {
      llvm_unreachable("unexpected statement!");
      return ExprError();
    }

    ExprResult VisitExpr(Expr *E) {
      S.Diag(E->getExprLoc(), diag::err_unsupported_unknown_any_call)
        << E->getSourceRange();
      return ExprError();
    }

    /// Rebuild an expression which simply semantically wraps another
    /// expression which it shares the type and value kind of.
    template <class T> ExprResult rebuildSugarExpr(T *E) {
      ExprResult SubResult = Visit(E->getSubExpr());
      if (SubResult.isInvalid()) return ExprError();

      Expr *SubExpr = SubResult.take();
      E->setSubExpr(SubExpr);
      E->setType(SubExpr->getType());
      E->setValueKind(SubExpr->getValueKind());
      assert(E->getObjectKind() == OK_Ordinary);
      return E;
    }

    ExprResult VisitParenExpr(ParenExpr *E) {
      return rebuildSugarExpr(E);
    }

    ExprResult VisitUnaryExtension(UnaryOperator *E) {
      return rebuildSugarExpr(E);
    }

    ExprResult VisitUnaryAddrOf(UnaryOperator *E) {
      ExprResult SubResult = Visit(E->getSubExpr());
      if (SubResult.isInvalid()) return ExprError();

      Expr *SubExpr = SubResult.take();
      E->setSubExpr(SubExpr);
      E->setType(S.Context.getPointerType(SubExpr->getType()));
      assert(E->getValueKind() == VK_RValue);
      assert(E->getObjectKind() == OK_Ordinary);
      return E;
    }

    ExprResult resolveDecl(Expr *E, ValueDecl *VD) {
      if (!isa<FunctionDecl>(VD)) return VisitExpr(E);

      E->setType(VD->getType());

      assert(E->getValueKind() == VK_RValue);
      if (S.getLangOptions().CPlusPlus &&
          !(isa<CXXMethodDecl>(VD) &&
            cast<CXXMethodDecl>(VD)->isInstance()))
        E->setValueKind(VK_LValue);

      return E;
    }

    ExprResult VisitMemberExpr(MemberExpr *E) {
      return resolveDecl(E, E->getMemberDecl());
    }

    ExprResult VisitDeclRefExpr(DeclRefExpr *E) {
      return resolveDecl(E, E->getDecl());
    }
  };
}

/// Given a function expression of unknown-any type, try to rebuild it
/// to have a function type.
static ExprResult rebuildUnknownAnyFunction(Sema &S, Expr *FunctionExpr) {
  ExprResult Result = RebuildUnknownAnyFunction(S).Visit(FunctionExpr);
  if (Result.isInvalid()) return ExprError();
  return S.DefaultFunctionArrayConversion(Result.take());
}

namespace {
  /// A visitor for rebuilding an expression of type __unknown_anytype
  /// into one which resolves the type directly on the referring
  /// expression.  Strict preservation of the original source
  /// structure is not a goal.
  struct RebuildUnknownAnyExpr
    : StmtVisitor<RebuildUnknownAnyExpr, ExprResult> {

    Sema &S;

    /// The current destination type.
    QualType DestType;

    RebuildUnknownAnyExpr(Sema &S, QualType CastType)
      : S(S), DestType(CastType) {}

    ExprResult VisitStmt(Stmt *S) {
      llvm_unreachable("unexpected statement!");
      return ExprError();
    }

    ExprResult VisitExpr(Expr *E) {
      S.Diag(E->getExprLoc(), diag::err_unsupported_unknown_any_expr)
        << E->getSourceRange();
      return ExprError();
    }

    ExprResult VisitCallExpr(CallExpr *E);
    ExprResult VisitObjCMessageExpr(ObjCMessageExpr *E);

    /// Rebuild an expression which simply semantically wraps another
    /// expression which it shares the type and value kind of.
    template <class T> ExprResult rebuildSugarExpr(T *E) {
      ExprResult SubResult = Visit(E->getSubExpr());
      if (SubResult.isInvalid()) return ExprError();
      Expr *SubExpr = SubResult.take();
      E->setSubExpr(SubExpr);
      E->setType(SubExpr->getType());
      E->setValueKind(SubExpr->getValueKind());
      assert(E->getObjectKind() == OK_Ordinary);
      return E;
    }

    ExprResult VisitParenExpr(ParenExpr *E) {
      return rebuildSugarExpr(E);
    }

    ExprResult VisitUnaryExtension(UnaryOperator *E) {
      return rebuildSugarExpr(E);
    }

    ExprResult VisitUnaryAddrOf(UnaryOperator *E) {
      const PointerType *Ptr = DestType->getAs<PointerType>();
      if (!Ptr) {
        S.Diag(E->getOperatorLoc(), diag::err_unknown_any_addrof)
          << E->getSourceRange();
        return ExprError();
      }
      assert(E->getValueKind() == VK_RValue);
      assert(E->getObjectKind() == OK_Ordinary);
      E->setType(DestType);

      // Build the sub-expression as if it were an object of the pointee type.
      DestType = Ptr->getPointeeType();
      ExprResult SubResult = Visit(E->getSubExpr());
      if (SubResult.isInvalid()) return ExprError();
      E->setSubExpr(SubResult.take());
      return E;
    }

    ExprResult VisitImplicitCastExpr(ImplicitCastExpr *E);

    ExprResult resolveDecl(Expr *E, ValueDecl *VD);

    ExprResult VisitMemberExpr(MemberExpr *E) {
      return resolveDecl(E, E->getMemberDecl());
    }

    ExprResult VisitDeclRefExpr(DeclRefExpr *E) {
      return resolveDecl(E, E->getDecl());
    }
  };
}

/// Rebuilds a call expression which yielded __unknown_anytype.
ExprResult RebuildUnknownAnyExpr::VisitCallExpr(CallExpr *E) {
  Expr *CalleeExpr = E->getCallee();

  enum FnKind {
    FK_MemberFunction,
    FK_FunctionPointer,
    FK_BlockPointer
  };

  FnKind Kind;
  QualType CalleeType = CalleeExpr->getType();
  if (CalleeType == S.Context.BoundMemberTy) {
    assert(isa<CXXMemberCallExpr>(E) || isa<CXXOperatorCallExpr>(E));
    Kind = FK_MemberFunction;
    CalleeType = Expr::findBoundMemberType(CalleeExpr);
  } else if (const PointerType *Ptr = CalleeType->getAs<PointerType>()) {
    CalleeType = Ptr->getPointeeType();
    Kind = FK_FunctionPointer;
  } else {
    CalleeType = CalleeType->castAs<BlockPointerType>()->getPointeeType();
    Kind = FK_BlockPointer;
  }
  const FunctionType *FnType = CalleeType->castAs<FunctionType>();

  // Verify that this is a legal result type of a function.
  if (DestType->isArrayType() || DestType->isFunctionType()) {
    unsigned diagID = diag::err_func_returning_array_function;
    if (Kind == FK_BlockPointer)
      diagID = diag::err_block_returning_array_function;

    S.Diag(E->getExprLoc(), diagID)
      << DestType->isFunctionType() << DestType;
    return ExprError();
  }

  // Otherwise, go ahead and set DestType as the call's result.
  E->setType(DestType.getNonLValueExprType(S.Context));
  E->setValueKind(Expr::getValueKindForType(DestType));
  assert(E->getObjectKind() == OK_Ordinary);

  // Rebuild the function type, replacing the result type with DestType.
  if (const FunctionProtoType *Proto = dyn_cast<FunctionProtoType>(FnType))
    DestType = S.Context.getFunctionType(DestType,
                                         Proto->arg_type_begin(),
                                         Proto->getNumArgs(),
                                         Proto->getExtProtoInfo());
  else
    DestType = S.Context.getFunctionNoProtoType(DestType,
                                                FnType->getExtInfo());

  // Rebuild the appropriate pointer-to-function type.
  switch (Kind) { 
  case FK_MemberFunction:
    // Nothing to do.
    break;

  case FK_FunctionPointer:
    DestType = S.Context.getPointerType(DestType);
    break;

  case FK_BlockPointer:
    DestType = S.Context.getBlockPointerType(DestType);
    break;
  }

  // Finally, we can recurse.
  ExprResult CalleeResult = Visit(CalleeExpr);
  if (!CalleeResult.isUsable()) return ExprError();
  E->setCallee(CalleeResult.take());

  // Bind a temporary if necessary.
  return S.MaybeBindToTemporary(E);
}

ExprResult RebuildUnknownAnyExpr::VisitObjCMessageExpr(ObjCMessageExpr *E) {
  // Verify that this is a legal result type of a call.
  if (DestType->isArrayType() || DestType->isFunctionType()) {
    S.Diag(E->getExprLoc(), diag::err_func_returning_array_function)
      << DestType->isFunctionType() << DestType;
    return ExprError();
  }

  // Rewrite the method result type if available.
  if (ObjCMethodDecl *Method = E->getMethodDecl()) {
    assert(Method->getResultType() == S.Context.UnknownAnyTy);
    Method->setResultType(DestType);
  }

  // Change the type of the message.
  E->setType(DestType.getNonReferenceType());
  E->setValueKind(Expr::getValueKindForType(DestType));

  return S.MaybeBindToTemporary(E);
}

ExprResult RebuildUnknownAnyExpr::VisitImplicitCastExpr(ImplicitCastExpr *E) {
  // The only case we should ever see here is a function-to-pointer decay.
  assert(E->getCastKind() == CK_FunctionToPointerDecay);
  assert(E->getValueKind() == VK_RValue);
  assert(E->getObjectKind() == OK_Ordinary);

  E->setType(DestType);

  // Rebuild the sub-expression as the pointee (function) type.
  DestType = DestType->castAs<PointerType>()->getPointeeType();

  ExprResult Result = Visit(E->getSubExpr());
  if (!Result.isUsable()) return ExprError();

  E->setSubExpr(Result.take());
  return S.Owned(E);
}

ExprResult RebuildUnknownAnyExpr::resolveDecl(Expr *E, ValueDecl *VD) {
  ExprValueKind ValueKind = VK_LValue;
  QualType Type = DestType;

  // We know how to make this work for certain kinds of decls:

  //  - functions
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(VD)) {
    if (const PointerType *Ptr = Type->getAs<PointerType>()) {
      DestType = Ptr->getPointeeType();
      ExprResult Result = resolveDecl(E, VD);
      if (Result.isInvalid()) return ExprError();
      return S.ImpCastExprToType(Result.take(), Type,
                                 CK_FunctionToPointerDecay, VK_RValue);
    }

    if (!Type->isFunctionType()) {
      S.Diag(E->getExprLoc(), diag::err_unknown_any_function)
        << VD << E->getSourceRange();
      return ExprError();
    }

    if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(FD))
      if (MD->isInstance()) {
        ValueKind = VK_RValue;
        Type = S.Context.BoundMemberTy;
      }

    // Function references aren't l-values in C.
    if (!S.getLangOptions().CPlusPlus)
      ValueKind = VK_RValue;

  //  - variables
  } else if (isa<VarDecl>(VD)) {
    if (const ReferenceType *RefTy = Type->getAs<ReferenceType>()) {
      Type = RefTy->getPointeeType();
    } else if (Type->isFunctionType()) {
      S.Diag(E->getExprLoc(), diag::err_unknown_any_var_function_type)
        << VD << E->getSourceRange();
      return ExprError();
    }

  //  - nothing else
  } else {
    S.Diag(E->getExprLoc(), diag::err_unsupported_unknown_any_decl)
      << VD << E->getSourceRange();
    return ExprError();
  }

  VD->setType(DestType);
  E->setType(Type);
  E->setValueKind(ValueKind);
  return S.Owned(E);
}

/// Check a cast of an unknown-any type.  We intentionally only
/// trigger this for C-style casts.
ExprResult Sema::checkUnknownAnyCast(SourceRange TypeRange, QualType CastType,
                                     Expr *CastExpr, CastKind &CastKind,
                                     ExprValueKind &VK, CXXCastPath &Path) {
  // Rewrite the casted expression from scratch.
  ExprResult result = RebuildUnknownAnyExpr(*this, CastType).Visit(CastExpr);
  if (!result.isUsable()) return ExprError();

  CastExpr = result.take();
  VK = CastExpr->getValueKind();
  CastKind = CK_NoOp;

  return CastExpr;
}

static ExprResult diagnoseUnknownAnyExpr(Sema &S, Expr *E) {
  Expr *orig = E;
  unsigned diagID = diag::err_uncasted_use_of_unknown_any;
  while (true) {
    E = E->IgnoreParenImpCasts();
    if (CallExpr *call = dyn_cast<CallExpr>(E)) {
      E = call->getCallee();
      diagID = diag::err_uncasted_call_of_unknown_any;
    } else {
      break;
    }
  }

  SourceLocation loc;
  NamedDecl *d;
  if (DeclRefExpr *ref = dyn_cast<DeclRefExpr>(E)) {
    loc = ref->getLocation();
    d = ref->getDecl();
  } else if (MemberExpr *mem = dyn_cast<MemberExpr>(E)) {
    loc = mem->getMemberLoc();
    d = mem->getMemberDecl();
  } else if (ObjCMessageExpr *msg = dyn_cast<ObjCMessageExpr>(E)) {
    diagID = diag::err_uncasted_call_of_unknown_any;
    loc = msg->getSelectorStartLoc();
    d = msg->getMethodDecl();
    if (!d) {
      S.Diag(loc, diag::err_uncasted_send_to_unknown_any_method)
        << static_cast<unsigned>(msg->isClassMessage()) << msg->getSelector()
        << orig->getSourceRange();
      return ExprError();
    }
  } else {
    S.Diag(E->getExprLoc(), diag::err_unsupported_unknown_any_expr)
      << E->getSourceRange();
    return ExprError();
  }

  S.Diag(loc, diagID) << d << orig->getSourceRange();

  // Never recoverable.
  return ExprError();
}

/// Check for operands with placeholder types and complain if found.
/// Returns true if there was an error and no recovery was possible.
ExprResult Sema::CheckPlaceholderExpr(Expr *E) {
  // Placeholder types are always *exactly* the appropriate builtin type.
  QualType type = E->getType();

  // Overloaded expressions.
  if (type == Context.OverloadTy) {
    // Try to resolve a single function template specialization.
    // This is obligatory.
    ExprResult result = Owned(E);
    if (ResolveAndFixSingleFunctionTemplateSpecialization(result, false)) {
      return result;

    // If that failed, try to recover with a call.
    } else {
      tryToRecoverWithCall(result, PDiag(diag::err_ovl_unresolvable),
                           /*complain*/ true);
      return result;
    }
  }

  // Bound member functions.
  if (type == Context.BoundMemberTy) {
    ExprResult result = Owned(E);
    tryToRecoverWithCall(result, PDiag(diag::err_bound_member_function),
                         /*complain*/ true);
    return result;
  }    

  // Expressions of unknown type.
  if (type == Context.UnknownAnyTy)
    return diagnoseUnknownAnyExpr(*this, E);

  assert(!type->isPlaceholderType());
  return Owned(E);
}

bool Sema::CheckCaseExpression(Expr *E) {
  if (E->isTypeDependent())
    return true;
  if (E->isValueDependent() || E->isIntegerConstantExpr(Context))
    return E->getType()->isIntegralOrEnumerationType();
  return false;
}
