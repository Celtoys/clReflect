//===--- RewriteObjCFoundationAPI.cpp - Foundation API Rewriter -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Rewrites legacy method calls to modern syntax.
//
//===----------------------------------------------------------------------===//

#include "clang/Edit/Rewriters.h"
#include "clang/Edit/Commit.h"
#include "clang/Lex/Lexer.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/NSAPI.h"

using namespace clang;
using namespace edit;

static bool checkForLiteralCreation(const ObjCMessageExpr *Msg,
                                    IdentifierInfo *&ClassId) {
  if (!Msg || Msg->isImplicit() || !Msg->getMethodDecl())
    return false;

  const ObjCInterfaceDecl *Receiver = Msg->getReceiverInterface();
  if (!Receiver)
    return false;
  ClassId = Receiver->getIdentifier();

  if (Msg->getReceiverKind() == ObjCMessageExpr::Class)
    return true;

  return false;
}

//===----------------------------------------------------------------------===//
// rewriteObjCRedundantCallWithLiteral.
//===----------------------------------------------------------------------===//

bool edit::rewriteObjCRedundantCallWithLiteral(const ObjCMessageExpr *Msg,
                                              const NSAPI &NS, Commit &commit) {
  IdentifierInfo *II = 0;
  if (!checkForLiteralCreation(Msg, II))
    return false;
  if (Msg->getNumArgs() != 1)
    return false;

  const Expr *Arg = Msg->getArg(0)->IgnoreParenImpCasts();
  Selector Sel = Msg->getSelector();

  if ((isa<ObjCStringLiteral>(Arg) &&
       NS.getNSClassId(NSAPI::ClassId_NSString) == II &&
       NS.getNSStringSelector(NSAPI::NSStr_stringWithString) == Sel)    ||

      (isa<ObjCArrayLiteral>(Arg) &&
       NS.getNSClassId(NSAPI::ClassId_NSArray) == II &&
       NS.getNSArraySelector(NSAPI::NSArr_arrayWithArray) == Sel)      ||

      (isa<ObjCDictionaryLiteral>(Arg) &&
       NS.getNSClassId(NSAPI::ClassId_NSDictionary) == II &&
       NS.getNSDictionarySelector(
                              NSAPI::NSDict_dictionaryWithDictionary) == Sel)) {
    
    commit.replaceWithInner(Msg->getSourceRange(),
                           Msg->getArg(0)->getSourceRange());
    return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// rewriteToObjCSubscriptSyntax.
//===----------------------------------------------------------------------===//

static void maybePutParensOnReceiver(const Expr *Receiver, Commit &commit) {
  Receiver = Receiver->IgnoreImpCasts();
  if (isa<BinaryOperator>(Receiver) || isa<UnaryOperator>(Receiver)) {
    SourceRange RecRange = Receiver->getSourceRange();
    commit.insertWrap("(", RecRange, ")");
  }
}

static bool rewriteToSubscriptGet(const ObjCMessageExpr *Msg, Commit &commit) {
  if (Msg->getNumArgs() != 1)
    return false;
  const Expr *Rec = Msg->getInstanceReceiver();
  if (!Rec)
    return false;

  SourceRange MsgRange = Msg->getSourceRange();
  SourceRange RecRange = Rec->getSourceRange();
  SourceRange ArgRange = Msg->getArg(0)->getSourceRange();

  commit.replaceWithInner(CharSourceRange::getCharRange(MsgRange.getBegin(),
                                                       ArgRange.getBegin()),
                         CharSourceRange::getTokenRange(RecRange));
  commit.replaceWithInner(SourceRange(ArgRange.getBegin(), MsgRange.getEnd()),
                         ArgRange);
  commit.insertWrap("[", ArgRange, "]");
  maybePutParensOnReceiver(Rec, commit);
  return true;
}

static bool rewriteToArraySubscriptSet(const ObjCMessageExpr *Msg,
                                       Commit &commit) {
  if (Msg->getNumArgs() != 2)
    return false;
  const Expr *Rec = Msg->getInstanceReceiver();
  if (!Rec)
    return false;

  SourceRange MsgRange = Msg->getSourceRange();
  SourceRange RecRange = Rec->getSourceRange();
  SourceRange Arg0Range = Msg->getArg(0)->getSourceRange();
  SourceRange Arg1Range = Msg->getArg(1)->getSourceRange();

  commit.replaceWithInner(CharSourceRange::getCharRange(MsgRange.getBegin(),
                                                       Arg0Range.getBegin()),
                         CharSourceRange::getTokenRange(RecRange));
  commit.replaceWithInner(CharSourceRange::getCharRange(Arg0Range.getBegin(),
                                                       Arg1Range.getBegin()),
                         CharSourceRange::getTokenRange(Arg0Range));
  commit.replaceWithInner(SourceRange(Arg1Range.getBegin(), MsgRange.getEnd()),
                         Arg1Range);
  commit.insertWrap("[", CharSourceRange::getCharRange(Arg0Range.getBegin(),
                                                       Arg1Range.getBegin()),
                    "] = ");
  maybePutParensOnReceiver(Rec, commit);
  return true;
}

static bool rewriteToDictionarySubscriptSet(const ObjCMessageExpr *Msg,
                                            Commit &commit) {
  if (Msg->getNumArgs() != 2)
    return false;
  const Expr *Rec = Msg->getInstanceReceiver();
  if (!Rec)
    return false;

  SourceRange MsgRange = Msg->getSourceRange();
  SourceRange RecRange = Rec->getSourceRange();
  SourceRange Arg0Range = Msg->getArg(0)->getSourceRange();
  SourceRange Arg1Range = Msg->getArg(1)->getSourceRange();

  SourceLocation LocBeforeVal = Arg0Range.getBegin();
  commit.insertBefore(LocBeforeVal, "] = ");
  commit.insertFromRange(LocBeforeVal, Arg1Range, /*afterToken=*/false,
                         /*beforePreviousInsertions=*/true);
  commit.insertBefore(LocBeforeVal, "[");
  commit.replaceWithInner(CharSourceRange::getCharRange(MsgRange.getBegin(),
                                                       Arg0Range.getBegin()),
                         CharSourceRange::getTokenRange(RecRange));
  commit.replaceWithInner(SourceRange(Arg0Range.getBegin(), MsgRange.getEnd()),
                         Arg0Range);
  maybePutParensOnReceiver(Rec, commit);
  return true;
}

bool edit::rewriteToObjCSubscriptSyntax(const ObjCMessageExpr *Msg,
                                           const NSAPI &NS, Commit &commit) {
  if (!Msg || Msg->isImplicit() ||
      Msg->getReceiverKind() != ObjCMessageExpr::Instance)
    return false;
  const ObjCMethodDecl *Method = Msg->getMethodDecl();
  if (!Method)
    return false;

  const ObjCInterfaceDecl *
    IFace = NS.getASTContext().getObjContainingInterface(
                                          const_cast<ObjCMethodDecl *>(Method));
  if (!IFace)
    return false;
  IdentifierInfo *II = IFace->getIdentifier();
  Selector Sel = Msg->getSelector();

  if ((II == NS.getNSClassId(NSAPI::ClassId_NSArray) &&
       Sel == NS.getNSArraySelector(NSAPI::NSArr_objectAtIndex)) ||
      (II == NS.getNSClassId(NSAPI::ClassId_NSDictionary) &&
       Sel == NS.getNSDictionarySelector(NSAPI::NSDict_objectForKey)))
    return rewriteToSubscriptGet(Msg, commit);

  if (Msg->getNumArgs() != 2)
    return false;

  if (II == NS.getNSClassId(NSAPI::ClassId_NSMutableArray) &&
      Sel == NS.getNSArraySelector(NSAPI::NSMutableArr_replaceObjectAtIndex))
    return rewriteToArraySubscriptSet(Msg, commit);

  if (II == NS.getNSClassId(NSAPI::ClassId_NSMutableDictionary) &&
      Sel == NS.getNSDictionarySelector(NSAPI::NSMutableDict_setObjectForKey))
    return rewriteToDictionarySubscriptSet(Msg, commit);

  return false;
}

//===----------------------------------------------------------------------===//
// rewriteToObjCLiteralSyntax.
//===----------------------------------------------------------------------===//

static bool rewriteToArrayLiteral(const ObjCMessageExpr *Msg,
                                  const NSAPI &NS, Commit &commit);
static bool rewriteToDictionaryLiteral(const ObjCMessageExpr *Msg,
                                  const NSAPI &NS, Commit &commit);
static bool rewriteToNumberLiteral(const ObjCMessageExpr *Msg,
                                  const NSAPI &NS, Commit &commit);

bool edit::rewriteToObjCLiteralSyntax(const ObjCMessageExpr *Msg,
                                      const NSAPI &NS, Commit &commit) {
  IdentifierInfo *II = 0;
  if (!checkForLiteralCreation(Msg, II))
    return false;

  if (II == NS.getNSClassId(NSAPI::ClassId_NSArray))
    return rewriteToArrayLiteral(Msg, NS, commit);
  if (II == NS.getNSClassId(NSAPI::ClassId_NSDictionary))
    return rewriteToDictionaryLiteral(Msg, NS, commit);
  if (II == NS.getNSClassId(NSAPI::ClassId_NSNumber))
    return rewriteToNumberLiteral(Msg, NS, commit);

  return false;
}

//===----------------------------------------------------------------------===//
// rewriteToArrayLiteral.
//===----------------------------------------------------------------------===//

static bool rewriteToArrayLiteral(const ObjCMessageExpr *Msg,
                                  const NSAPI &NS, Commit &commit) {
  Selector Sel = Msg->getSelector();
  SourceRange MsgRange = Msg->getSourceRange();

  if (Sel == NS.getNSArraySelector(NSAPI::NSArr_array)) {
    if (Msg->getNumArgs() != 0)
      return false;
    commit.replace(MsgRange, "@[]");
    return true;
  }

  if (Sel == NS.getNSArraySelector(NSAPI::NSArr_arrayWithObject)) {
    if (Msg->getNumArgs() != 1)
      return false;
    SourceRange ArgRange = Msg->getArg(0)->getSourceRange();
    commit.replaceWithInner(MsgRange, ArgRange);
    commit.insertWrap("@[", ArgRange, "]");
    return true;
  }

  if (Sel == NS.getNSArraySelector(NSAPI::NSArr_arrayWithObjects)) {
    if (Msg->getNumArgs() == 0)
      return false;
    const Expr *SentinelExpr = Msg->getArg(Msg->getNumArgs() - 1);
    if (!NS.getASTContext().isSentinelNullExpr(SentinelExpr))
      return false;

    if (Msg->getNumArgs() == 1) {
      commit.replace(MsgRange, "@[]");
      return true;
    }
    SourceRange ArgRange(Msg->getArg(0)->getLocStart(),
                         Msg->getArg(Msg->getNumArgs()-2)->getLocEnd());
    commit.replaceWithInner(MsgRange, ArgRange);
    commit.insertWrap("@[", ArgRange, "]");
    return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// rewriteToDictionaryLiteral.
//===----------------------------------------------------------------------===//

static bool rewriteToDictionaryLiteral(const ObjCMessageExpr *Msg,
                                       const NSAPI &NS, Commit &commit) {
  Selector Sel = Msg->getSelector();
  SourceRange MsgRange = Msg->getSourceRange();

  if (Sel == NS.getNSDictionarySelector(NSAPI::NSDict_dictionary)) {
    if (Msg->getNumArgs() != 0)
      return false;
    commit.replace(MsgRange, "@{}");
    return true;
  }

  if (Sel == NS.getNSDictionarySelector(
                                    NSAPI::NSDict_dictionaryWithObjectForKey)) {
    if (Msg->getNumArgs() != 2)
      return false;
    SourceRange ValRange = Msg->getArg(0)->getSourceRange();
    SourceRange KeyRange = Msg->getArg(1)->getSourceRange();
    // Insert key before the value.
    commit.insertBefore(ValRange.getBegin(), ": ");
    commit.insertFromRange(ValRange.getBegin(),
                           CharSourceRange::getTokenRange(KeyRange),
                       /*afterToken=*/false, /*beforePreviousInsertions=*/true);
    commit.insertBefore(ValRange.getBegin(), "@{");
    commit.insertAfterToken(ValRange.getEnd(), "}");
    commit.replaceWithInner(MsgRange, ValRange);
    return true;
  }

  if (Sel == NS.getNSDictionarySelector(
                                  NSAPI::NSDict_dictionaryWithObjectsAndKeys)) {
    if (Msg->getNumArgs() % 2 != 1)
      return false;
    unsigned SentinelIdx = Msg->getNumArgs() - 1;
    const Expr *SentinelExpr = Msg->getArg(SentinelIdx);
    if (!NS.getASTContext().isSentinelNullExpr(SentinelExpr))
      return false;

    if (Msg->getNumArgs() == 1) {
      commit.replace(MsgRange, "@{}");
      return true;
    }

    for (unsigned i = 0; i < SentinelIdx; i += 2) {
      SourceRange ValRange = Msg->getArg(i)->getSourceRange();
      SourceRange KeyRange = Msg->getArg(i+1)->getSourceRange();
      // Insert value after key.
      commit.insertAfterToken(KeyRange.getEnd(), ": ");
      commit.insertFromRange(KeyRange.getEnd(), ValRange, /*afterToken=*/true);
      commit.remove(CharSourceRange::getCharRange(ValRange.getBegin(),
                                                  KeyRange.getBegin()));
    }
    // Range of arguments up until and including the last key.
    // The sentinel and first value are cut off, the value will move after the
    // key.
    SourceRange ArgRange(Msg->getArg(1)->getLocStart(),
                         Msg->getArg(SentinelIdx-1)->getLocEnd());
    commit.insertWrap("@{", ArgRange, "}");
    commit.replaceWithInner(MsgRange, ArgRange);
    return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// rewriteToNumberLiteral.
//===----------------------------------------------------------------------===//

static bool rewriteToCharLiteral(const ObjCMessageExpr *Msg,
                                   const CharacterLiteral *Arg,
                                   const NSAPI &NS, Commit &commit) {
  if (Arg->getKind() != CharacterLiteral::Ascii)
    return false;
  if (NS.isNSNumberLiteralSelector(NSAPI::NSNumberWithChar,
                                   Msg->getSelector())) {
    SourceRange ArgRange = Arg->getSourceRange();
    commit.replaceWithInner(Msg->getSourceRange(), ArgRange);
    commit.insert(ArgRange.getBegin(), "@");
    return true;
  }

  return false;
}

static bool rewriteToBoolLiteral(const ObjCMessageExpr *Msg,
                                   const Expr *Arg,
                                   const NSAPI &NS, Commit &commit) {
  if (NS.isNSNumberLiteralSelector(NSAPI::NSNumberWithBool,
                                   Msg->getSelector())) {
    SourceRange ArgRange = Arg->getSourceRange();
    commit.replaceWithInner(Msg->getSourceRange(), ArgRange);
    commit.insert(ArgRange.getBegin(), "@");
    return true;
  }

  return false;
}

namespace {

struct LiteralInfo {
  bool Hex, Octal;
  StringRef U, F, L, LL;
  CharSourceRange WithoutSuffRange;
};

}

static bool getLiteralInfo(SourceRange literalRange,
                           bool isFloat, bool isIntZero,
                          ASTContext &Ctx, LiteralInfo &Info) {
  if (literalRange.getBegin().isMacroID() ||
      literalRange.getEnd().isMacroID())
    return false;
  StringRef text = Lexer::getSourceText(
                                  CharSourceRange::getTokenRange(literalRange),
                                  Ctx.getSourceManager(), Ctx.getLangOpts());
  if (text.empty())
    return false;

  llvm::Optional<bool> UpperU, UpperL; 
  bool UpperF = false;

  struct Suff {
    static bool has(StringRef suff, StringRef &text) {
      if (text.endswith(suff)) {
        text = text.substr(0, text.size()-suff.size());
        return true;
      }
      return false;
    }
  };

  while (1) {
    if (Suff::has("u", text)) {
      UpperU = false;
    } else if (Suff::has("U", text)) {
      UpperU = true;
    } else if (Suff::has("ll", text)) {
      UpperL = false;
    } else if (Suff::has("LL", text)) {
      UpperL = true;
    } else if (Suff::has("l", text)) {
      UpperL = false;
    } else if (Suff::has("L", text)) {
      UpperL = true;
    } else if (isFloat && Suff::has("f", text)) {
      UpperF = false;
    } else if (isFloat && Suff::has("F", text)) {
      UpperF = true;
    } else
      break;
  }
  
  if (!UpperU.hasValue() && !UpperL.hasValue())
    UpperU = UpperL = true;
  else if (UpperU.hasValue() && !UpperL.hasValue())
    UpperL = UpperU;
  else if (UpperL.hasValue() && !UpperU.hasValue())
    UpperU = UpperL;

  Info.U = *UpperU ? "U" : "u";
  Info.L = *UpperL ? "L" : "l";
  Info.LL = *UpperL ? "LL" : "ll";
  Info.F = UpperF ? "F" : "f";
  
  Info.Hex = Info.Octal = false;
  if (text.startswith("0x"))
    Info.Hex = true;
  else if (!isFloat && !isIntZero && text.startswith("0"))
    Info.Octal = true;

  SourceLocation B = literalRange.getBegin();
  Info.WithoutSuffRange =
      CharSourceRange::getCharRange(B, B.getLocWithOffset(text.size()));
  return true;
}

static bool rewriteToNumberLiteral(const ObjCMessageExpr *Msg,
                                   const NSAPI &NS, Commit &commit) {
  if (Msg->getNumArgs() != 1)
    return false;

  const Expr *Arg = Msg->getArg(0)->IgnoreParenImpCasts();
  if (const CharacterLiteral *CharE = dyn_cast<CharacterLiteral>(Arg))
    return rewriteToCharLiteral(Msg, CharE, NS, commit);
  if (const ObjCBoolLiteralExpr *BE = dyn_cast<ObjCBoolLiteralExpr>(Arg))
    return rewriteToBoolLiteral(Msg, BE, NS, commit);
  if (const CXXBoolLiteralExpr *BE = dyn_cast<CXXBoolLiteralExpr>(Arg))
    return rewriteToBoolLiteral(Msg, BE, NS, commit);

  const Expr *literalE = Arg;
  if (const UnaryOperator *UOE = dyn_cast<UnaryOperator>(literalE)) {
    if (UOE->getOpcode() == UO_Plus || UOE->getOpcode() == UO_Minus)
      literalE = UOE->getSubExpr();
  }

  // Only integer and floating literals; non-literals or imaginary literal
  // cannot be rewritten.
  if (!isa<IntegerLiteral>(literalE) && !isa<FloatingLiteral>(literalE))
    return false;

  ASTContext &Ctx = NS.getASTContext();
  Selector Sel = Msg->getSelector();
  llvm::Optional<NSAPI::NSNumberLiteralMethodKind>
    MKOpt = NS.getNSNumberLiteralMethodKind(Sel);
  if (!MKOpt)
    return false;
  NSAPI::NSNumberLiteralMethodKind MK = *MKOpt;

  bool CallIsUnsigned = false, CallIsLong = false, CallIsLongLong = false;
  bool CallIsFloating = false, CallIsDouble = false;

  switch (MK) {
  // We cannot have these calls with int/float literals.
  case NSAPI::NSNumberWithChar:
  case NSAPI::NSNumberWithUnsignedChar:
  case NSAPI::NSNumberWithShort:
  case NSAPI::NSNumberWithUnsignedShort:
  case NSAPI::NSNumberWithBool:
    return false;

  case NSAPI::NSNumberWithUnsignedInt:
  case NSAPI::NSNumberWithUnsignedInteger:
    CallIsUnsigned = true;
  case NSAPI::NSNumberWithInt:
  case NSAPI::NSNumberWithInteger:
    break;

  case NSAPI::NSNumberWithUnsignedLong:
    CallIsUnsigned = true;
  case NSAPI::NSNumberWithLong:
    CallIsLong = true;
    break;

  case NSAPI::NSNumberWithUnsignedLongLong:
    CallIsUnsigned = true;
  case NSAPI::NSNumberWithLongLong:
    CallIsLongLong = true;
    break;

  case NSAPI::NSNumberWithDouble:
    CallIsDouble = true;
  case NSAPI::NSNumberWithFloat:
    CallIsFloating = true;
    break;
  }

  SourceRange ArgRange = Arg->getSourceRange();
  QualType ArgTy = Arg->getType();
  QualType CallTy = Msg->getArg(0)->getType();

  // Check for the easy case, the literal maps directly to the call.
  if (Ctx.hasSameType(ArgTy, CallTy)) {
    commit.replaceWithInner(Msg->getSourceRange(), ArgRange);
    commit.insert(ArgRange.getBegin(), "@");
    return true;
  }

  // We will need to modify the literal suffix to get the same type as the call.
  // Don't even try if it came from a macro.
  if (ArgRange.getBegin().isMacroID())
    return false;

  bool LitIsFloat = ArgTy->isFloatingType();
  // For a float passed to integer call, don't try rewriting. It is difficult
  // and a very uncommon case anyway.
  if (LitIsFloat && !CallIsFloating)
    return false;

  // Try to modify the literal make it the same type as the method call.
  // -Modify the suffix, and/or
  // -Change integer to float
  
  LiteralInfo LitInfo;
  bool isIntZero = false;
  if (const IntegerLiteral *IntE = dyn_cast<IntegerLiteral>(literalE))
    isIntZero = !IntE->getValue().getBoolValue();
  if (!getLiteralInfo(ArgRange, LitIsFloat, isIntZero, Ctx, LitInfo))
    return false;

  // Not easy to do int -> float with hex/octal and uncommon anyway.
  if (!LitIsFloat && CallIsFloating && (LitInfo.Hex || LitInfo.Octal))
    return false;
  
  SourceLocation LitB = LitInfo.WithoutSuffRange.getBegin();
  SourceLocation LitE = LitInfo.WithoutSuffRange.getEnd();

  commit.replaceWithInner(CharSourceRange::getTokenRange(Msg->getSourceRange()),
                         LitInfo.WithoutSuffRange);
  commit.insert(LitB, "@");

  if (!LitIsFloat && CallIsFloating)
    commit.insert(LitE, ".0");

  if (CallIsFloating) {
    if (!CallIsDouble)
      commit.insert(LitE, LitInfo.F);
  } else {
    if (CallIsUnsigned)
      commit.insert(LitE, LitInfo.U);
  
    if (CallIsLong)
      commit.insert(LitE, LitInfo.L);
    else if (CallIsLongLong)
      commit.insert(LitE, LitInfo.LL);
  }
  return true;
}
