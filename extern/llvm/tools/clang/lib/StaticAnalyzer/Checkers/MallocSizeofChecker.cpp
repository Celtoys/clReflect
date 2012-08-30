// MallocSizeofChecker.cpp - Check for dubious malloc arguments ---*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Reports inconsistencies between the casted type of the return value of a
// malloc/calloc/realloc call and the operand of any sizeof expressions
// contained within its argument(s).
//
//===----------------------------------------------------------------------===//

#include "ClangSACheckers.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TypeLoc.h"
#include "llvm/ADT/SmallString.h"

using namespace clang;
using namespace ento;

namespace {

typedef std::pair<const TypeSourceInfo *, const CallExpr *> TypeCallPair;
typedef llvm::PointerUnion<const Stmt *, const VarDecl *> ExprParent;

class CastedAllocFinder
  : public ConstStmtVisitor<CastedAllocFinder, TypeCallPair> {
  IdentifierInfo *II_malloc, *II_calloc, *II_realloc;

public:
  struct CallRecord {
    ExprParent CastedExprParent;
    const Expr *CastedExpr;
    const TypeSourceInfo *ExplicitCastType;
    const CallExpr *AllocCall;

    CallRecord(ExprParent CastedExprParent, const Expr *CastedExpr,
               const TypeSourceInfo *ExplicitCastType,
               const CallExpr *AllocCall)
      : CastedExprParent(CastedExprParent), CastedExpr(CastedExpr),
        ExplicitCastType(ExplicitCastType), AllocCall(AllocCall) {}
  };

  typedef std::vector<CallRecord> CallVec;
  CallVec Calls;

  CastedAllocFinder(ASTContext *Ctx) :
    II_malloc(&Ctx->Idents.get("malloc")),
    II_calloc(&Ctx->Idents.get("calloc")),
    II_realloc(&Ctx->Idents.get("realloc")) {}

  void VisitChild(ExprParent Parent, const Stmt *S) {
    TypeCallPair AllocCall = Visit(S);
    if (AllocCall.second && AllocCall.second != S)
      Calls.push_back(CallRecord(Parent, cast<Expr>(S), AllocCall.first,
                                 AllocCall.second));
  }

  void VisitChildren(const Stmt *S) {
    for (Stmt::const_child_iterator I = S->child_begin(), E = S->child_end();
         I!=E; ++I)
      if (const Stmt *child = *I)
        VisitChild(S, child);
  }

  TypeCallPair VisitCastExpr(const CastExpr *E) {
    return Visit(E->getSubExpr());
  }

  TypeCallPair VisitExplicitCastExpr(const ExplicitCastExpr *E) {
    return TypeCallPair(E->getTypeInfoAsWritten(),
                        Visit(E->getSubExpr()).second);
  }

  TypeCallPair VisitParenExpr(const ParenExpr *E) {
    return Visit(E->getSubExpr());
  }

  TypeCallPair VisitStmt(const Stmt *S) {
    VisitChildren(S);
    return TypeCallPair();
  }

  TypeCallPair VisitCallExpr(const CallExpr *E) {
    VisitChildren(E);
    const FunctionDecl *FD = E->getDirectCallee();
    if (FD) {
      IdentifierInfo *II = FD->getIdentifier();
      if (II == II_malloc || II == II_calloc || II == II_realloc)
        return TypeCallPair((const TypeSourceInfo *)0, E);
    }
    return TypeCallPair();
  }

  TypeCallPair VisitDeclStmt(const DeclStmt *S) {
    for (DeclStmt::const_decl_iterator I = S->decl_begin(), E = S->decl_end();
         I!=E; ++I)
      if (const VarDecl *VD = dyn_cast<VarDecl>(*I))
        if (const Expr *Init = VD->getInit())
          VisitChild(VD, Init);
    return TypeCallPair();
  }
};

class SizeofFinder : public ConstStmtVisitor<SizeofFinder> {
public:
  std::vector<const UnaryExprOrTypeTraitExpr *> Sizeofs;

  void VisitBinMul(const BinaryOperator *E) {
    Visit(E->getLHS());
    Visit(E->getRHS());
  }

  void VisitBinAdd(const BinaryOperator *E) {
    Visit(E->getLHS());
    Visit(E->getRHS());
  }

  void VisitImplicitCastExpr(const ImplicitCastExpr *E) {
    return Visit(E->getSubExpr());
  }

  void VisitParenExpr(const ParenExpr *E) {
    return Visit(E->getSubExpr());
  }

  void VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *E) {
    if (E->getKind() != UETT_SizeOf)
      return;

    Sizeofs.push_back(E);
  }
};

class MallocSizeofChecker : public Checker<check::ASTCodeBody> {
public:
  void checkASTCodeBody(const Decl *D, AnalysisManager& mgr,
                        BugReporter &BR) const {
    AnalysisDeclContext *ADC = mgr.getAnalysisDeclContext(D);
    CastedAllocFinder Finder(&BR.getContext());
    Finder.Visit(D->getBody());
    for (CastedAllocFinder::CallVec::iterator i = Finder.Calls.begin(),
         e = Finder.Calls.end(); i != e; ++i) {
      QualType CastedType = i->CastedExpr->getType();
      if (!CastedType->isPointerType())
        continue;
      QualType PointeeType = CastedType->getAs<PointerType>()->getPointeeType();
      if (PointeeType->isVoidType())
        continue;

      for (CallExpr::const_arg_iterator ai = i->AllocCall->arg_begin(),
           ae = i->AllocCall->arg_end(); ai != ae; ++ai) {
        if (!(*ai)->getType()->isIntegerType())
          continue;

        SizeofFinder SFinder;
        SFinder.Visit(*ai);
        if (SFinder.Sizeofs.size() != 1)
          continue;

        QualType SizeofType = SFinder.Sizeofs[0]->getTypeOfArgument();
        if (!BR.getContext().hasSameUnqualifiedType(PointeeType, SizeofType)) {
          const TypeSourceInfo *TSI = 0;
          if (i->CastedExprParent.is<const VarDecl *>()) {
            TSI =
              i->CastedExprParent.get<const VarDecl *>()->getTypeSourceInfo();
          } else {
            TSI = i->ExplicitCastType;
          }

          SmallString<64> buf;
          llvm::raw_svector_ostream OS(buf);

          OS << "Result of '"
             << i->AllocCall->getDirectCallee()->getIdentifier()->getName()
             << "' is converted to type '"
             << CastedType.getAsString() << "', whose pointee type '"
             << PointeeType.getAsString() << "' is incompatible with "
             << "sizeof operand type '" << SizeofType.getAsString() << "'";
          llvm::SmallVector<SourceRange, 4> Ranges;
          Ranges.push_back(i->AllocCall->getCallee()->getSourceRange());
          Ranges.push_back(SFinder.Sizeofs[0]->getSourceRange());
          if (TSI)
            Ranges.push_back(TSI->getTypeLoc().getSourceRange());

          PathDiagnosticLocation L =
            PathDiagnosticLocation::createBegin(i->AllocCall->getCallee(),
                                                BR.getSourceManager(), ADC);

          BR.EmitBasicReport(D, "allocator sizeof operand mismatch",
                             categories::UnixAPI,
                             OS.str(),
                             L, Ranges.data(), Ranges.size());
        }
      }
    }
  }
};

}

void ento::registerMallocSizeofChecker(CheckerManager &mgr) {
  mgr.registerChecker<MallocSizeofChecker>();
}
