//===--- PPCallbacks.h - Callbacks for Preprocessor actions -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the PPCallbacks interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_PPCALLBACKS_H
#define LLVM_CLANG_LEX_PPCALLBACKS_H

#include "clang/Lex/DirectoryLookup.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "llvm/ADT/StringRef.h"
#include <string>

namespace clang {
  class SourceLocation;
  class Token;
  class IdentifierInfo;
  class MacroInfo;

/// PPCallbacks - This interface provides a way to observe the actions of the
/// preprocessor as it does its thing.  Clients can define their hooks here to
/// implement preprocessor level tools.
class PPCallbacks {
public:
  virtual ~PPCallbacks();

  enum FileChangeReason {
    EnterFile, ExitFile, SystemHeaderPragma, RenameFile
  };

  /// FileChanged - This callback is invoked whenever a source file is
  /// entered or exited.  The SourceLocation indicates the new location, and
  /// EnteringFile indicates whether this is because we are entering a new
  /// #include'd file (when true) or whether we're exiting one because we ran
  /// off the end (when false).
  ///
  /// \param PrevFID the file that was exited if \arg Reason is ExitFile. 
  virtual void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                           SrcMgr::CharacteristicKind FileType,
                           FileID PrevFID = FileID()) {
  }

  /// FileSkipped - This callback is invoked whenever a source file is
  /// skipped as the result of header guard optimization.  ParentFile
  /// is the file that #includes the skipped file.  FilenameTok is the
  /// token in ParentFile that indicates the skipped file.
  virtual void FileSkipped(const FileEntry &ParentFile,
                           const Token &FilenameTok,
                           SrcMgr::CharacteristicKind FileType) {
  }

  /// FileNotFound - This callback is invoked whenever an inclusion directive
  /// results in a file-not-found error.
  ///
  /// \param FileName The name of the file being included, as written in the 
  /// source code.
  ///
  /// \param RecoveryPath If this client indicates that it can recover from 
  /// this missing file, the client should set this as an additional header
  /// search patch.
  ///
  /// \returns true to indicate that the preprocessor should attempt to recover
  /// by adding \p RecoveryPath as a header search path.
  virtual bool FileNotFound(StringRef FileName,
                            SmallVectorImpl<char> &RecoveryPath) {
    return false;
  }

  /// \brief This callback is invoked whenever an inclusion directive of
  /// any kind (\c #include, \c #import, etc.) has been processed, regardless
  /// of whether the inclusion will actually result in an inclusion.
  ///
  /// \param HashLoc The location of the '#' that starts the inclusion 
  /// directive.
  ///
  /// \param IncludeTok The token that indicates the kind of inclusion 
  /// directive, e.g., 'include' or 'import'.
  ///
  /// \param FileName The name of the file being included, as written in the 
  /// source code.
  ///
  /// \param IsAngled Whether the file name was enclosed in angle brackets;
  /// otherwise, it was enclosed in quotes.
  ///
  /// \param File The actual file that may be included by this inclusion 
  /// directive.
  ///
  /// \param EndLoc The location of the last token within the inclusion
  /// directive.
  ///
  /// \param SearchPath Contains the search path which was used to find the file
  /// in the file system. If the file was found via an absolute include path,
  /// SearchPath will be empty. For framework includes, the SearchPath and
  /// RelativePath will be split up. For example, if an include of "Some/Some.h"
  /// is found via the framework path
  /// "path/to/Frameworks/Some.framework/Headers/Some.h", SearchPath will be
  /// "path/to/Frameworks/Some.framework/Headers" and RelativePath will be
  /// "Some.h".
  ///
  /// \param RelativePath The path relative to SearchPath, at which the include
  /// file was found. This is equal to FileName except for framework includes.
  virtual void InclusionDirective(SourceLocation HashLoc,
                                  const Token &IncludeTok,
                                  StringRef FileName,
                                  bool IsAngled,
                                  const FileEntry *File,
                                  SourceLocation EndLoc,
                                  StringRef SearchPath,
                                  StringRef RelativePath) {
  }

  /// EndOfMainFile - This callback is invoked when the end of the main file is
  /// reach, no subsequent callbacks will be made.
  virtual void EndOfMainFile() {
  }

  /// Ident - This callback is invoked when a #ident or #sccs directive is read.
  /// \param Loc The location of the directive.
  /// \param str The text of the directive.
  ///
  virtual void Ident(SourceLocation Loc, const std::string &str) {
  }

  /// PragmaComment - This callback is invoked when a #pragma comment directive
  /// is read.
  ///
  virtual void PragmaComment(SourceLocation Loc, const IdentifierInfo *Kind,
                             const std::string &Str) {
  }

  /// PragmaMessage - This callback is invoked when a #pragma message directive
  /// is read.
  /// \param Loc The location of the message directive.
  /// \param str The text of the message directive.
  ///
  virtual void PragmaMessage(SourceLocation Loc, StringRef Str) {
  }

  /// PragmaDiagnosticPush - This callback is invoked when a
  /// #pragma gcc dianostic push directive is read.
  virtual void PragmaDiagnosticPush(SourceLocation Loc,
                                    StringRef Namespace) {
  }

  /// PragmaDiagnosticPop - This callback is invoked when a
  /// #pragma gcc dianostic pop directive is read.
  virtual void PragmaDiagnosticPop(SourceLocation Loc,
                                   StringRef Namespace) {
  }

  /// PragmaDiagnostic - This callback is invoked when a
  /// #pragma gcc dianostic directive is read.
  virtual void PragmaDiagnostic(SourceLocation Loc, StringRef Namespace,
                                diag::Mapping mapping, StringRef Str) {
  }

  /// MacroExpands - This is called by
  /// Preprocessor::HandleMacroExpandedIdentifier when a macro invocation is
  /// found.
  virtual void MacroExpands(const Token &MacroNameTok, const MacroInfo* MI,
                            SourceRange Range) {
  }

  /// MacroDefined - This hook is called whenever a macro definition is seen.
  virtual void MacroDefined(const Token &MacroNameTok, const MacroInfo *MI) {
  }

  /// MacroUndefined - This hook is called whenever a macro #undef is seen.
  /// MI is released immediately following this callback.
  virtual void MacroUndefined(const Token &MacroNameTok, const MacroInfo *MI) {
  }
  
  /// Defined - This hook is called whenever the 'defined' operator is seen.
  virtual void Defined(const Token &MacroNameTok) {
  }
  
  /// SourceRangeSkipped - This hook is called when a source range is skipped.
  /// \param Range The SourceRange that was skipped. The range begins at the
  /// #if/#else directive and ends after the #endif/#else directive.
  virtual void SourceRangeSkipped(SourceRange Range) {
  }

  /// If -- This hook is called whenever an #if is seen.
  /// \param Loc the source location of the directive.
  /// \param ConditionRange The SourceRange of the expression being tested.
  // FIXME: better to pass in a list (or tree!) of Tokens.
  virtual void If(SourceLocation Loc, SourceRange ConditionRange) {
  }

  /// Elif -- This hook is called whenever an #elif is seen.
  /// \param Loc the source location of the directive.
  /// \param ConditionRange The SourceRange of the expression being tested.
  /// \param IfLoc the source location of the #if/#ifdef/#ifndef directive.
  // FIXME: better to pass in a list (or tree!) of Tokens.
  virtual void Elif(SourceLocation Loc, SourceRange ConditionRange,
                    SourceLocation IfLoc) {
  }

  /// Ifdef -- This hook is called whenever an #ifdef is seen.
  /// \param Loc the source location of the directive.
  /// \param II Information on the token being tested.
  virtual void Ifdef(SourceLocation Loc, const Token &MacroNameTok) {
  }

  /// Ifndef -- This hook is called whenever an #ifndef is seen.
  /// \param Loc the source location of the directive.
  /// \param II Information on the token being tested.
  virtual void Ifndef(SourceLocation Loc, const Token &MacroNameTok) {
  }

  /// Else -- This hook is called whenever an #else is seen.
  /// \param Loc the source location of the directive.
  /// \param IfLoc the source location of the #if/#ifdef/#ifndef directive.
  virtual void Else(SourceLocation Loc, SourceLocation IfLoc) {
  }

  /// Endif -- This hook is called whenever an #endif is seen.
  /// \param Loc the source location of the directive.
  /// \param IfLoc the source location of the #if/#ifdef/#ifndef directive.
  virtual void Endif(SourceLocation Loc, SourceLocation IfLoc) {
  }
};

/// PPChainedCallbacks - Simple wrapper class for chaining callbacks.
class PPChainedCallbacks : public PPCallbacks {
  virtual void anchor();
  PPCallbacks *First, *Second;

public:
  PPChainedCallbacks(PPCallbacks *_First, PPCallbacks *_Second)
    : First(_First), Second(_Second) {}
  ~PPChainedCallbacks() {
    delete Second;
    delete First;
  }

  virtual void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                           SrcMgr::CharacteristicKind FileType,
                           FileID PrevFID) {
    First->FileChanged(Loc, Reason, FileType, PrevFID);
    Second->FileChanged(Loc, Reason, FileType, PrevFID);
  }

  virtual void FileSkipped(const FileEntry &ParentFile,
                           const Token &FilenameTok,
                           SrcMgr::CharacteristicKind FileType) {
    First->FileSkipped(ParentFile, FilenameTok, FileType);
    Second->FileSkipped(ParentFile, FilenameTok, FileType);
  }

  virtual bool FileNotFound(StringRef FileName,
                            SmallVectorImpl<char> &RecoveryPath) {
    return First->FileNotFound(FileName, RecoveryPath) ||
           Second->FileNotFound(FileName, RecoveryPath);
  }

  virtual void InclusionDirective(SourceLocation HashLoc,
                                  const Token &IncludeTok,
                                  StringRef FileName,
                                  bool IsAngled,
                                  const FileEntry *File,
                                  SourceLocation EndLoc,
                                  StringRef SearchPath,
                                  StringRef RelativePath) {
    First->InclusionDirective(HashLoc, IncludeTok, FileName, IsAngled, File,
                              EndLoc, SearchPath, RelativePath);
    Second->InclusionDirective(HashLoc, IncludeTok, FileName, IsAngled, File,
                               EndLoc, SearchPath, RelativePath);
  }

  virtual void EndOfMainFile() {
    First->EndOfMainFile();
    Second->EndOfMainFile();
  }

  virtual void Ident(SourceLocation Loc, const std::string &str) {
    First->Ident(Loc, str);
    Second->Ident(Loc, str);
  }

  virtual void PragmaComment(SourceLocation Loc, const IdentifierInfo *Kind,
                             const std::string &Str) {
    First->PragmaComment(Loc, Kind, Str);
    Second->PragmaComment(Loc, Kind, Str);
  }

  virtual void PragmaMessage(SourceLocation Loc, StringRef Str) {
    First->PragmaMessage(Loc, Str);
    Second->PragmaMessage(Loc, Str);
  }

  virtual void PragmaDiagnosticPush(SourceLocation Loc,
                                    StringRef Namespace) {
    First->PragmaDiagnosticPush(Loc, Namespace);
    Second->PragmaDiagnosticPush(Loc, Namespace);
  }

  virtual void PragmaDiagnosticPop(SourceLocation Loc,
                                    StringRef Namespace) {
    First->PragmaDiagnosticPop(Loc, Namespace);
    Second->PragmaDiagnosticPop(Loc, Namespace);
  }

  virtual void PragmaDiagnostic(SourceLocation Loc, StringRef Namespace,
                                diag::Mapping mapping, StringRef Str) {
    First->PragmaDiagnostic(Loc, Namespace, mapping, Str);
    Second->PragmaDiagnostic(Loc, Namespace, mapping, Str);
  }

  virtual void MacroExpands(const Token &MacroNameTok, const MacroInfo* MI,
                            SourceRange Range) {
    First->MacroExpands(MacroNameTok, MI, Range);
    Second->MacroExpands(MacroNameTok, MI, Range);
  }

  virtual void MacroDefined(const Token &MacroNameTok, const MacroInfo *MI) {
    First->MacroDefined(MacroNameTok, MI);
    Second->MacroDefined(MacroNameTok, MI);
  }

  virtual void MacroUndefined(const Token &MacroNameTok, const MacroInfo *MI) {
    First->MacroUndefined(MacroNameTok, MI);
    Second->MacroUndefined(MacroNameTok, MI);
  }

  virtual void Defined(const Token &MacroNameTok) {
    First->Defined(MacroNameTok);
    Second->Defined(MacroNameTok);
  }

  virtual void SourceRangeSkipped(SourceRange Range) {
    First->SourceRangeSkipped(Range);
    Second->SourceRangeSkipped(Range);
  }

  /// If -- This hook is called whenever an #if is seen.
  virtual void If(SourceLocation Loc, SourceRange ConditionRange) {
    First->If(Loc, ConditionRange);
    Second->If(Loc, ConditionRange);
  }

  /// Elif -- This hook is called whenever an #if is seen.
  virtual void Elif(SourceLocation Loc, SourceRange ConditionRange,
                    SourceLocation IfLoc) {
    First->Elif(Loc, ConditionRange, IfLoc);
    Second->Elif(Loc, ConditionRange, IfLoc);
  }

  /// Ifdef -- This hook is called whenever an #ifdef is seen.
  virtual void Ifdef(SourceLocation Loc, const Token &MacroNameTok) {
    First->Ifdef(Loc, MacroNameTok);
    Second->Ifdef(Loc, MacroNameTok);
  }

  /// Ifndef -- This hook is called whenever an #ifndef is seen.
  virtual void Ifndef(SourceLocation Loc, const Token &MacroNameTok) {
    First->Ifndef(Loc, MacroNameTok);
    Second->Ifndef(Loc, MacroNameTok);
  }

  /// Else -- This hook is called whenever an #else is seen.
  virtual void Else(SourceLocation Loc, SourceLocation IfLoc) {
    First->Else(Loc, IfLoc);
    Second->Else(Loc, IfLoc);
  }

  /// Endif -- This hook is called whenever an #endif is seen.
  virtual void Endif(SourceLocation Loc, SourceLocation IfLoc) {
    First->Endif(Loc, IfLoc);
    Second->Endif(Loc, IfLoc);
  }
};

}  // end namespace clang

#endif
