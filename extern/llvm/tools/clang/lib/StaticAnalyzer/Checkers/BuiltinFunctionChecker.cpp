//=== BuiltinFunctionChecker.cpp --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This checker evaluates clang builtin functions.
//
//===----------------------------------------------------------------------===//

#include "ClangSACheckers.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/Basic/Builtins.h"

using namespace clang;
using namespace ento;

namespace {

class BuiltinFunctionChecker : public Checker<eval::Call> {
public:
  bool evalCall(const CallExpr *CE, CheckerContext &C) const;
};

}

bool BuiltinFunctionChecker::evalCall(const CallExpr *CE,
                                      CheckerContext &C) const{
  const ProgramState *state = C.getState();
  const Expr *Callee = CE->getCallee();
  SVal L = state->getSVal(Callee);
  const FunctionDecl *FD = L.getAsFunctionDecl();

  if (!FD)
    return false;

  unsigned id = FD->getBuiltinID();

  if (!id)
    return false;

  switch (id) {
  case Builtin::BI__builtin_expect: {
    // For __builtin_expect, just return the value of the subexpression.
    assert (CE->arg_begin() != CE->arg_end());
    SVal X = state->getSVal(*(CE->arg_begin()));
    C.generateNode(state->BindExpr(CE, X));
    return true;
  }

  case Builtin::BI__builtin_alloca: {
    // FIXME: Refactor into StoreManager itself?
    MemRegionManager& RM = C.getStoreManager().getRegionManager();
    const AllocaRegion* R =
      RM.getAllocaRegion(CE, C.getCurrentBlockCount(),
                         C.getPredecessor()->getLocationContext());

    // Set the extent of the region in bytes. This enables us to use the
    // SVal of the argument directly. If we save the extent in bits, we
    // cannot represent values like symbol*8.
    DefinedOrUnknownSVal Size =
      cast<DefinedOrUnknownSVal>(state->getSVal(*(CE->arg_begin())));

    SValBuilder& svalBuilder = C.getSValBuilder();
    DefinedOrUnknownSVal Extent = R->getExtent(svalBuilder);
    DefinedOrUnknownSVal extentMatchesSizeArg =
      svalBuilder.evalEQ(state, Extent, Size);
    state = state->assume(extentMatchesSizeArg, true);

    C.generateNode(state->BindExpr(CE, loc::MemRegionVal(R)));
    return true;
  }
  }

  return false;
}

void ento::registerBuiltinFunctionChecker(CheckerManager &mgr) {
  mgr.registerChecker<BuiltinFunctionChecker>();
}
