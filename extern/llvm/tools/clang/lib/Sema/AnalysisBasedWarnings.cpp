//=- AnalysisBasedWarnings.cpp - Sema warnings based on libAnalysis -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines analysis_warnings::[Policy,Executor].
// Together they are used by Sema to issue warnings based on inexpensive
// static analysis algorithms in libAnalysis.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/AnalysisBasedWarnings.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Analysis/AnalysisContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/Analyses/ReachableCode.h"
#include "clang/Analysis/Analyses/CFGReachabilityAnalysis.h"
#include "clang/Analysis/Analyses/ThreadSafety.h"
#include "clang/Analysis/CFGStmtMap.h"
#include "clang/Analysis/Analyses/UninitializedValues.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <vector>

using namespace clang;

//===----------------------------------------------------------------------===//
// Unreachable code analysis.
//===----------------------------------------------------------------------===//

namespace {
  class UnreachableCodeHandler : public reachable_code::Callback {
    Sema &S;
  public:
    UnreachableCodeHandler(Sema &s) : S(s) {}

    void HandleUnreachable(SourceLocation L, SourceRange R1, SourceRange R2) {
      S.Diag(L, diag::warn_unreachable) << R1 << R2;
    }
  };
}

/// CheckUnreachable - Check for unreachable code.
static void CheckUnreachable(Sema &S, AnalysisDeclContext &AC) {
  UnreachableCodeHandler UC(S);
  reachable_code::FindUnreachableCode(AC, UC);
}

//===----------------------------------------------------------------------===//
// Check for missing return value.
//===----------------------------------------------------------------------===//

enum ControlFlowKind {
  UnknownFallThrough,
  NeverFallThrough,
  MaybeFallThrough,
  AlwaysFallThrough,
  NeverFallThroughOrReturn
};

/// CheckFallThrough - Check that we don't fall off the end of a
/// Statement that should return a value.
///
/// \returns AlwaysFallThrough iff we always fall off the end of the statement,
/// MaybeFallThrough iff we might or might not fall off the end,
/// NeverFallThroughOrReturn iff we never fall off the end of the statement or
/// return.  We assume NeverFallThrough iff we never fall off the end of the
/// statement but we may return.  We assume that functions not marked noreturn
/// will return.
static ControlFlowKind CheckFallThrough(AnalysisDeclContext &AC) {
  CFG *cfg = AC.getCFG();
  if (cfg == 0) return UnknownFallThrough;

  // The CFG leaves in dead things, and we don't want the dead code paths to
  // confuse us, so we mark all live things first.
  llvm::BitVector live(cfg->getNumBlockIDs());
  unsigned count = reachable_code::ScanReachableFromBlock(&cfg->getEntry(),
                                                          live);

  bool AddEHEdges = AC.getAddEHEdges();
  if (!AddEHEdges && count != cfg->getNumBlockIDs())
    // When there are things remaining dead, and we didn't add EH edges
    // from CallExprs to the catch clauses, we have to go back and
    // mark them as live.
    for (CFG::iterator I = cfg->begin(), E = cfg->end(); I != E; ++I) {
      CFGBlock &b = **I;
      if (!live[b.getBlockID()]) {
        if (b.pred_begin() == b.pred_end()) {
          if (b.getTerminator() && isa<CXXTryStmt>(b.getTerminator()))
            // When not adding EH edges from calls, catch clauses
            // can otherwise seem dead.  Avoid noting them as dead.
            count += reachable_code::ScanReachableFromBlock(&b, live);
          continue;
        }
      }
    }

  // Now we know what is live, we check the live precessors of the exit block
  // and look for fall through paths, being careful to ignore normal returns,
  // and exceptional paths.
  bool HasLiveReturn = false;
  bool HasFakeEdge = false;
  bool HasPlainEdge = false;
  bool HasAbnormalEdge = false;

  // Ignore default cases that aren't likely to be reachable because all
  // enums in a switch(X) have explicit case statements.
  CFGBlock::FilterOptions FO;
  FO.IgnoreDefaultsWithCoveredEnums = 1;

  for (CFGBlock::filtered_pred_iterator
	 I = cfg->getExit().filtered_pred_start_end(FO); I.hasMore(); ++I) {
    const CFGBlock& B = **I;
    if (!live[B.getBlockID()])
      continue;

    // Skip blocks which contain an element marked as no-return. They don't
    // represent actually viable edges into the exit block, so mark them as
    // abnormal.
    if (B.hasNoReturnElement()) {
      HasAbnormalEdge = true;
      continue;
    }

    // Destructors can appear after the 'return' in the CFG.  This is
    // normal.  We need to look pass the destructors for the return
    // statement (if it exists).
    CFGBlock::const_reverse_iterator ri = B.rbegin(), re = B.rend();

    for ( ; ri != re ; ++ri)
      if (isa<CFGStmt>(*ri))
        break;

    // No more CFGElements in the block?
    if (ri == re) {
      if (B.getTerminator() && isa<CXXTryStmt>(B.getTerminator())) {
        HasAbnormalEdge = true;
        continue;
      }
      // A labeled empty statement, or the entry block...
      HasPlainEdge = true;
      continue;
    }

    CFGStmt CS = cast<CFGStmt>(*ri);
    const Stmt *S = CS.getStmt();
    if (isa<ReturnStmt>(S)) {
      HasLiveReturn = true;
      continue;
    }
    if (isa<ObjCAtThrowStmt>(S)) {
      HasFakeEdge = true;
      continue;
    }
    if (isa<CXXThrowExpr>(S)) {
      HasFakeEdge = true;
      continue;
    }
    if (const AsmStmt *AS = dyn_cast<AsmStmt>(S)) {
      if (AS->isMSAsm()) {
        HasFakeEdge = true;
        HasLiveReturn = true;
        continue;
      }
    }
    if (isa<CXXTryStmt>(S)) {
      HasAbnormalEdge = true;
      continue;
    }
    if (std::find(B.succ_begin(), B.succ_end(), &cfg->getExit())
        == B.succ_end()) {
      HasAbnormalEdge = true;
      continue;
    }

    HasPlainEdge = true;
  }
  if (!HasPlainEdge) {
    if (HasLiveReturn)
      return NeverFallThrough;
    return NeverFallThroughOrReturn;
  }
  if (HasAbnormalEdge || HasFakeEdge || HasLiveReturn)
    return MaybeFallThrough;
  // This says AlwaysFallThrough for calls to functions that are not marked
  // noreturn, that don't return.  If people would like this warning to be more
  // accurate, such functions should be marked as noreturn.
  return AlwaysFallThrough;
}

namespace {

struct CheckFallThroughDiagnostics {
  unsigned diag_MaybeFallThrough_HasNoReturn;
  unsigned diag_MaybeFallThrough_ReturnsNonVoid;
  unsigned diag_AlwaysFallThrough_HasNoReturn;
  unsigned diag_AlwaysFallThrough_ReturnsNonVoid;
  unsigned diag_NeverFallThroughOrReturn;
  enum { Function, Block, Lambda } funMode;
  SourceLocation FuncLoc;

  static CheckFallThroughDiagnostics MakeForFunction(const Decl *Func) {
    CheckFallThroughDiagnostics D;
    D.FuncLoc = Func->getLocation();
    D.diag_MaybeFallThrough_HasNoReturn =
      diag::warn_falloff_noreturn_function;
    D.diag_MaybeFallThrough_ReturnsNonVoid =
      diag::warn_maybe_falloff_nonvoid_function;
    D.diag_AlwaysFallThrough_HasNoReturn =
      diag::warn_falloff_noreturn_function;
    D.diag_AlwaysFallThrough_ReturnsNonVoid =
      diag::warn_falloff_nonvoid_function;

    // Don't suggest that virtual functions be marked "noreturn", since they
    // might be overridden by non-noreturn functions.
    bool isVirtualMethod = false;
    if (const CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(Func))
      isVirtualMethod = Method->isVirtual();
    
    // Don't suggest that template instantiations be marked "noreturn"
    bool isTemplateInstantiation = false;
    if (const FunctionDecl *Function = dyn_cast<FunctionDecl>(Func))
      isTemplateInstantiation = Function->isTemplateInstantiation();
        
    if (!isVirtualMethod && !isTemplateInstantiation)
      D.diag_NeverFallThroughOrReturn =
        diag::warn_suggest_noreturn_function;
    else
      D.diag_NeverFallThroughOrReturn = 0;
    
    D.funMode = Function;
    return D;
  }

  static CheckFallThroughDiagnostics MakeForBlock() {
    CheckFallThroughDiagnostics D;
    D.diag_MaybeFallThrough_HasNoReturn =
      diag::err_noreturn_block_has_return_expr;
    D.diag_MaybeFallThrough_ReturnsNonVoid =
      diag::err_maybe_falloff_nonvoid_block;
    D.diag_AlwaysFallThrough_HasNoReturn =
      diag::err_noreturn_block_has_return_expr;
    D.diag_AlwaysFallThrough_ReturnsNonVoid =
      diag::err_falloff_nonvoid_block;
    D.diag_NeverFallThroughOrReturn =
      diag::warn_suggest_noreturn_block;
    D.funMode = Block;
    return D;
  }

  static CheckFallThroughDiagnostics MakeForLambda() {
    CheckFallThroughDiagnostics D;
    D.diag_MaybeFallThrough_HasNoReturn =
      diag::err_noreturn_lambda_has_return_expr;
    D.diag_MaybeFallThrough_ReturnsNonVoid =
      diag::warn_maybe_falloff_nonvoid_lambda;
    D.diag_AlwaysFallThrough_HasNoReturn =
      diag::err_noreturn_lambda_has_return_expr;
    D.diag_AlwaysFallThrough_ReturnsNonVoid =
      diag::warn_falloff_nonvoid_lambda;
    D.diag_NeverFallThroughOrReturn = 0;
    D.funMode = Lambda;
    return D;
  }

  bool checkDiagnostics(DiagnosticsEngine &D, bool ReturnsVoid,
                        bool HasNoReturn) const {
    if (funMode == Function) {
      return (ReturnsVoid ||
              D.getDiagnosticLevel(diag::warn_maybe_falloff_nonvoid_function,
                                   FuncLoc) == DiagnosticsEngine::Ignored)
        && (!HasNoReturn ||
            D.getDiagnosticLevel(diag::warn_noreturn_function_has_return_expr,
                                 FuncLoc) == DiagnosticsEngine::Ignored)
        && (!ReturnsVoid ||
            D.getDiagnosticLevel(diag::warn_suggest_noreturn_block, FuncLoc)
              == DiagnosticsEngine::Ignored);
    }

    // For blocks / lambdas.
    return ReturnsVoid && !HasNoReturn
            && ((funMode == Lambda) ||
                D.getDiagnosticLevel(diag::warn_suggest_noreturn_block, FuncLoc)
                  == DiagnosticsEngine::Ignored);
  }
};

}

/// CheckFallThroughForFunctionDef - Check that we don't fall off the end of a
/// function that should return a value.  Check that we don't fall off the end
/// of a noreturn function.  We assume that functions and blocks not marked
/// noreturn will return.
static void CheckFallThroughForBody(Sema &S, const Decl *D, const Stmt *Body,
                                    const BlockExpr *blkExpr,
                                    const CheckFallThroughDiagnostics& CD,
                                    AnalysisDeclContext &AC) {

  bool ReturnsVoid = false;
  bool HasNoReturn = false;

  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    ReturnsVoid = FD->getResultType()->isVoidType();
    HasNoReturn = FD->hasAttr<NoReturnAttr>() ||
       FD->getType()->getAs<FunctionType>()->getNoReturnAttr();
  }
  else if (const ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(D)) {
    ReturnsVoid = MD->getResultType()->isVoidType();
    HasNoReturn = MD->hasAttr<NoReturnAttr>();
  }
  else if (isa<BlockDecl>(D)) {
    QualType BlockTy = blkExpr->getType();
    if (const FunctionType *FT =
          BlockTy->getPointeeType()->getAs<FunctionType>()) {
      if (FT->getResultType()->isVoidType())
        ReturnsVoid = true;
      if (FT->getNoReturnAttr())
        HasNoReturn = true;
    }
  }

  DiagnosticsEngine &Diags = S.getDiagnostics();

  // Short circuit for compilation speed.
  if (CD.checkDiagnostics(Diags, ReturnsVoid, HasNoReturn))
      return;

  // FIXME: Function try block
  if (const CompoundStmt *Compound = dyn_cast<CompoundStmt>(Body)) {
    switch (CheckFallThrough(AC)) {
      case UnknownFallThrough:
        break;

      case MaybeFallThrough:
        if (HasNoReturn)
          S.Diag(Compound->getRBracLoc(),
                 CD.diag_MaybeFallThrough_HasNoReturn);
        else if (!ReturnsVoid)
          S.Diag(Compound->getRBracLoc(),
                 CD.diag_MaybeFallThrough_ReturnsNonVoid);
        break;
      case AlwaysFallThrough:
        if (HasNoReturn)
          S.Diag(Compound->getRBracLoc(),
                 CD.diag_AlwaysFallThrough_HasNoReturn);
        else if (!ReturnsVoid)
          S.Diag(Compound->getRBracLoc(),
                 CD.diag_AlwaysFallThrough_ReturnsNonVoid);
        break;
      case NeverFallThroughOrReturn:
        if (ReturnsVoid && !HasNoReturn && CD.diag_NeverFallThroughOrReturn) {
          if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
            S.Diag(Compound->getLBracLoc(), CD.diag_NeverFallThroughOrReturn)
              << 0 << FD;
          } else if (const ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(D)) {
            S.Diag(Compound->getLBracLoc(), CD.diag_NeverFallThroughOrReturn)
              << 1 << MD;
          } else {
            S.Diag(Compound->getLBracLoc(), CD.diag_NeverFallThroughOrReturn);
          }
        }
        break;
      case NeverFallThrough:
        break;
    }
  }
}

//===----------------------------------------------------------------------===//
// -Wuninitialized
//===----------------------------------------------------------------------===//

namespace {
/// ContainsReference - A visitor class to search for references to
/// a particular declaration (the needle) within any evaluated component of an
/// expression (recursively).
class ContainsReference : public EvaluatedExprVisitor<ContainsReference> {
  bool FoundReference;
  const DeclRefExpr *Needle;

public:
  ContainsReference(ASTContext &Context, const DeclRefExpr *Needle)
    : EvaluatedExprVisitor<ContainsReference>(Context),
      FoundReference(false), Needle(Needle) {}

  void VisitExpr(Expr *E) {
    // Stop evaluating if we already have a reference.
    if (FoundReference)
      return;

    EvaluatedExprVisitor<ContainsReference>::VisitExpr(E);
  }

  void VisitDeclRefExpr(DeclRefExpr *E) {
    if (E == Needle)
      FoundReference = true;
    else
      EvaluatedExprVisitor<ContainsReference>::VisitDeclRefExpr(E);
  }

  bool doesContainReference() const { return FoundReference; }
};
}

static bool SuggestInitializationFixit(Sema &S, const VarDecl *VD) {
  QualType VariableTy = VD->getType().getCanonicalType();
  if (VariableTy->isBlockPointerType() &&
      !VD->hasAttr<BlocksAttr>()) {
    S.Diag(VD->getLocation(), diag::note_block_var_fixit_add_initialization) << VD->getDeclName()
    << FixItHint::CreateInsertion(VD->getLocation(), "__block ");
    return true;
  }
  
  // Don't issue a fixit if there is already an initializer.
  if (VD->getInit())
    return false;
  
  // Suggest possible initialization (if any).
  const char *Init = S.getFixItZeroInitializerForType(VariableTy);
  if (!Init)
    return false;
  SourceLocation Loc = S.PP.getLocForEndOfToken(VD->getLocEnd());
  
  S.Diag(Loc, diag::note_var_fixit_add_initialization) << VD->getDeclName()
    << FixItHint::CreateInsertion(Loc, Init);
  return true;
}

/// DiagnoseUninitializedUse -- Helper function for diagnosing uses of an
/// uninitialized variable. This manages the different forms of diagnostic
/// emitted for particular types of uses. Returns true if the use was diagnosed
/// as a warning. If a pariticular use is one we omit warnings for, returns
/// false.
static bool DiagnoseUninitializedUse(Sema &S, const VarDecl *VD,
                                     const Expr *E, bool isAlwaysUninit,
                                     bool alwaysReportSelfInit = false) {
  bool isSelfInit = false;

  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (isAlwaysUninit) {
      // Inspect the initializer of the variable declaration which is
      // being referenced prior to its initialization. We emit
      // specialized diagnostics for self-initialization, and we
      // specifically avoid warning about self references which take the
      // form of:
      //
      //   int x = x;
      //
      // This is used to indicate to GCC that 'x' is intentionally left
      // uninitialized. Proven code paths which access 'x' in
      // an uninitialized state after this will still warn.
      //
      // TODO: Should we suppress maybe-uninitialized warnings for
      // variables initialized in this way?
      if (const Expr *Initializer = VD->getInit()) {
        if (!alwaysReportSelfInit && DRE == Initializer->IgnoreParenImpCasts())
          return false;

        ContainsReference CR(S.Context, DRE);
        CR.Visit(const_cast<Expr*>(Initializer));
        isSelfInit = CR.doesContainReference();
      }
      if (isSelfInit) {
        S.Diag(DRE->getLocStart(),
               diag::warn_uninit_self_reference_in_init)
        << VD->getDeclName() << VD->getLocation() << DRE->getSourceRange();
      } else {
        S.Diag(DRE->getLocStart(), diag::warn_uninit_var)
          << VD->getDeclName() << DRE->getSourceRange();
      }
    } else {
      S.Diag(DRE->getLocStart(), diag::warn_maybe_uninit_var)
        << VD->getDeclName() << DRE->getSourceRange();
    }
  } else {
    const BlockExpr *BE = cast<BlockExpr>(E);
    if (VD->getType()->isBlockPointerType() &&
        !VD->hasAttr<BlocksAttr>())
      S.Diag(BE->getLocStart(), diag::warn_uninit_byref_blockvar_captured_by_block)
        << VD->getDeclName();
    else
      S.Diag(BE->getLocStart(),
             isAlwaysUninit ? diag::warn_uninit_var_captured_by_block
                            : diag::warn_maybe_uninit_var_captured_by_block)
        << VD->getDeclName();
  }

  // Report where the variable was declared when the use wasn't within
  // the initializer of that declaration & we didn't already suggest
  // an initialization fixit.
  if (!isSelfInit && !SuggestInitializationFixit(S, VD))
    S.Diag(VD->getLocStart(), diag::note_uninit_var_def)
      << VD->getDeclName();

  return true;
}

typedef std::pair<const Expr*, bool> UninitUse;

namespace {
struct SLocSort {
  bool operator()(const UninitUse &a, const UninitUse &b) {
    SourceLocation aLoc = a.first->getLocStart();
    SourceLocation bLoc = b.first->getLocStart();
    return aLoc.getRawEncoding() < bLoc.getRawEncoding();
  }
};

class UninitValsDiagReporter : public UninitVariablesHandler {
  Sema &S;
  typedef SmallVector<UninitUse, 2> UsesVec;
  typedef llvm::DenseMap<const VarDecl *, std::pair<UsesVec*, bool> > UsesMap;
  UsesMap *uses;
  
public:
  UninitValsDiagReporter(Sema &S) : S(S), uses(0) {}
  ~UninitValsDiagReporter() { 
    flushDiagnostics();
  }

  std::pair<UsesVec*, bool> &getUses(const VarDecl *vd) {
    if (!uses)
      uses = new UsesMap();

    UsesMap::mapped_type &V = (*uses)[vd];
    UsesVec *&vec = V.first;
    if (!vec)
      vec = new UsesVec();
    
    return V;
  }
  
  void handleUseOfUninitVariable(const Expr *ex, const VarDecl *vd,
                                 bool isAlwaysUninit) {
    getUses(vd).first->push_back(std::make_pair(ex, isAlwaysUninit));
  }
  
  void handleSelfInit(const VarDecl *vd) {
    getUses(vd).second = true;    
  }
  
  void flushDiagnostics() {
    if (!uses)
      return;
    
    for (UsesMap::iterator i = uses->begin(), e = uses->end(); i != e; ++i) {
      const VarDecl *vd = i->first;
      const UsesMap::mapped_type &V = i->second;

      UsesVec *vec = V.first;
      bool hasSelfInit = V.second;

      // Specially handle the case where we have uses of an uninitialized 
      // variable, but the root cause is an idiomatic self-init.  We want
      // to report the diagnostic at the self-init since that is the root cause.
      if (!vec->empty() && hasSelfInit && hasAlwaysUninitializedUse(vec))
        DiagnoseUninitializedUse(S, vd, vd->getInit()->IgnoreParenCasts(),
                                 /* isAlwaysUninit */ true,
                                 /* alwaysReportSelfInit */ true);
      else {
        // Sort the uses by their SourceLocations.  While not strictly
        // guaranteed to produce them in line/column order, this will provide
        // a stable ordering.
        std::sort(vec->begin(), vec->end(), SLocSort());
        
        for (UsesVec::iterator vi = vec->begin(), ve = vec->end(); vi != ve;
             ++vi) {
          if (DiagnoseUninitializedUse(S, vd, vi->first,
                                        /*isAlwaysUninit=*/vi->second))
            // Skip further diagnostics for this variable. We try to warn only
            // on the first point at which a variable is used uninitialized.
            break;
        }
      }
      
      // Release the uses vector.
      delete vec;
    }
    delete uses;
  }

private:
  static bool hasAlwaysUninitializedUse(const UsesVec* vec) {
  for (UsesVec::const_iterator i = vec->begin(), e = vec->end(); i != e; ++i) {
    if (i->second) {
      return true;
    }
  }
  return false;
}
};
}


//===----------------------------------------------------------------------===//
// -Wthread-safety
//===----------------------------------------------------------------------===//
namespace clang {
namespace thread_safety {
typedef llvm::SmallVector<PartialDiagnosticAt, 1> OptionalNotes;
typedef std::pair<PartialDiagnosticAt, OptionalNotes> DelayedDiag;
typedef std::list<DelayedDiag> DiagList;

struct SortDiagBySourceLocation {
  SourceManager &SM;
  SortDiagBySourceLocation(SourceManager &SM) : SM(SM) {}

  bool operator()(const DelayedDiag &left, const DelayedDiag &right) {
    // Although this call will be slow, this is only called when outputting
    // multiple warnings.
    return SM.isBeforeInTranslationUnit(left.first.first, right.first.first);
  }
};

namespace {
class ThreadSafetyReporter : public clang::thread_safety::ThreadSafetyHandler {
  Sema &S;
  DiagList Warnings;
  SourceLocation FunLocation, FunEndLocation;

  // Helper functions
  void warnLockMismatch(unsigned DiagID, Name LockName, SourceLocation Loc) {
    // Gracefully handle rare cases when the analysis can't get a more
    // precise source location.
    if (!Loc.isValid())
      Loc = FunLocation;
    PartialDiagnosticAt Warning(Loc, S.PDiag(DiagID) << LockName);
    Warnings.push_back(DelayedDiag(Warning, OptionalNotes()));
  }

 public:
  ThreadSafetyReporter(Sema &S, SourceLocation FL, SourceLocation FEL)
    : S(S), FunLocation(FL), FunEndLocation(FEL) {}

  /// \brief Emit all buffered diagnostics in order of sourcelocation.
  /// We need to output diagnostics produced while iterating through
  /// the lockset in deterministic order, so this function orders diagnostics
  /// and outputs them.
  void emitDiagnostics() {
    Warnings.sort(SortDiagBySourceLocation(S.getSourceManager()));
    for (DiagList::iterator I = Warnings.begin(), E = Warnings.end();
         I != E; ++I) {
      S.Diag(I->first.first, I->first.second);
      const OptionalNotes &Notes = I->second;
      for (unsigned NoteI = 0, NoteN = Notes.size(); NoteI != NoteN; ++NoteI)
        S.Diag(Notes[NoteI].first, Notes[NoteI].second);
    }
  }

  void handleInvalidLockExp(SourceLocation Loc) {
    PartialDiagnosticAt Warning(Loc,
                                S.PDiag(diag::warn_cannot_resolve_lock) << Loc);
    Warnings.push_back(DelayedDiag(Warning, OptionalNotes()));
  }
  void handleUnmatchedUnlock(Name LockName, SourceLocation Loc) {
    warnLockMismatch(diag::warn_unlock_but_no_lock, LockName, Loc);
  }

  void handleDoubleLock(Name LockName, SourceLocation Loc) {
    warnLockMismatch(diag::warn_double_lock, LockName, Loc);
  }

  void handleMutexHeldEndOfScope(Name LockName, SourceLocation LocLocked,
                                 SourceLocation LocEndOfScope,
                                 LockErrorKind LEK){
    unsigned DiagID = 0;
    switch (LEK) {
      case LEK_LockedSomePredecessors:
        DiagID = diag::warn_lock_some_predecessors;
        break;
      case LEK_LockedSomeLoopIterations:
        DiagID = diag::warn_expecting_lock_held_on_loop;
        break;
      case LEK_LockedAtEndOfFunction:
        DiagID = diag::warn_no_unlock;
        break;
    }
    if (LocEndOfScope.isInvalid())
      LocEndOfScope = FunEndLocation;

    PartialDiagnosticAt Warning(LocEndOfScope, S.PDiag(DiagID) << LockName);
    PartialDiagnosticAt Note(LocLocked, S.PDiag(diag::note_locked_here));
    Warnings.push_back(DelayedDiag(Warning, OptionalNotes(1, Note)));
  }


  void handleExclusiveAndShared(Name LockName, SourceLocation Loc1,
                                SourceLocation Loc2) {
    PartialDiagnosticAt Warning(
      Loc1, S.PDiag(diag::warn_lock_exclusive_and_shared) << LockName);
    PartialDiagnosticAt Note(
      Loc2, S.PDiag(diag::note_lock_exclusive_and_shared) << LockName);
    Warnings.push_back(DelayedDiag(Warning, OptionalNotes(1, Note)));
  }

  void handleNoMutexHeld(const NamedDecl *D, ProtectedOperationKind POK,
                         AccessKind AK, SourceLocation Loc) {
    assert((POK == POK_VarAccess || POK == POK_VarDereference)
             && "Only works for variables");
    unsigned DiagID = POK == POK_VarAccess?
                        diag::warn_variable_requires_any_lock:
                        diag::warn_var_deref_requires_any_lock;
    PartialDiagnosticAt Warning(Loc, S.PDiag(DiagID)
      << D->getName() << getLockKindFromAccessKind(AK));
    Warnings.push_back(DelayedDiag(Warning, OptionalNotes()));
  }

  void handleMutexNotHeld(const NamedDecl *D, ProtectedOperationKind POK,
                          Name LockName, LockKind LK, SourceLocation Loc) {
    unsigned DiagID = 0;
    switch (POK) {
      case POK_VarAccess:
        DiagID = diag::warn_variable_requires_lock;
        break;
      case POK_VarDereference:
        DiagID = diag::warn_var_deref_requires_lock;
        break;
      case POK_FunctionCall:
        DiagID = diag::warn_fun_requires_lock;
        break;
    }
    PartialDiagnosticAt Warning(Loc, S.PDiag(DiagID)
      << D->getName() << LockName << LK);
    Warnings.push_back(DelayedDiag(Warning, OptionalNotes()));
  }

  void handleFunExcludesLock(Name FunName, Name LockName, SourceLocation Loc) {
    PartialDiagnosticAt Warning(Loc,
      S.PDiag(diag::warn_fun_excludes_mutex) << FunName << LockName);
    Warnings.push_back(DelayedDiag(Warning, OptionalNotes()));
  }
};
}
}
}

//===----------------------------------------------------------------------===//
// AnalysisBasedWarnings - Worker object used by Sema to execute analysis-based
//  warnings on a function, method, or block.
//===----------------------------------------------------------------------===//

clang::sema::AnalysisBasedWarnings::Policy::Policy() {
  enableCheckFallThrough = 1;
  enableCheckUnreachable = 0;
  enableThreadSafetyAnalysis = 0;
}

clang::sema::AnalysisBasedWarnings::AnalysisBasedWarnings(Sema &s)
  : S(s),
    NumFunctionsAnalyzed(0),
    NumFunctionsWithBadCFGs(0),
    NumCFGBlocks(0),
    MaxCFGBlocksPerFunction(0),
    NumUninitAnalysisFunctions(0),
    NumUninitAnalysisVariables(0),
    MaxUninitAnalysisVariablesPerFunction(0),
    NumUninitAnalysisBlockVisits(0),
    MaxUninitAnalysisBlockVisitsPerFunction(0) {
  DiagnosticsEngine &D = S.getDiagnostics();
  DefaultPolicy.enableCheckUnreachable = (unsigned)
    (D.getDiagnosticLevel(diag::warn_unreachable, SourceLocation()) !=
        DiagnosticsEngine::Ignored);
  DefaultPolicy.enableThreadSafetyAnalysis = (unsigned)
    (D.getDiagnosticLevel(diag::warn_double_lock, SourceLocation()) !=
     DiagnosticsEngine::Ignored);

}

static void flushDiagnostics(Sema &S, sema::FunctionScopeInfo *fscope) {
  for (SmallVectorImpl<sema::PossiblyUnreachableDiag>::iterator
       i = fscope->PossiblyUnreachableDiags.begin(),
       e = fscope->PossiblyUnreachableDiags.end();
       i != e; ++i) {
    const sema::PossiblyUnreachableDiag &D = *i;
    S.Diag(D.Loc, D.PD);
  }
}

void clang::sema::
AnalysisBasedWarnings::IssueWarnings(sema::AnalysisBasedWarnings::Policy P,
                                     sema::FunctionScopeInfo *fscope,
                                     const Decl *D, const BlockExpr *blkExpr) {

  // We avoid doing analysis-based warnings when there are errors for
  // two reasons:
  // (1) The CFGs often can't be constructed (if the body is invalid), so
  //     don't bother trying.
  // (2) The code already has problems; running the analysis just takes more
  //     time.
  DiagnosticsEngine &Diags = S.getDiagnostics();

  // Do not do any analysis for declarations in system headers if we are
  // going to just ignore them.
  if (Diags.getSuppressSystemWarnings() &&
      S.SourceMgr.isInSystemHeader(D->getLocation()))
    return;

  // For code in dependent contexts, we'll do this at instantiation time.
  if (cast<DeclContext>(D)->isDependentContext())
    return;

  if (Diags.hasErrorOccurred() || Diags.hasFatalErrorOccurred()) {
    // Flush out any possibly unreachable diagnostics.
    flushDiagnostics(S, fscope);
    return;
  }
  
  const Stmt *Body = D->getBody();
  assert(Body);

  AnalysisDeclContext AC(/* AnalysisDeclContextManager */ 0,  D, 0);

  // Don't generate EH edges for CallExprs as we'd like to avoid the n^2
  // explosion for destrutors that can result and the compile time hit.
  AC.getCFGBuildOptions().PruneTriviallyFalseEdges = true;
  AC.getCFGBuildOptions().AddEHEdges = false;
  AC.getCFGBuildOptions().AddInitializers = true;
  AC.getCFGBuildOptions().AddImplicitDtors = true;
  
  // Force that certain expressions appear as CFGElements in the CFG.  This
  // is used to speed up various analyses.
  // FIXME: This isn't the right factoring.  This is here for initial
  // prototyping, but we need a way for analyses to say what expressions they
  // expect to always be CFGElements and then fill in the BuildOptions
  // appropriately.  This is essentially a layering violation.
  if (P.enableCheckUnreachable || P.enableThreadSafetyAnalysis) {
    // Unreachable code analysis and thread safety require a linearized CFG.
    AC.getCFGBuildOptions().setAllAlwaysAdd();
  }
  else {
    AC.getCFGBuildOptions()
      .setAlwaysAdd(Stmt::BinaryOperatorClass)
      .setAlwaysAdd(Stmt::BlockExprClass)
      .setAlwaysAdd(Stmt::CStyleCastExprClass)
      .setAlwaysAdd(Stmt::DeclRefExprClass)
      .setAlwaysAdd(Stmt::ImplicitCastExprClass)
      .setAlwaysAdd(Stmt::UnaryOperatorClass);
  }

  // Construct the analysis context with the specified CFG build options.
  
  // Emit delayed diagnostics.
  if (!fscope->PossiblyUnreachableDiags.empty()) {
    bool analyzed = false;

    // Register the expressions with the CFGBuilder.
    for (SmallVectorImpl<sema::PossiblyUnreachableDiag>::iterator
         i = fscope->PossiblyUnreachableDiags.begin(),
         e = fscope->PossiblyUnreachableDiags.end();
         i != e; ++i) {
      if (const Stmt *stmt = i->stmt)
        AC.registerForcedBlockExpression(stmt);
    }

    if (AC.getCFG()) {
      analyzed = true;
      for (SmallVectorImpl<sema::PossiblyUnreachableDiag>::iterator
            i = fscope->PossiblyUnreachableDiags.begin(),
            e = fscope->PossiblyUnreachableDiags.end();
            i != e; ++i)
      {
        const sema::PossiblyUnreachableDiag &D = *i;
        bool processed = false;
        if (const Stmt *stmt = i->stmt) {
          const CFGBlock *block = AC.getBlockForRegisteredExpression(stmt);
          CFGReverseBlockReachabilityAnalysis *cra =
              AC.getCFGReachablityAnalysis();
          // FIXME: We should be able to assert that block is non-null, but
          // the CFG analysis can skip potentially-evaluated expressions in
          // edge cases; see test/Sema/vla-2.c.
          if (block && cra) {
            // Can this block be reached from the entrance?
            if (cra->isReachable(&AC.getCFG()->getEntry(), block))
              S.Diag(D.Loc, D.PD);
            processed = true;
          }
        }
        if (!processed) {
          // Emit the warning anyway if we cannot map to a basic block.
          S.Diag(D.Loc, D.PD);
        }
      }
    }

    if (!analyzed)
      flushDiagnostics(S, fscope);
  }
  
  
  // Warning: check missing 'return'
  if (P.enableCheckFallThrough) {
    const CheckFallThroughDiagnostics &CD =
      (isa<BlockDecl>(D) ? CheckFallThroughDiagnostics::MakeForBlock()
       : (isa<CXXMethodDecl>(D) &&
          cast<CXXMethodDecl>(D)->getOverloadedOperator() == OO_Call &&
          cast<CXXMethodDecl>(D)->getParent()->isLambda())
            ? CheckFallThroughDiagnostics::MakeForLambda()
            : CheckFallThroughDiagnostics::MakeForFunction(D));
    CheckFallThroughForBody(S, D, Body, blkExpr, CD, AC);
  }

  // Warning: check for unreachable code
  if (P.enableCheckUnreachable) {
    // Only check for unreachable code on non-template instantiations.
    // Different template instantiations can effectively change the control-flow
    // and it is very difficult to prove that a snippet of code in a template
    // is unreachable for all instantiations.
    bool isTemplateInstantiation = false;
    if (const FunctionDecl *Function = dyn_cast<FunctionDecl>(D))
      isTemplateInstantiation = Function->isTemplateInstantiation();
    if (!isTemplateInstantiation)
      CheckUnreachable(S, AC);
  }

  // Check for thread safety violations
  if (P.enableThreadSafetyAnalysis) {
    SourceLocation FL = AC.getDecl()->getLocation();
    SourceLocation FEL = AC.getDecl()->getLocEnd();
    thread_safety::ThreadSafetyReporter Reporter(S, FL, FEL);
    thread_safety::runThreadSafetyAnalysis(AC, Reporter);
    Reporter.emitDiagnostics();
  }

  if (Diags.getDiagnosticLevel(diag::warn_uninit_var, D->getLocStart())
      != DiagnosticsEngine::Ignored ||
      Diags.getDiagnosticLevel(diag::warn_maybe_uninit_var, D->getLocStart())
      != DiagnosticsEngine::Ignored) {
    if (CFG *cfg = AC.getCFG()) {
      UninitValsDiagReporter reporter(S);
      UninitVariablesAnalysisStats stats;
      std::memset(&stats, 0, sizeof(UninitVariablesAnalysisStats));
      runUninitializedVariablesAnalysis(*cast<DeclContext>(D), *cfg, AC,
                                        reporter, stats);

      if (S.CollectStats && stats.NumVariablesAnalyzed > 0) {
        ++NumUninitAnalysisFunctions;
        NumUninitAnalysisVariables += stats.NumVariablesAnalyzed;
        NumUninitAnalysisBlockVisits += stats.NumBlockVisits;
        MaxUninitAnalysisVariablesPerFunction =
            std::max(MaxUninitAnalysisVariablesPerFunction,
                     stats.NumVariablesAnalyzed);
        MaxUninitAnalysisBlockVisitsPerFunction =
            std::max(MaxUninitAnalysisBlockVisitsPerFunction,
                     stats.NumBlockVisits);
      }
    }
  }

  // Collect statistics about the CFG if it was built.
  if (S.CollectStats && AC.isCFGBuilt()) {
    ++NumFunctionsAnalyzed;
    if (CFG *cfg = AC.getCFG()) {
      // If we successfully built a CFG for this context, record some more
      // detail information about it.
      NumCFGBlocks += cfg->getNumBlockIDs();
      MaxCFGBlocksPerFunction = std::max(MaxCFGBlocksPerFunction,
                                         cfg->getNumBlockIDs());
    } else {
      ++NumFunctionsWithBadCFGs;
    }
  }
}

void clang::sema::AnalysisBasedWarnings::PrintStats() const {
  llvm::errs() << "\n*** Analysis Based Warnings Stats:\n";

  unsigned NumCFGsBuilt = NumFunctionsAnalyzed - NumFunctionsWithBadCFGs;
  unsigned AvgCFGBlocksPerFunction =
      !NumCFGsBuilt ? 0 : NumCFGBlocks/NumCFGsBuilt;
  llvm::errs() << NumFunctionsAnalyzed << " functions analyzed ("
               << NumFunctionsWithBadCFGs << " w/o CFGs).\n"
               << "  " << NumCFGBlocks << " CFG blocks built.\n"
               << "  " << AvgCFGBlocksPerFunction
               << " average CFG blocks per function.\n"
               << "  " << MaxCFGBlocksPerFunction
               << " max CFG blocks per function.\n";

  unsigned AvgUninitVariablesPerFunction = !NumUninitAnalysisFunctions ? 0
      : NumUninitAnalysisVariables/NumUninitAnalysisFunctions;
  unsigned AvgUninitBlockVisitsPerFunction = !NumUninitAnalysisFunctions ? 0
      : NumUninitAnalysisBlockVisits/NumUninitAnalysisFunctions;
  llvm::errs() << NumUninitAnalysisFunctions
               << " functions analyzed for uninitialiazed variables\n"
               << "  " << NumUninitAnalysisVariables << " variables analyzed.\n"
               << "  " << AvgUninitVariablesPerFunction
               << " average variables per function.\n"
               << "  " << MaxUninitAnalysisVariablesPerFunction
               << " max variables per function.\n"
               << "  " << NumUninitAnalysisBlockVisits << " block visits.\n"
               << "  " << AvgUninitBlockVisitsPerFunction
               << " average block visits per function.\n"
               << "  " << MaxUninitAnalysisBlockVisitsPerFunction
               << " max block visits per function.\n";
}
