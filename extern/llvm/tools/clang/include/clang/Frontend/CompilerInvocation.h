//===-- CompilerInvocation.h - Compiler Invocation Helper Data --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_COMPILERINVOCATION_H_
#define LLVM_CLANG_FRONTEND_COMPILERINVOCATION_H_

#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Frontend/AnalyzerOptions.h"
#include "clang/Frontend/MigratorOptions.h"
#include "clang/Frontend/CodeGenOptions.h"
#include "clang/Frontend/DependencyOutputOptions.h"
#include "clang/Frontend/DiagnosticOptions.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Frontend/HeaderSearchOptions.h"
#include "clang/Frontend/LangStandard.h"
#include "clang/Frontend/PreprocessorOptions.h"
#include "clang/Frontend/PreprocessorOutputOptions.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringMap.h"
#include <string>
#include <vector>

namespace clang {

class CompilerInvocation;
class DiagnosticsEngine;

namespace driver {
class ArgList;
}

/// CompilerInvocation - Fill out Opts based on the options given in Args.
/// Args must have been created from the OptTable returned by
/// createCC1OptTable(). When errors are encountered, return false and,
/// if Diags is non-null, report the error(s).
bool ParseDiagnosticArgs(DiagnosticOptions &Opts, driver::ArgList &Args,
                         DiagnosticsEngine *Diags = 0);
  
class CompilerInvocationBase : public RefCountedBase<CompilerInvocation> {
protected:
  /// Options controlling the language variant.
  IntrusiveRefCntPtr<LangOptions> LangOpts;
public:
  CompilerInvocationBase();

  CompilerInvocationBase(const CompilerInvocationBase &X);
  
  LangOptions *getLangOpts() { return LangOpts.getPtr(); }
  const LangOptions *getLangOpts() const { return LangOpts.getPtr(); }
};
  
/// CompilerInvocation - Helper class for holding the data necessary to invoke
/// the compiler.
///
/// This class is designed to represent an abstract "invocation" of the
/// compiler, including data such as the include paths, the code generation
/// options, the warning flags, and so on.
class CompilerInvocation : public CompilerInvocationBase {
  /// Options controlling the static analyzer.
  AnalyzerOptions AnalyzerOpts;

  MigratorOptions MigratorOpts;
  
  /// Options controlling IRgen and the backend.
  CodeGenOptions CodeGenOpts;

  /// Options controlling dependency output.
  DependencyOutputOptions DependencyOutputOpts;

  /// Options controlling the diagnostic engine.
  DiagnosticOptions DiagnosticOpts;

  /// Options controlling file system operations.
  FileSystemOptions FileSystemOpts;

  /// Options controlling the frontend itself.
  FrontendOptions FrontendOpts;

  /// Options controlling the #include directive.
  HeaderSearchOptions HeaderSearchOpts;

  /// Options controlling the preprocessor (aside from #include handling).
  PreprocessorOptions PreprocessorOpts;

  /// Options controlling preprocessed output.
  PreprocessorOutputOptions PreprocessorOutputOpts;

  /// Options controlling the target.
  TargetOptions TargetOpts;

public:
  CompilerInvocation() {}

  /// @name Utility Methods
  /// @{

  /// CreateFromArgs - Create a compiler invocation from a list of input
  /// options. Returns true on success.
  ///
  /// \param Res [out] - The resulting invocation.
  /// \param ArgBegin - The first element in the argument vector.
  /// \param ArgEnd - The last element in the argument vector.
  /// \param Diags - The diagnostic engine to use for errors.
  static bool CreateFromArgs(CompilerInvocation &Res,
                             const char* const *ArgBegin,
                             const char* const *ArgEnd,
                             DiagnosticsEngine &Diags);

  /// GetBuiltinIncludePath - Get the directory where the compiler headers
  /// reside, relative to the compiler binary (found by the passed in
  /// arguments).
  ///
  /// \param Argv0 - The program path (from argv[0]), for finding the builtin
  /// compiler path.
  /// \param MainAddr - The address of main (or some other function in the main
  /// executable), for finding the builtin compiler path.
  static std::string GetResourcesPath(const char *Argv0, void *MainAddr);

  /// toArgs - Convert the CompilerInvocation to a list of strings suitable for
  /// passing to CreateFromArgs.
  void toArgs(std::vector<std::string> &Res);

  /// setLangDefaults - Set language defaults for the given input language and
  /// language standard in this CompilerInvocation.
  ///
  /// \param IK - The input language.
  /// \param LangStd - The input language standard.
  void setLangDefaults(InputKind IK,
                  LangStandard::Kind LangStd = LangStandard::lang_unspecified) {
    setLangDefaults(*getLangOpts(), IK, LangStd);
  }

  /// setLangDefaults - Set language defaults for the given input language and
  /// language standard in the given LangOptions object.
  ///
  /// \param LangOpts - The LangOptions object to set up.
  /// \param IK - The input language.
  /// \param LangStd - The input language standard.
  static void setLangDefaults(LangOptions &Opts, InputKind IK,
                   LangStandard::Kind LangStd = LangStandard::lang_unspecified);
  
  /// \brief Retrieve a module hash string that is suitable for uniquely 
  /// identifying the conditions under which the module was built.
  std::string getModuleHash() const;
  
  /// @}
  /// @name Option Subgroups
  /// @{

  AnalyzerOptions &getAnalyzerOpts() { return AnalyzerOpts; }
  const AnalyzerOptions &getAnalyzerOpts() const {
    return AnalyzerOpts;
  }

  MigratorOptions &getMigratorOpts() { return MigratorOpts; }
  const MigratorOptions &getMigratorOpts() const {
    return MigratorOpts;
  }
  
  CodeGenOptions &getCodeGenOpts() { return CodeGenOpts; }
  const CodeGenOptions &getCodeGenOpts() const {
    return CodeGenOpts;
  }

  DependencyOutputOptions &getDependencyOutputOpts() {
    return DependencyOutputOpts;
  }
  const DependencyOutputOptions &getDependencyOutputOpts() const {
    return DependencyOutputOpts;
  }

  DiagnosticOptions &getDiagnosticOpts() { return DiagnosticOpts; }
  const DiagnosticOptions &getDiagnosticOpts() const { return DiagnosticOpts; }

  FileSystemOptions &getFileSystemOpts() { return FileSystemOpts; }
  const FileSystemOptions &getFileSystemOpts() const {
    return FileSystemOpts;
  }

  HeaderSearchOptions &getHeaderSearchOpts() { return HeaderSearchOpts; }
  const HeaderSearchOptions &getHeaderSearchOpts() const {
    return HeaderSearchOpts;
  }

  FrontendOptions &getFrontendOpts() { return FrontendOpts; }
  const FrontendOptions &getFrontendOpts() const {
    return FrontendOpts;
  }

  PreprocessorOptions &getPreprocessorOpts() { return PreprocessorOpts; }
  const PreprocessorOptions &getPreprocessorOpts() const {
    return PreprocessorOpts;
  }

  PreprocessorOutputOptions &getPreprocessorOutputOpts() {
    return PreprocessorOutputOpts;
  }
  const PreprocessorOutputOptions &getPreprocessorOutputOpts() const {
    return PreprocessorOutputOpts;
  }

  TargetOptions &getTargetOpts() { return TargetOpts; }
  const TargetOptions &getTargetOpts() const {
    return TargetOpts;
  }

  /// @}
};

} // end namespace clang

#endif
