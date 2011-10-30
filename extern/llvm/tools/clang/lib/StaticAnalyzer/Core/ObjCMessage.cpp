//===- ObjCMessage.cpp - Wrapper for ObjC messages and dot syntax -*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines ObjCMessage which serves as a common wrapper for ObjC
// message expressions or implicit messages for loading/storing ObjC properties.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/ObjCMessage.h"

using namespace clang;
using namespace ento;

QualType ObjCMessage::getType(ASTContext &ctx) const {
  assert(isValid() && "This ObjCMessage is uninitialized!");
  if (const ObjCMessageExpr *msgE = dyn_cast<ObjCMessageExpr>(MsgOrPropE))
    return msgE->getType();
  const ObjCPropertyRefExpr *propE = cast<ObjCPropertyRefExpr>(MsgOrPropE);
  if (isPropertySetter())
    return ctx.VoidTy;
  return propE->getType();
}

Selector ObjCMessage::getSelector() const {
  assert(isValid() && "This ObjCMessage is uninitialized!");
  if (const ObjCMessageExpr *msgE = dyn_cast<ObjCMessageExpr>(MsgOrPropE))
    return msgE->getSelector();
  const ObjCPropertyRefExpr *propE = cast<ObjCPropertyRefExpr>(MsgOrPropE);
  if (isPropertySetter())
    return propE->getSetterSelector();
  return propE->getGetterSelector();
}

ObjCMethodFamily ObjCMessage::getMethodFamily() const {
  assert(isValid() && "This ObjCMessage is uninitialized!");
  // Case 1.  Explicit message send.
  if (const ObjCMessageExpr *msgE = dyn_cast<ObjCMessageExpr>(MsgOrPropE))
    return msgE->getMethodFamily();

  const ObjCPropertyRefExpr *propE = cast<ObjCPropertyRefExpr>(MsgOrPropE);

  // Case 2.  Reference to implicit property.
  if (propE->isImplicitProperty()) {
    if (isPropertySetter())
      return propE->getImplicitPropertySetter()->getMethodFamily();
    else
      return propE->getImplicitPropertyGetter()->getMethodFamily();
  }

  // Case 3.  Reference to explicit property.
  const ObjCPropertyDecl *prop = propE->getExplicitProperty();
  if (isPropertySetter()) {
    if (prop->getSetterMethodDecl())
      return prop->getSetterMethodDecl()->getMethodFamily();
    return prop->getSetterName().getMethodFamily();
  } else {
    if (prop->getGetterMethodDecl())
      return prop->getGetterMethodDecl()->getMethodFamily();
    return prop->getGetterName().getMethodFamily();
  }
}

const ObjCMethodDecl *ObjCMessage::getMethodDecl() const {
  assert(isValid() && "This ObjCMessage is uninitialized!");
  if (const ObjCMessageExpr *msgE = dyn_cast<ObjCMessageExpr>(MsgOrPropE))
    return msgE->getMethodDecl();
  const ObjCPropertyRefExpr *propE = cast<ObjCPropertyRefExpr>(MsgOrPropE);
  if (propE->isImplicitProperty())
    return isPropertySetter() ? propE->getImplicitPropertySetter()
                              : propE->getImplicitPropertyGetter();
  return 0;
}

const ObjCInterfaceDecl *ObjCMessage::getReceiverInterface() const {
  assert(isValid() && "This ObjCMessage is uninitialized!");
  if (const ObjCMessageExpr *msgE = dyn_cast<ObjCMessageExpr>(MsgOrPropE))
    return msgE->getReceiverInterface();
  const ObjCPropertyRefExpr *propE = cast<ObjCPropertyRefExpr>(MsgOrPropE);
  if (propE->isClassReceiver())
    return propE->getClassReceiver();
  QualType recT;
  if (const Expr *recE = getInstanceReceiver())
    recT = recE->getType();
  else {
    assert(propE->isSuperReceiver());
    recT = propE->getSuperReceiverType();
  }
  if (const ObjCObjectPointerType *Ptr = recT->getAs<ObjCObjectPointerType>())
    return Ptr->getInterfaceDecl();
  return 0;
}

const Expr *ObjCMessage::getArgExpr(unsigned i) const {
  assert(isValid() && "This ObjCMessage is uninitialized!");
  assert(i < getNumArgs() && "Invalid index for argument");
  if (const ObjCMessageExpr *msgE = dyn_cast<ObjCMessageExpr>(MsgOrPropE))
    return msgE->getArg(i);
  assert(isPropertySetter());
  if (const BinaryOperator *bop = dyn_cast<BinaryOperator>(OriginE))
    if (bop->isAssignmentOp())
      return bop->getRHS();
  return 0;
}

QualType CallOrObjCMessage::getResultType(ASTContext &ctx) const {
  QualType resultTy;
  bool isLVal = false;

  if (isObjCMessage()) {
    isLVal = isa<ObjCMessageExpr>(Msg.getOriginExpr()) &&
             Msg.getOriginExpr()->isLValue();
    resultTy = Msg.getResultType(ctx);
  } else if (const CXXConstructExpr *Ctor =
              CallE.dyn_cast<const CXXConstructExpr *>()) {
    resultTy = Ctor->getType();
  } else {
    const CallExpr *FunctionCall = CallE.get<const CallExpr *>();

    isLVal = FunctionCall->isLValue();
    const Expr *Callee = FunctionCall->getCallee();
    if (const FunctionDecl *FD = State->getSVal(Callee).getAsFunctionDecl())
      resultTy = FD->getResultType();
    else
      resultTy = FunctionCall->getType();
  }

  if (isLVal)
    resultTy = ctx.getPointerType(resultTy);

  return resultTy;
}

SVal CallOrObjCMessage::getFunctionCallee() const {
  assert(isFunctionCall());
  assert(!isCXXCall());
  const Expr *Fun = CallE.get<const CallExpr *>()->getCallee()->IgnoreParens();
  return State->getSVal(Fun);
}

SVal CallOrObjCMessage::getCXXCallee() const {
  assert(isCXXCall());
  const CallExpr *ActualCall = CallE.get<const CallExpr *>();
  const Expr *callee =
    cast<CXXMemberCallExpr>(ActualCall)->getImplicitObjectArgument();
  
  // FIXME: Will eventually need to cope with member pointers.  This is
  // a limitation in getImplicitObjectArgument().
  if (!callee)
    return UnknownVal();
  
  return State->getSVal(callee);
}

SVal
CallOrObjCMessage::getInstanceMessageReceiver(const LocationContext *LC) const {
  assert(isObjCMessage());
  return Msg.getInstanceReceiverSVal(State, LC);
}
