//=== OSAtomicChecker.cpp - OSAtomic functions evaluator --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This checker evaluates OSAtomic functions.
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

class OSAtomicChecker : public Checker<eval::InlineCall> {
public:
  bool inlineCall(const CallExpr *CE, ExprEngine &Eng,
                  ExplodedNode *Pred, ExplodedNodeSet &Dst) const;

private:
  bool evalOSAtomicCompareAndSwap(const CallExpr *CE,
                                  ExprEngine &Eng,
                                  ExplodedNode *Pred,
                                  ExplodedNodeSet &Dst) const;

  ExplodedNode *generateNode(const ProgramState *State,
                             ExplodedNode *Pred, const CallExpr *Statement,
                             StmtNodeBuilder &B, ExplodedNodeSet &Dst) const;
};
}

bool OSAtomicChecker::inlineCall(const CallExpr *CE,
                                 ExprEngine &Eng,
                                 ExplodedNode *Pred,
                                 ExplodedNodeSet &Dst) const {
  const ProgramState *state = Pred->getState();
  const Expr *Callee = CE->getCallee();
  SVal L = state->getSVal(Callee);

  const FunctionDecl *FD = L.getAsFunctionDecl();
  if (!FD)
    return false;

  const IdentifierInfo *II = FD->getIdentifier();
  if (!II)
    return false;
  
  StringRef FName(II->getName());

  // Check for compare and swap.
  if (FName.startswith("OSAtomicCompareAndSwap") ||
      FName.startswith("objc_atomicCompareAndSwap"))
    return evalOSAtomicCompareAndSwap(CE, Eng, Pred, Dst);

  // FIXME: Other atomics.
  return false;
}

ExplodedNode *OSAtomicChecker::generateNode(const ProgramState *State,
                                            ExplodedNode *Pred,
                                            const CallExpr *Statement,
                                            StmtNodeBuilder &B,
                                            ExplodedNodeSet &Dst) const {
  ExplodedNode *N = B.generateNode(Statement, State, Pred, this);
  if (N)
    Dst.Add(N);
  return N;
}

bool OSAtomicChecker::evalOSAtomicCompareAndSwap(const CallExpr *CE,
                                                 ExprEngine &Eng,
                                                 ExplodedNode *Pred,
                                                 ExplodedNodeSet &Dst) const {
  // Not enough arguments to match OSAtomicCompareAndSwap?
  if (CE->getNumArgs() != 3)
    return false;

  StmtNodeBuilder &Builder = Eng.getBuilder();
  ASTContext &Ctx = Eng.getContext();
  const Expr *oldValueExpr = CE->getArg(0);
  QualType oldValueType = Ctx.getCanonicalType(oldValueExpr->getType());

  const Expr *newValueExpr = CE->getArg(1);
  QualType newValueType = Ctx.getCanonicalType(newValueExpr->getType());

  // Do the types of 'oldValue' and 'newValue' match?
  if (oldValueType != newValueType)
    return false;

  const Expr *theValueExpr = CE->getArg(2);
  const PointerType *theValueType=theValueExpr->getType()->getAs<PointerType>();

  // theValueType not a pointer?
  if (!theValueType)
    return false;

  QualType theValueTypePointee =
    Ctx.getCanonicalType(theValueType->getPointeeType()).getUnqualifiedType();

  // The pointee must match newValueType and oldValueType.
  if (theValueTypePointee != newValueType)
    return false;

  static SimpleProgramPointTag OSAtomicLoadTag("OSAtomicChecker : Load");
  static SimpleProgramPointTag OSAtomicStoreTag("OSAtomicChecker : Store");
  
  // Load 'theValue'.
  const ProgramState *state = Pred->getState();
  ExplodedNodeSet Tmp;
  SVal location = state->getSVal(theValueExpr);
  // Here we should use the value type of the region as the load type, because
  // we are simulating the semantics of the function, not the semantics of 
  // passing argument. So the type of theValue expr is not we are loading.
  // But usually the type of the varregion is not the type we want either,
  // we still need to do a CastRetrievedVal in store manager. So actually this
  // LoadTy specifying can be omitted. But we put it here to emphasize the 
  // semantics.
  QualType LoadTy;
  if (const TypedValueRegion *TR =
      dyn_cast_or_null<TypedValueRegion>(location.getAsRegion())) {
    LoadTy = TR->getValueType();
  }
  Eng.evalLoad(Tmp, theValueExpr, Pred,
                  state, location, &OSAtomicLoadTag, LoadTy);

  if (Tmp.empty()) {
    // If no nodes were generated, other checkers must generated sinks. But 
    // since the builder state was restored, we set it manually to prevent 
    // auto transition.
    // FIXME: there should be a better approach.
    Builder.BuildSinks = true;
    return true;
  }
 
  for (ExplodedNodeSet::iterator I = Tmp.begin(), E = Tmp.end();
       I != E; ++I) {

    ExplodedNode *N = *I;
    const ProgramState *stateLoad = N->getState();

    // Use direct bindings from the environment since we are forcing a load
    // from a location that the Environment would typically not be used
    // to bind a value.
    SVal theValueVal_untested = stateLoad->getSVal(theValueExpr, true);

    SVal oldValueVal_untested = stateLoad->getSVal(oldValueExpr);

    // FIXME: Issue an error.
    if (theValueVal_untested.isUndef() || oldValueVal_untested.isUndef()) {
      return false;
    }
    
    DefinedOrUnknownSVal theValueVal =
      cast<DefinedOrUnknownSVal>(theValueVal_untested);
    DefinedOrUnknownSVal oldValueVal =
      cast<DefinedOrUnknownSVal>(oldValueVal_untested);

    SValBuilder &svalBuilder = Eng.getSValBuilder();

    // Perform the comparison.
    DefinedOrUnknownSVal Cmp =
      svalBuilder.evalEQ(stateLoad,theValueVal,oldValueVal);

    const ProgramState *stateEqual = stateLoad->assume(Cmp, true);

    // Were they equal?
    if (stateEqual) {
      // Perform the store.
      ExplodedNodeSet TmpStore;
      SVal val = stateEqual->getSVal(newValueExpr);

      // Handle implicit value casts.
      if (const TypedValueRegion *R =
          dyn_cast_or_null<TypedValueRegion>(location.getAsRegion())) {
        val = svalBuilder.evalCast(val,R->getValueType(), newValueExpr->getType());
      }

      Eng.evalStore(TmpStore, NULL, theValueExpr, N,
                       stateEqual, location, val, &OSAtomicStoreTag);

      if (TmpStore.empty()) {
        // If no nodes were generated, other checkers must generated sinks. But 
        // since the builder state was restored, we set it manually to prevent 
        // auto transition.
        // FIXME: there should be a better approach.
        Builder.BuildSinks = true;
        return true;
      }

      // Now bind the result of the comparison.
      for (ExplodedNodeSet::iterator I2 = TmpStore.begin(),
           E2 = TmpStore.end(); I2 != E2; ++I2) {
        ExplodedNode *predNew = *I2;
        const ProgramState *stateNew = predNew->getState();
        // Check for 'void' return type if we have a bogus function prototype.
        SVal Res = UnknownVal();
        QualType T = CE->getType();
        if (!T->isVoidType())
          Res = Eng.getSValBuilder().makeTruthVal(true, T);
        generateNode(stateNew->BindExpr(CE, Res), predNew, CE, Builder, Dst);
      }
    }

    // Were they not equal?
    if (const ProgramState *stateNotEqual = stateLoad->assume(Cmp, false)) {
      // Check for 'void' return type if we have a bogus function prototype.
      SVal Res = UnknownVal();
      QualType T = CE->getType();
      if (!T->isVoidType())
        Res = Eng.getSValBuilder().makeTruthVal(false, CE->getType());
      generateNode(stateNotEqual->BindExpr(CE, Res), N, CE, Builder, Dst);
    }
  }

  return true;
}

void ento::registerOSAtomicChecker(CheckerManager &mgr) {
  mgr.registerChecker<OSAtomicChecker>();
}
