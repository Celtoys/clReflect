//=-- AggExprVisitor.cpp - evaluating expressions of C++ class type -*- C++ -*-=
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines AggExprVisitor class, which contains lots of boiler
// plate code for evaluating expressions of C++ class type.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "clang/AST/StmtVisitor.h"

using namespace clang;
using namespace ento;

namespace {
/// AggExprVisitor is designed after AggExprEmitter of the CodeGen module.  It
/// is used for evaluating exprs of C++ object type. Evaluating such exprs
/// requires a destination pointer pointing to the object being evaluated
/// into. Passing such a pointer around would pollute the Visit* interface of
/// ExprEngine. AggExprVisitor encapsulates code that goes through various
/// cast and construct exprs (and others), and at the final point, dispatches
/// back to the ExprEngine to let the real evaluation logic happen.
class AggExprVisitor : public StmtVisitor<AggExprVisitor> {
  const MemRegion *Dest;
  ExplodedNode *Pred;
  ExplodedNodeSet &DstSet;
  ExprEngine &Eng;

public:
  AggExprVisitor(const MemRegion *dest, ExplodedNode *N, ExplodedNodeSet &dst, 
                 ExprEngine &eng)
    : Dest(dest), Pred(N), DstSet(dst), Eng(eng) {}

  void VisitCastExpr(CastExpr *E);
  void VisitCXXConstructExpr(CXXConstructExpr *E);
  void VisitCXXMemberCallExpr(CXXMemberCallExpr *E);
};
}

void AggExprVisitor::VisitCastExpr(CastExpr *E) {
  switch (E->getCastKind()) {
  default: 
    llvm_unreachable("Unhandled cast kind");
  case CK_NoOp:
  case CK_ConstructorConversion:
  case CK_UserDefinedConversion:
    Visit(E->getSubExpr());
    break;
  }
}

void AggExprVisitor::VisitCXXConstructExpr(CXXConstructExpr *E) {
  Eng.VisitCXXConstructExpr(E, Dest, Pred, DstSet);
}

void AggExprVisitor::VisitCXXMemberCallExpr(CXXMemberCallExpr *E) {
  Eng.Visit(E, Pred, DstSet);
}

void ExprEngine::VisitAggExpr(const Expr *E, const MemRegion *Dest, 
                                ExplodedNode *Pred, ExplodedNodeSet &Dst) {
  AggExprVisitor(Dest, Pred, Dst, *this).Visit(const_cast<Expr *>(E));
}
