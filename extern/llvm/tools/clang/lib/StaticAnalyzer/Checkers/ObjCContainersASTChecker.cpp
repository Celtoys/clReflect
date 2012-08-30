//== ObjCContainersASTChecker.cpp - CoreFoundation containers API *- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// An AST checker that looks for common pitfalls when using 'CFArray',
// 'CFDictionary', 'CFSet' APIs.
//
//===----------------------------------------------------------------------===//
#include "ClangSACheckers.h"
#include "clang/Analysis/AnalysisContext.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {
class WalkAST : public StmtVisitor<WalkAST> {
  BugReporter &BR;
  AnalysisDeclContext* AC;
  ASTContext &ASTC;
  uint64_t PtrWidth;

  static const unsigned InvalidArgIndex = UINT_MAX;

  /// Check if the type has pointer size (very conservative).
  inline bool isPointerSize(const Type *T) {
    if (!T)
      return true;
    if (T->isIncompleteType())
      return true;
    return (ASTC.getTypeSize(T) == PtrWidth);
  }

  /// Check if the type is a pointer/array to pointer sized values.
  inline bool hasPointerToPointerSizedType(const Expr *E) {
    QualType T = E->getType();

    // The type could be either a pointer or array.
    const Type *TP = T.getTypePtr();
    QualType PointeeT = TP->getPointeeType();
    if (!PointeeT.isNull()) {
      // If the type is a pointer to an array, check the size of the array
      // elements. To avoid false positives coming from assumption that the
      // values x and &x are equal when x is an array.
      if (const Type *TElem = PointeeT->getArrayElementTypeNoTypeQual())
        if (isPointerSize(TElem))
          return true;

      // Else, check the pointee size.
      return isPointerSize(PointeeT.getTypePtr());
    }

    if (const Type *TElem = TP->getArrayElementTypeNoTypeQual())
      return isPointerSize(TElem);

    // The type must be an array/pointer type.

    // This could be a null constant, which is allowed.
    if (E->isNullPointerConstant(ASTC, Expr::NPC_ValueDependentIsNull))
      return true;
    return false;
  }

public:
  WalkAST(BugReporter &br, AnalysisDeclContext* ac)
  : BR(br), AC(ac), ASTC(AC->getASTContext()),
    PtrWidth(ASTC.getTargetInfo().getPointerWidth(0)) {}

  // Statement visitor methods.
  void VisitChildren(Stmt *S);
  void VisitStmt(Stmt *S) { VisitChildren(S); }
  void VisitCallExpr(CallExpr *CE);
};
} // end anonymous namespace

static StringRef getCalleeName(CallExpr *CE) {
  const FunctionDecl *FD = CE->getDirectCallee();
  if (!FD)
    return StringRef();

  IdentifierInfo *II = FD->getIdentifier();
  if (!II)   // if no identifier, not a simple C function
    return StringRef();

  return II->getName();
}

void WalkAST::VisitCallExpr(CallExpr *CE) {
  StringRef Name = getCalleeName(CE);
  if (Name.empty())
    return;

  const Expr *Arg = 0;
  unsigned ArgNum = InvalidArgIndex;

  if (Name.equals("CFArrayCreate") || Name.equals("CFSetCreate")) {
    ArgNum = 1;
    Arg = CE->getArg(ArgNum)->IgnoreParenCasts();
    if (hasPointerToPointerSizedType(Arg))
        return;
  }

  if (Arg == 0 && Name.equals("CFDictionaryCreate")) {
    // Check first argument.
    ArgNum = 1;
    Arg = CE->getArg(ArgNum)->IgnoreParenCasts();
    if (hasPointerToPointerSizedType(Arg)) {
      // Check second argument.
      ArgNum = 2;
      Arg = CE->getArg(ArgNum)->IgnoreParenCasts();
      if (hasPointerToPointerSizedType(Arg))
        // Both are good, return.
        return;
    }
  }

  if (ArgNum != InvalidArgIndex) {
    assert(ArgNum == 1 || ArgNum == 2);

    SmallString<256> BufName;
    llvm::raw_svector_ostream OsName(BufName);
    assert(ArgNum == 1 || ArgNum == 2);
    OsName << " Invalid use of '" << Name << "'" ;

    SmallString<256> Buf;
    llvm::raw_svector_ostream Os(Buf);
    Os << " The "<< ((ArgNum == 1) ? "first" : "second") << " argument to '"
        << Name << "' must be a C array of pointer-sized values, not '"
        << Arg->getType().getAsString() << "'";

    SourceRange R = Arg->getSourceRange();
    PathDiagnosticLocation CELoc =
        PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);
    BR.EmitBasicReport(AC->getDecl(),
                       OsName.str(), categories::CoreFoundationObjectiveC,
                       Os.str(), CELoc, &R, 1);
  }

  // Recurse and check children.
  VisitChildren(CE);
}

void WalkAST::VisitChildren(Stmt *S) {
  for (Stmt::child_iterator I = S->child_begin(), E = S->child_end(); I!=E; ++I)
    if (Stmt *child = *I)
      Visit(child);
}

namespace {
class ObjCContainersASTChecker : public Checker<check::ASTCodeBody> {
public:

  void checkASTCodeBody(const Decl *D, AnalysisManager& Mgr,
                        BugReporter &BR) const {
    WalkAST walker(BR, Mgr.getAnalysisDeclContext(D));
    walker.Visit(D->getBody());
  }
};
}

void ento::registerObjCContainersASTChecker(CheckerManager &mgr) {
  mgr.registerChecker<ObjCContainersASTChecker>();
}
