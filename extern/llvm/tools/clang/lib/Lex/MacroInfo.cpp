//===--- MacroInfo.cpp - Information about #defined identifiers -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the MacroInfo interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
using namespace clang;

MacroInfo::MacroInfo(SourceLocation DefLoc) : Location(DefLoc) {
  IsFunctionLike = false;
  IsC99Varargs = false;
  IsGNUVarargs = false;
  IsBuiltinMacro = false;
  IsFromAST = false;
  ChangedAfterLoad = false;
  IsDisabled = false;
  IsUsed = false;
  IsAllowRedefinitionsWithoutWarning = false;
  IsWarnIfUnused = false;
  IsDefinitionLengthCached = false;

  ArgumentList = 0;
  NumArguments = 0;
}

MacroInfo::MacroInfo(const MacroInfo &MI, llvm::BumpPtrAllocator &PPAllocator) {
  Location = MI.Location;
  EndLocation = MI.EndLocation;
  ReplacementTokens = MI.ReplacementTokens;
  IsFunctionLike = MI.IsFunctionLike;
  IsC99Varargs = MI.IsC99Varargs;
  IsGNUVarargs = MI.IsGNUVarargs;
  IsBuiltinMacro = MI.IsBuiltinMacro;
  IsFromAST = MI.IsFromAST;
  ChangedAfterLoad = MI.ChangedAfterLoad;
  IsDisabled = MI.IsDisabled;
  IsUsed = MI.IsUsed;
  IsAllowRedefinitionsWithoutWarning = MI.IsAllowRedefinitionsWithoutWarning;
  IsWarnIfUnused = MI.IsWarnIfUnused;
  IsDefinitionLengthCached = MI.IsDefinitionLengthCached;
  DefinitionLength = MI.DefinitionLength;
  ArgumentList = 0;
  NumArguments = 0;
  setArgumentList(MI.ArgumentList, MI.NumArguments, PPAllocator);
}

unsigned MacroInfo::getDefinitionLengthSlow(SourceManager &SM) const {
  assert(!IsDefinitionLengthCached);
  IsDefinitionLengthCached = true;

  if (ReplacementTokens.empty())
    return (DefinitionLength = 0);

  const Token &firstToken = ReplacementTokens.front();
  const Token &lastToken = ReplacementTokens.back();
  SourceLocation macroStart = firstToken.getLocation();
  SourceLocation macroEnd = lastToken.getLocation();
  assert(macroStart.isValid() && macroEnd.isValid());
  assert((macroStart.isFileID() || firstToken.is(tok::comment)) &&
         "Macro defined in macro?");
  assert((macroEnd.isFileID() || lastToken.is(tok::comment)) &&
         "Macro defined in macro?");
  std::pair<FileID, unsigned>
      startInfo = SM.getDecomposedExpansionLoc(macroStart);
  std::pair<FileID, unsigned>
      endInfo = SM.getDecomposedExpansionLoc(macroEnd);
  assert(startInfo.first == endInfo.first &&
         "Macro definition spanning multiple FileIDs ?");
  assert(startInfo.second <= endInfo.second);
  DefinitionLength = endInfo.second - startInfo.second;
  DefinitionLength += lastToken.getLength();

  return DefinitionLength;
}

/// isIdenticalTo - Return true if the specified macro definition is equal to
/// this macro in spelling, arguments, and whitespace.  This is used to emit
/// duplicate definition warnings.  This implements the rules in C99 6.10.3.
///
bool MacroInfo::isIdenticalTo(const MacroInfo &Other, Preprocessor &PP) const {
  // Check # tokens in replacement, number of args, and various flags all match.
  if (ReplacementTokens.size() != Other.ReplacementTokens.size() ||
      getNumArgs() != Other.getNumArgs() ||
      isFunctionLike() != Other.isFunctionLike() ||
      isC99Varargs() != Other.isC99Varargs() ||
      isGNUVarargs() != Other.isGNUVarargs())
    return false;

  // Check arguments.
  for (arg_iterator I = arg_begin(), OI = Other.arg_begin(), E = arg_end();
       I != E; ++I, ++OI)
    if (*I != *OI) return false;

  // Check all the tokens.
  for (unsigned i = 0, e = ReplacementTokens.size(); i != e; ++i) {
    const Token &A = ReplacementTokens[i];
    const Token &B = Other.ReplacementTokens[i];
    if (A.getKind() != B.getKind())
      return false;

    // If this isn't the first first token, check that the whitespace and
    // start-of-line characteristics match.
    if (i != 0 &&
        (A.isAtStartOfLine() != B.isAtStartOfLine() ||
         A.hasLeadingSpace() != B.hasLeadingSpace()))
      return false;

    // If this is an identifier, it is easy.
    if (A.getIdentifierInfo() || B.getIdentifierInfo()) {
      if (A.getIdentifierInfo() != B.getIdentifierInfo())
        return false;
      continue;
    }

    // Otherwise, check the spelling.
    if (PP.getSpelling(A) != PP.getSpelling(B))
      return false;
  }

  return true;
}
