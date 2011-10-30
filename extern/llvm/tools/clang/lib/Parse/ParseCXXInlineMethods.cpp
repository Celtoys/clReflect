//===--- ParseCXXInlineMethods.cpp - C++ class inline methods parsing------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements parsing for C++ class inline methods.
//
//===----------------------------------------------------------------------===//

#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Scope.h"
#include "clang/AST/DeclTemplate.h"
using namespace clang;

/// ParseCXXInlineMethodDef - We parsed and verified that the specified
/// Declarator is a well formed C++ inline method definition. Now lex its body
/// and store its tokens for parsing after the C++ class is complete.
Decl *Parser::ParseCXXInlineMethodDef(AccessSpecifier AS,
                                      AttributeList *AccessAttrs,
                                      ParsingDeclarator &D,
                                const ParsedTemplateInfo &TemplateInfo,
                                const VirtSpecifiers& VS, ExprResult& Init) {
  assert(D.isFunctionDeclarator() && "This isn't a function declarator!");
  assert((Tok.is(tok::l_brace) || Tok.is(tok::colon) || Tok.is(tok::kw_try) ||
          Tok.is(tok::equal)) &&
         "Current token not a '{', ':', '=', or 'try'!");

  MultiTemplateParamsArg TemplateParams(Actions,
          TemplateInfo.TemplateParams ? TemplateInfo.TemplateParams->data() : 0,
          TemplateInfo.TemplateParams ? TemplateInfo.TemplateParams->size() : 0);

  Decl *FnD;
  D.setFunctionDefinition(true);
  if (D.getDeclSpec().isFriendSpecified())
    FnD = Actions.ActOnFriendFunctionDecl(getCurScope(), D,
                                          move(TemplateParams));
  else {
    FnD = Actions.ActOnCXXMemberDeclarator(getCurScope(), AS, D,
                                           move(TemplateParams), 0, 
                                           VS, /*HasInit=*/false);
    if (FnD) {
      Actions.ProcessDeclAttributeList(getCurScope(), FnD, AccessAttrs,
                                       false, true);
      bool TypeSpecContainsAuto
        = D.getDeclSpec().getTypeSpecType() == DeclSpec::TST_auto;
      if (Init.get())
        Actions.AddInitializerToDecl(FnD, Init.get(), false, 
                                     TypeSpecContainsAuto);
      else
        Actions.ActOnUninitializedDecl(FnD, TypeSpecContainsAuto);
    }
  }

  HandleMemberFunctionDefaultArgs(D, FnD);

  D.complete(FnD);

  if (Tok.is(tok::equal)) {
    ConsumeToken();

    bool Delete = false;
    SourceLocation KWLoc;
    if (Tok.is(tok::kw_delete)) {
      if (!getLang().CPlusPlus0x)
        Diag(Tok, diag::warn_deleted_function_accepted_as_extension);

      KWLoc = ConsumeToken();
      Actions.SetDeclDeleted(FnD, KWLoc);
      Delete = true;
    } else if (Tok.is(tok::kw_default)) {
      if (!getLang().CPlusPlus0x)
        Diag(Tok, diag::warn_defaulted_function_accepted_as_extension);

      KWLoc = ConsumeToken();
      Actions.SetDeclDefaulted(FnD, KWLoc);
    } else {
      llvm_unreachable("function definition after = not 'delete' or 'default'");
    }

    if (Tok.is(tok::comma)) {
      Diag(KWLoc, diag::err_default_delete_in_multiple_declaration)
        << Delete;
      SkipUntil(tok::semi);
    } else {
      ExpectAndConsume(tok::semi, diag::err_expected_semi_after,
                       Delete ? "delete" : "default", tok::semi);
    }

    return FnD;
  }

  // In delayed template parsing mode, if we are within a class template
  // or if we are about to parse function member template then consume
  // the tokens and store them for parsing at the end of the translation unit.
  if (getLang().DelayedTemplateParsing && 
      ((Actions.CurContext->isDependentContext() ||
        TemplateInfo.Kind != ParsedTemplateInfo::NonTemplate) && 
        !Actions.IsInsideALocalClassWithinATemplateFunction()) &&
        !D.getDeclSpec().isFriendSpecified()) {

    if (FnD) {
      LateParsedTemplatedFunction *LPT =
        new LateParsedTemplatedFunction(this, FnD);

      FunctionDecl *FD = 0;
      if (FunctionTemplateDecl *FunTmpl = dyn_cast<FunctionTemplateDecl>(FnD))
        FD = FunTmpl->getTemplatedDecl();
      else
        FD = cast<FunctionDecl>(FnD);
      Actions.CheckForFunctionRedefinition(FD);

      LateParsedTemplateMap[FD] = LPT;
      Actions.MarkAsLateParsedTemplate(FD);
      LexTemplateFunctionForLateParsing(LPT->Toks);
    } else {
      CachedTokens Toks;
      LexTemplateFunctionForLateParsing(Toks);
    }

    return FnD;
  }

  // Consume the tokens and store them for later parsing.

  LexedMethod* LM = new LexedMethod(this, FnD);
  getCurrentClass().LateParsedDeclarations.push_back(LM);
  LM->TemplateScope = getCurScope()->isTemplateParamScope();
  CachedTokens &Toks = LM->Toks;

  tok::TokenKind kind = Tok.getKind();
  // Consume everything up to (and including) the left brace of the
  // function body.
  if (ConsumeAndStoreFunctionPrologue(Toks)) {
    // We didn't find the left-brace we expected after the
    // constructor initializer.
    if (Tok.is(tok::semi)) {
      // We found a semicolon; complain, consume the semicolon, and
      // don't try to parse this method later.
      Diag(Tok.getLocation(), diag::err_expected_lbrace);
      ConsumeAnyToken();
      delete getCurrentClass().LateParsedDeclarations.back();
      getCurrentClass().LateParsedDeclarations.pop_back();
      return FnD;
    }
  } else {
    // Consume everything up to (and including) the matching right brace.
    ConsumeAndStoreUntil(tok::r_brace, Toks, /*StopAtSemi=*/false);
  }

  // If we're in a function-try-block, we need to store all the catch blocks.
  if (kind == tok::kw_try) {
    while (Tok.is(tok::kw_catch)) {
      ConsumeAndStoreUntil(tok::l_brace, Toks, /*StopAtSemi=*/false);
      ConsumeAndStoreUntil(tok::r_brace, Toks, /*StopAtSemi=*/false);
    }
  }


  if (!FnD) {
    // If semantic analysis could not build a function declaration,
    // just throw away the late-parsed declaration.
    delete getCurrentClass().LateParsedDeclarations.back();
    getCurrentClass().LateParsedDeclarations.pop_back();
  }

  return FnD;
}

/// ParseCXXNonStaticMemberInitializer - We parsed and verified that the
/// specified Declarator is a well formed C++ non-static data member
/// declaration. Now lex its initializer and store its tokens for parsing
/// after the class is complete.
void Parser::ParseCXXNonStaticMemberInitializer(Decl *VarD) {
  assert((Tok.is(tok::l_brace) || Tok.is(tok::equal)) &&
         "Current token not a '{' or '='!");

  LateParsedMemberInitializer *MI =
    new LateParsedMemberInitializer(this, VarD);
  getCurrentClass().LateParsedDeclarations.push_back(MI);
  CachedTokens &Toks = MI->Toks;

  tok::TokenKind kind = Tok.getKind();
  if (kind == tok::equal) {
    Toks.push_back(Tok);
    ConsumeAnyToken();
  }

  if (kind == tok::l_brace) {
    // Begin by storing the '{' token.
    Toks.push_back(Tok);
    ConsumeBrace();

    // Consume everything up to (and including) the matching right brace.
    ConsumeAndStoreUntil(tok::r_brace, Toks, /*StopAtSemi=*/true);
  } else {
    // Consume everything up to (but excluding) the comma or semicolon.
    ConsumeAndStoreUntil(tok::comma, Toks, /*StopAtSemi=*/true,
                         /*ConsumeFinalToken=*/false);
  }

  // Store an artificial EOF token to ensure that we don't run off the end of
  // the initializer when we come to parse it.
  Token Eof;
  Eof.startToken();
  Eof.setKind(tok::eof);
  Eof.setLocation(Tok.getLocation());
  Toks.push_back(Eof);
}

Parser::LateParsedDeclaration::~LateParsedDeclaration() {}
void Parser::LateParsedDeclaration::ParseLexedMethodDeclarations() {}
void Parser::LateParsedDeclaration::ParseLexedMemberInitializers() {}
void Parser::LateParsedDeclaration::ParseLexedMethodDefs() {}

Parser::LateParsedClass::LateParsedClass(Parser *P, ParsingClass *C)
  : Self(P), Class(C) {}

Parser::LateParsedClass::~LateParsedClass() {
  Self->DeallocateParsedClasses(Class);
}

void Parser::LateParsedClass::ParseLexedMethodDeclarations() {
  Self->ParseLexedMethodDeclarations(*Class);
}

void Parser::LateParsedClass::ParseLexedMemberInitializers() {
  Self->ParseLexedMemberInitializers(*Class);
}

void Parser::LateParsedClass::ParseLexedMethodDefs() {
  Self->ParseLexedMethodDefs(*Class);
}

void Parser::LateParsedMethodDeclaration::ParseLexedMethodDeclarations() {
  Self->ParseLexedMethodDeclaration(*this);
}

void Parser::LexedMethod::ParseLexedMethodDefs() {
  Self->ParseLexedMethodDef(*this);
}

void Parser::LateParsedMemberInitializer::ParseLexedMemberInitializers() {
  Self->ParseLexedMemberInitializer(*this);
}

/// ParseLexedMethodDeclarations - We finished parsing the member
/// specification of a top (non-nested) C++ class. Now go over the
/// stack of method declarations with some parts for which parsing was
/// delayed (such as default arguments) and parse them.
void Parser::ParseLexedMethodDeclarations(ParsingClass &Class) {
  bool HasTemplateScope = !Class.TopLevelClass && Class.TemplateScope;
  ParseScope ClassTemplateScope(this, Scope::TemplateParamScope, HasTemplateScope);
  if (HasTemplateScope)
    Actions.ActOnReenterTemplateScope(getCurScope(), Class.TagOrTemplate);

  // The current scope is still active if we're the top-level class.
  // Otherwise we'll need to push and enter a new scope.
  bool HasClassScope = !Class.TopLevelClass;
  ParseScope ClassScope(this, Scope::ClassScope|Scope::DeclScope,
                        HasClassScope);
  if (HasClassScope)
    Actions.ActOnStartDelayedMemberDeclarations(getCurScope(), Class.TagOrTemplate);

  for (size_t i = 0; i < Class.LateParsedDeclarations.size(); ++i) {
    Class.LateParsedDeclarations[i]->ParseLexedMethodDeclarations();
  }

  if (HasClassScope)
    Actions.ActOnFinishDelayedMemberDeclarations(getCurScope(), Class.TagOrTemplate);
}

void Parser::ParseLexedMethodDeclaration(LateParsedMethodDeclaration &LM) {
  // If this is a member template, introduce the template parameter scope.
  ParseScope TemplateScope(this, Scope::TemplateParamScope, LM.TemplateScope);
  if (LM.TemplateScope)
    Actions.ActOnReenterTemplateScope(getCurScope(), LM.Method);

  // Start the delayed C++ method declaration
  Actions.ActOnStartDelayedCXXMethodDeclaration(getCurScope(), LM.Method);

  // Introduce the parameters into scope and parse their default
  // arguments.
  ParseScope PrototypeScope(this,
                            Scope::FunctionPrototypeScope|Scope::DeclScope);
  for (unsigned I = 0, N = LM.DefaultArgs.size(); I != N; ++I) {
    // Introduce the parameter into scope.
    Actions.ActOnDelayedCXXMethodParameter(getCurScope(), LM.DefaultArgs[I].Param);

    if (CachedTokens *Toks = LM.DefaultArgs[I].Toks) {
      // Save the current token position.
      SourceLocation origLoc = Tok.getLocation();

      // Parse the default argument from its saved token stream.
      Toks->push_back(Tok); // So that the current token doesn't get lost
      PP.EnterTokenStream(&Toks->front(), Toks->size(), true, false);

      // Consume the previously-pushed token.
      ConsumeAnyToken();

      // Consume the '='.
      assert(Tok.is(tok::equal) && "Default argument not starting with '='");
      SourceLocation EqualLoc = ConsumeToken();

      // The argument isn't actually potentially evaluated unless it is
      // used.
      EnterExpressionEvaluationContext Eval(Actions,
                                            Sema::PotentiallyEvaluatedIfUsed);

      ExprResult DefArgResult(ParseAssignmentExpression());
      if (DefArgResult.isInvalid())
        Actions.ActOnParamDefaultArgumentError(LM.DefaultArgs[I].Param);
      else {
        if (Tok.is(tok::cxx_defaultarg_end))
          ConsumeToken();
        else
          Diag(Tok.getLocation(), diag::err_default_arg_unparsed);
        Actions.ActOnParamDefaultArgument(LM.DefaultArgs[I].Param, EqualLoc,
                                          DefArgResult.take());
      }

      assert(!PP.getSourceManager().isBeforeInTranslationUnit(origLoc,
                                                         Tok.getLocation()) &&
             "ParseAssignmentExpression went over the default arg tokens!");
      // There could be leftover tokens (e.g. because of an error).
      // Skip through until we reach the original token position.
      while (Tok.getLocation() != origLoc && Tok.isNot(tok::eof))
        ConsumeAnyToken();

      delete Toks;
      LM.DefaultArgs[I].Toks = 0;
    }
  }
  PrototypeScope.Exit();

  // Finish the delayed C++ method declaration.
  Actions.ActOnFinishDelayedCXXMethodDeclaration(getCurScope(), LM.Method);
}

/// ParseLexedMethodDefs - We finished parsing the member specification of a top
/// (non-nested) C++ class. Now go over the stack of lexed methods that were
/// collected during its parsing and parse them all.
void Parser::ParseLexedMethodDefs(ParsingClass &Class) {
  bool HasTemplateScope = !Class.TopLevelClass && Class.TemplateScope;
  ParseScope ClassTemplateScope(this, Scope::TemplateParamScope, HasTemplateScope);
  if (HasTemplateScope)
    Actions.ActOnReenterTemplateScope(getCurScope(), Class.TagOrTemplate);

  bool HasClassScope = !Class.TopLevelClass;
  ParseScope ClassScope(this, Scope::ClassScope|Scope::DeclScope,
                        HasClassScope);

  for (size_t i = 0; i < Class.LateParsedDeclarations.size(); ++i) {
    Class.LateParsedDeclarations[i]->ParseLexedMethodDefs();
  }
}

void Parser::ParseLexedMethodDef(LexedMethod &LM) {
  // If this is a member template, introduce the template parameter scope.
  ParseScope TemplateScope(this, Scope::TemplateParamScope, LM.TemplateScope);
  if (LM.TemplateScope)
    Actions.ActOnReenterTemplateScope(getCurScope(), LM.D);

  // Save the current token position.
  SourceLocation origLoc = Tok.getLocation();

  assert(!LM.Toks.empty() && "Empty body!");
  // Append the current token at the end of the new token stream so that it
  // doesn't get lost.
  LM.Toks.push_back(Tok);
  PP.EnterTokenStream(LM.Toks.data(), LM.Toks.size(), true, false);

  // Consume the previously pushed token.
  ConsumeAnyToken();
  assert((Tok.is(tok::l_brace) || Tok.is(tok::colon) || Tok.is(tok::kw_try))
         && "Inline method not starting with '{', ':' or 'try'");

  // Parse the method body. Function body parsing code is similar enough
  // to be re-used for method bodies as well.
  ParseScope FnScope(this, Scope::FnScope|Scope::DeclScope);
  Actions.ActOnStartOfFunctionDef(getCurScope(), LM.D);

  if (Tok.is(tok::kw_try)) {
    ParseFunctionTryBlock(LM.D, FnScope);
    assert(!PP.getSourceManager().isBeforeInTranslationUnit(origLoc,
                                                         Tok.getLocation()) &&
           "ParseFunctionTryBlock went over the cached tokens!");
    // There could be leftover tokens (e.g. because of an error).
    // Skip through until we reach the original token position.
    while (Tok.getLocation() != origLoc && Tok.isNot(tok::eof))
      ConsumeAnyToken();
    return;
  }
  if (Tok.is(tok::colon)) {
    ParseConstructorInitializer(LM.D);

    // Error recovery.
    if (!Tok.is(tok::l_brace)) {
      FnScope.Exit();
      Actions.ActOnFinishFunctionBody(LM.D, 0);
      while (Tok.getLocation() != origLoc && Tok.isNot(tok::eof))
        ConsumeAnyToken();
      return;
    }
  } else
    Actions.ActOnDefaultCtorInitializers(LM.D);

  ParseFunctionStatementBody(LM.D, FnScope);

  if (Tok.getLocation() != origLoc) {
    // Due to parsing error, we either went over the cached tokens or
    // there are still cached tokens left. If it's the latter case skip the
    // leftover tokens.
    // Since this is an uncommon situation that should be avoided, use the
    // expensive isBeforeInTranslationUnit call.
    if (PP.getSourceManager().isBeforeInTranslationUnit(Tok.getLocation(),
                                                        origLoc))
      while (Tok.getLocation() != origLoc && Tok.isNot(tok::eof))
        ConsumeAnyToken();
  }
}

/// ParseLexedMemberInitializers - We finished parsing the member specification
/// of a top (non-nested) C++ class. Now go over the stack of lexed data member
/// initializers that were collected during its parsing and parse them all.
void Parser::ParseLexedMemberInitializers(ParsingClass &Class) {
  bool HasTemplateScope = !Class.TopLevelClass && Class.TemplateScope;
  ParseScope ClassTemplateScope(this, Scope::TemplateParamScope,
                                HasTemplateScope);
  if (HasTemplateScope)
    Actions.ActOnReenterTemplateScope(getCurScope(), Class.TagOrTemplate);

  // Set or update the scope flags to include Scope::ThisScope.
  bool AlreadyHasClassScope = Class.TopLevelClass;
  unsigned ScopeFlags = Scope::ClassScope|Scope::DeclScope|Scope::ThisScope;
  ParseScope ClassScope(this, ScopeFlags, !AlreadyHasClassScope);
  ParseScopeFlags ClassScopeFlags(this, ScopeFlags, AlreadyHasClassScope);

  if (!AlreadyHasClassScope)
    Actions.ActOnStartDelayedMemberDeclarations(getCurScope(),
                                                Class.TagOrTemplate);

  for (size_t i = 0; i < Class.LateParsedDeclarations.size(); ++i) {
    Class.LateParsedDeclarations[i]->ParseLexedMemberInitializers();
  }

  if (!AlreadyHasClassScope)
    Actions.ActOnFinishDelayedMemberDeclarations(getCurScope(),
                                                 Class.TagOrTemplate);

  Actions.ActOnFinishDelayedMemberInitializers(Class.TagOrTemplate);
}

void Parser::ParseLexedMemberInitializer(LateParsedMemberInitializer &MI) {
  if (!MI.Field || MI.Field->isInvalidDecl())
    return;

  // Append the current token at the end of the new token stream so that it
  // doesn't get lost.
  MI.Toks.push_back(Tok);
  PP.EnterTokenStream(MI.Toks.data(), MI.Toks.size(), true, false);

  // Consume the previously pushed token.
  ConsumeAnyToken();

  SourceLocation EqualLoc;
  ExprResult Init = ParseCXXMemberInitializer(/*IsFunction=*/false, EqualLoc);

  Actions.ActOnCXXInClassMemberInitializer(MI.Field, EqualLoc, Init.release());

  // The next token should be our artificial terminating EOF token.
  if (Tok.isNot(tok::eof)) {
    SourceLocation EndLoc = PP.getLocForEndOfToken(PrevTokLocation);
    if (!EndLoc.isValid())
      EndLoc = Tok.getLocation();
    // No fixit; we can't recover as if there were a semicolon here.
    Diag(EndLoc, diag::err_expected_semi_decl_list);

    // Consume tokens until we hit the artificial EOF.
    while (Tok.isNot(tok::eof))
      ConsumeAnyToken();
  }
  ConsumeAnyToken();
}

/// ConsumeAndStoreUntil - Consume and store the token at the passed token
/// container until the token 'T' is reached (which gets
/// consumed/stored too, if ConsumeFinalToken).
/// If StopAtSemi is true, then we will stop early at a ';' character.
/// Returns true if token 'T1' or 'T2' was found.
/// NOTE: This is a specialized version of Parser::SkipUntil.
bool Parser::ConsumeAndStoreUntil(tok::TokenKind T1, tok::TokenKind T2,
                                  CachedTokens &Toks,
                                  bool StopAtSemi, bool ConsumeFinalToken) {
  // We always want this function to consume at least one token if the first
  // token isn't T and if not at EOF.
  bool isFirstTokenConsumed = true;
  while (1) {
    // If we found one of the tokens, stop and return true.
    if (Tok.is(T1) || Tok.is(T2)) {
      if (ConsumeFinalToken) {
        Toks.push_back(Tok);
        ConsumeAnyToken();
      }
      return true;
    }

    switch (Tok.getKind()) {
    case tok::eof:
      // Ran out of tokens.
      return false;

    case tok::l_paren:
      // Recursively consume properly-nested parens.
      Toks.push_back(Tok);
      ConsumeParen();
      ConsumeAndStoreUntil(tok::r_paren, Toks, /*StopAtSemi=*/false);
      break;
    case tok::l_square:
      // Recursively consume properly-nested square brackets.
      Toks.push_back(Tok);
      ConsumeBracket();
      ConsumeAndStoreUntil(tok::r_square, Toks, /*StopAtSemi=*/false);
      break;
    case tok::l_brace:
      // Recursively consume properly-nested braces.
      Toks.push_back(Tok);
      ConsumeBrace();
      ConsumeAndStoreUntil(tok::r_brace, Toks, /*StopAtSemi=*/false);
      break;

    // Okay, we found a ']' or '}' or ')', which we think should be balanced.
    // Since the user wasn't looking for this token (if they were, it would
    // already be handled), this isn't balanced.  If there is a LHS token at a
    // higher level, we will assume that this matches the unbalanced token
    // and return it.  Otherwise, this is a spurious RHS token, which we skip.
    case tok::r_paren:
      if (ParenCount && !isFirstTokenConsumed)
        return false;  // Matches something.
      Toks.push_back(Tok);
      ConsumeParen();
      break;
    case tok::r_square:
      if (BracketCount && !isFirstTokenConsumed)
        return false;  // Matches something.
      Toks.push_back(Tok);
      ConsumeBracket();
      break;
    case tok::r_brace:
      if (BraceCount && !isFirstTokenConsumed)
        return false;  // Matches something.
      Toks.push_back(Tok);
      ConsumeBrace();
      break;

    case tok::code_completion:
      Toks.push_back(Tok);
      ConsumeCodeCompletionToken();
      break;

    case tok::string_literal:
    case tok::wide_string_literal:
    case tok::utf8_string_literal:
    case tok::utf16_string_literal:
    case tok::utf32_string_literal:
      Toks.push_back(Tok);
      ConsumeStringToken();
      break;
    case tok::semi:
      if (StopAtSemi)
        return false;
      // FALL THROUGH.
    default:
      // consume this token.
      Toks.push_back(Tok);
      ConsumeToken();
      break;
    }
    isFirstTokenConsumed = false;
  }
}

/// \brief Consume tokens and store them in the passed token container until
/// we've passed the try keyword and constructor initializers and have consumed
/// the opening brace of the function body. The opening brace will be consumed
/// if and only if there was no error.
///
/// \return True on error. 
bool Parser::ConsumeAndStoreFunctionPrologue(CachedTokens &Toks) {
  if (Tok.is(tok::kw_try)) {
    Toks.push_back(Tok);
    ConsumeToken();
  }
  if (Tok.is(tok::colon)) {
    // Initializers can contain braces too.
    Toks.push_back(Tok);
    ConsumeToken();

    while (Tok.is(tok::identifier) || Tok.is(tok::coloncolon)) {
      if (Tok.is(tok::eof) || Tok.is(tok::semi))
        return true;

      // Grab the identifier.
      if (!ConsumeAndStoreUntil(tok::l_paren, tok::l_brace, Toks,
                                /*StopAtSemi=*/true,
                                /*ConsumeFinalToken=*/false))
        return true;

      tok::TokenKind kind = Tok.getKind();
      Toks.push_back(Tok);
      if (kind == tok::l_paren)
        ConsumeParen();
      else {
        assert(kind == tok::l_brace && "Must be left paren or brace here.");
        ConsumeBrace();
        // In C++03, this has to be the start of the function body, which
        // means the initializer is malformed.
        if (!getLang().CPlusPlus0x)
          return false;
      }

      // Grab the initializer
      if (!ConsumeAndStoreUntil(kind == tok::l_paren ? tok::r_paren :
                                                       tok::r_brace,
                                Toks, /*StopAtSemi=*/true))
        return true;

      // Grab the separating comma, if any.
      if (Tok.is(tok::comma)) {
        Toks.push_back(Tok);
        ConsumeToken();
      }
    }
  }

  // Grab any remaining garbage to be diagnosed later. We stop when we reach a
  // brace: an opening one is the function body, while a closing one probably
  // means we've reached the end of the class.
  if (!ConsumeAndStoreUntil(tok::l_brace, tok::r_brace, Toks,
                            /*StopAtSemi=*/true, /*ConsumeFinalToken=*/false))
    return true;
  if(Tok.isNot(tok::l_brace))
    return true;

  Toks.push_back(Tok);
  ConsumeBrace();
  return false;
}
