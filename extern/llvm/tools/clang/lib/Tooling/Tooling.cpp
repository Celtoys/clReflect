//===--- Tooling.cpp - Running clang standalone tools ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements functions to run clang tools standalone instead
//  of running them as a plugin.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Tool.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/raw_ostream.h"

namespace clang {
namespace tooling {

FrontendActionFactory::~FrontendActionFactory() {}

// FIXME: This file contains structural duplication with other parts of the
// code that sets up a compiler to run tools on it, and we should refactor
// it to be based on the same framework.

/// \brief Builds a clang driver initialized for running clang tools.
static clang::driver::Driver *newDriver(clang::DiagnosticsEngine *Diagnostics,
                                        const char *BinaryName) {
  const std::string DefaultOutputName = "a.out";
  clang::driver::Driver *CompilerDriver = new clang::driver::Driver(
      BinaryName, llvm::sys::getDefaultTargetTriple(),
      DefaultOutputName, false, *Diagnostics);
  CompilerDriver->setTitle("clang_based_tool");
  return CompilerDriver;
}

/// \brief Retrieves the clang CC1 specific flags out of the compilation's jobs.
///
/// Returns NULL on error.
static const clang::driver::ArgStringList *getCC1Arguments(
    clang::DiagnosticsEngine *Diagnostics,
    clang::driver::Compilation *Compilation) {
  // We expect to get back exactly one Command job, if we didn't something
  // failed. Extract that job from the Compilation.
  const clang::driver::JobList &Jobs = Compilation->getJobs();
  if (Jobs.size() != 1 || !isa<clang::driver::Command>(*Jobs.begin())) {
    llvm::SmallString<256> error_msg;
    llvm::raw_svector_ostream error_stream(error_msg);
    Compilation->PrintJob(error_stream, Compilation->getJobs(), "; ", true);
    Diagnostics->Report(clang::diag::err_fe_expected_compiler_job)
        << error_stream.str();
    return NULL;
  }

  // The one job we find should be to invoke clang again.
  const clang::driver::Command *Cmd =
      cast<clang::driver::Command>(*Jobs.begin());
  if (StringRef(Cmd->getCreator().getName()) != "clang") {
    Diagnostics->Report(clang::diag::err_fe_expected_clang_command);
    return NULL;
  }

  return &Cmd->getArguments();
}

/// \brief Returns a clang build invocation initialized from the CC1 flags.
static clang::CompilerInvocation *newInvocation(
    clang::DiagnosticsEngine *Diagnostics,
    const clang::driver::ArgStringList &CC1Args) {
  assert(!CC1Args.empty() && "Must at least contain the program name!");
  clang::CompilerInvocation *Invocation = new clang::CompilerInvocation;
  clang::CompilerInvocation::CreateFromArgs(
      *Invocation, CC1Args.data() + 1, CC1Args.data() + CC1Args.size(),
      *Diagnostics);
  Invocation->getFrontendOpts().DisableFree = false;
  return Invocation;
}

bool runToolOnCode(clang::FrontendAction *ToolAction, const Twine &Code,
                   const Twine &FileName) {
  SmallString<16> FileNameStorage;
  StringRef FileNameRef = FileName.toNullTerminatedStringRef(FileNameStorage);
  const char *const CommandLine[] = {
      "clang-tool", "-fsyntax-only", FileNameRef.data()
  };
  FileManager Files((FileSystemOptions()));
  ToolInvocation Invocation(
      std::vector<std::string>(
          CommandLine,
          CommandLine + llvm::array_lengthof(CommandLine)),
      ToolAction, &Files);

  SmallString<1024> CodeStorage;
  Invocation.mapVirtualFile(FileNameRef,
                            Code.toNullTerminatedStringRef(CodeStorage));
  return Invocation.run();
}

/// \brief Returns the absolute path of 'File', by prepending it with
/// 'BaseDirectory' if 'File' is not absolute.
///
/// Otherwise returns 'File'.
/// If 'File' starts with "./", the returned path will not contain the "./".
/// Otherwise, the returned path will contain the literal path-concatenation of
/// 'BaseDirectory' and 'File'.
///
/// \param File Either an absolute or relative path.
/// \param BaseDirectory An absolute path.
static std::string getAbsolutePath(
    StringRef File, StringRef BaseDirectory) {
  assert(llvm::sys::path::is_absolute(BaseDirectory));
  if (llvm::sys::path::is_absolute(File)) {
    return File;
  }
  StringRef RelativePath(File);
  if (RelativePath.startswith("./")) {
    RelativePath = RelativePath.substr(strlen("./"));
  }
  llvm::SmallString<1024> AbsolutePath(BaseDirectory);
  llvm::sys::path::append(AbsolutePath, RelativePath);
  return AbsolutePath.str();
}

ToolInvocation::ToolInvocation(
    ArrayRef<std::string> CommandLine, FrontendAction *ToolAction,
    FileManager *Files)
    : CommandLine(CommandLine.vec()), ToolAction(ToolAction), Files(Files) {
}

void ToolInvocation::mapVirtualFile(StringRef FilePath, StringRef Content) {
  MappedFileContents[FilePath] = Content;
}

bool ToolInvocation::run() {
  std::vector<const char*> Argv;
  for (int I = 0, E = CommandLine.size(); I != E; ++I)
    Argv.push_back(CommandLine[I].c_str());
  const char *const BinaryName = Argv[0];
  DiagnosticOptions DefaultDiagnosticOptions;
  TextDiagnosticPrinter DiagnosticPrinter(
      llvm::errs(), DefaultDiagnosticOptions);
  DiagnosticsEngine Diagnostics(llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs>(
      new DiagnosticIDs()), &DiagnosticPrinter, false);

  const llvm::OwningPtr<clang::driver::Driver> Driver(
      newDriver(&Diagnostics, BinaryName));
  // Since the input might only be virtual, don't check whether it exists.
  Driver->setCheckInputsExist(false);
  const llvm::OwningPtr<clang::driver::Compilation> Compilation(
      Driver->BuildCompilation(llvm::makeArrayRef(Argv)));
  const clang::driver::ArgStringList *const CC1Args = getCC1Arguments(
      &Diagnostics, Compilation.get());
  if (CC1Args == NULL) {
    return false;
  }
  llvm::OwningPtr<clang::CompilerInvocation> Invocation(
      newInvocation(&Diagnostics, *CC1Args));
  return runInvocation(BinaryName, Compilation.get(),
                       Invocation.take(), *CC1Args, ToolAction.take());
}

// Exists solely for the purpose of lookup of the resource path.
static int StaticSymbol;

bool ToolInvocation::runInvocation(
    const char *BinaryName,
    clang::driver::Compilation *Compilation,
    clang::CompilerInvocation *Invocation,
    const clang::driver::ArgStringList &CC1Args,
    clang::FrontendAction *ToolAction) {
  llvm::OwningPtr<clang::FrontendAction> ScopedToolAction(ToolAction);
  // Show the invocation, with -v.
  if (Invocation->getHeaderSearchOpts().Verbose) {
    llvm::errs() << "clang Invocation:\n";
    Compilation->PrintJob(llvm::errs(), Compilation->getJobs(), "\n", true);
    llvm::errs() << "\n";
  }

  // Create a compiler instance to handle the actual work.
  clang::CompilerInstance Compiler;
  Compiler.setInvocation(Invocation);
  Compiler.setFileManager(Files);
  // FIXME: What about LangOpts?

  // Create the compilers actual diagnostics engine.
  Compiler.createDiagnostics(CC1Args.size(),
                             const_cast<char**>(CC1Args.data()));
  if (!Compiler.hasDiagnostics())
    return false;

  Compiler.createSourceManager(*Files);
  addFileMappingsTo(Compiler.getSourceManager());

  // Infer the builtin include path if unspecified.
  if (Compiler.getHeaderSearchOpts().UseBuiltinIncludes &&
      Compiler.getHeaderSearchOpts().ResourceDir.empty()) {
    // This just needs to be some symbol in the binary.
    void *const SymbolAddr = &StaticSymbol;
    Compiler.getHeaderSearchOpts().ResourceDir =
        clang::CompilerInvocation::GetResourcesPath(BinaryName, SymbolAddr);
  }

  const bool Success = Compiler.ExecuteAction(*ToolAction);

  Compiler.resetAndLeakFileManager();
  return Success;
}

void ToolInvocation::addFileMappingsTo(SourceManager &Sources) {
  for (llvm::StringMap<StringRef>::const_iterator
           It = MappedFileContents.begin(), End = MappedFileContents.end();
       It != End; ++It) {
    // Inject the code as the given file name into the preprocessor options.
    const llvm::MemoryBuffer *Input =
        llvm::MemoryBuffer::getMemBuffer(It->getValue());
    // FIXME: figure out what '0' stands for.
    const FileEntry *FromFile = Files->getVirtualFile(
        It->getKey(), Input->getBufferSize(), 0);
    // FIXME: figure out memory management ('true').
    Sources.overrideFileContents(FromFile, Input, true);
  }
}

ClangTool::ClangTool(const CompilationDatabase &Compilations,
                     ArrayRef<std::string> SourcePaths)
    : Files((FileSystemOptions())) {
  llvm::SmallString<1024> BaseDirectory;
  if (const char *PWD = ::getenv("PWD"))
    BaseDirectory = PWD;
  else
    llvm::sys::fs::current_path(BaseDirectory);
  for (unsigned I = 0, E = SourcePaths.size(); I != E; ++I) {
    llvm::SmallString<1024> File(getAbsolutePath(
        SourcePaths[I], BaseDirectory));

    std::vector<CompileCommand> CompileCommands =
      Compilations.getCompileCommands(File.str());
    if (!CompileCommands.empty()) {
      for (int I = 0, E = CompileCommands.size(); I != E; ++I) {
        CompileCommand &Command = CompileCommands[I];
        if (!Command.Directory.empty()) {
          // FIXME: What should happen if CommandLine includes -working-directory
          // as well?
          Command.CommandLine.push_back(
            "-working-directory=" + Command.Directory);
        }
        CommandLines.push_back(std::make_pair(File.str(), Command.CommandLine));
      }
    } else {
      // FIXME: There are two use cases here: doing a fuzzy
      // "find . -name '*.cc' |xargs tool" match, where as a user I don't care
      // about the .cc files that were not found, and the use case where I
      // specify all files I want to run over explicitly, where this should
      // be an error. We'll want to add an option for this.
      llvm::outs() << "Skipping " << File << ". Command line not found.\n";
    }
  }
}

void ClangTool::mapVirtualFile(StringRef FilePath, StringRef Content) {
  MappedFileContents.push_back(std::make_pair(FilePath, Content));
}

int ClangTool::run(FrontendActionFactory *ActionFactory) {
  bool ProcessingFailed = false;
  for (unsigned I = 0; I < CommandLines.size(); ++I) {
    std::string File = CommandLines[I].first;
    std::vector<std::string> &CommandLine = CommandLines[I].second;
    llvm::outs() << "Processing: " << File << ".\n";
    ToolInvocation Invocation(CommandLine, ActionFactory->create(), &Files);
    for (int I = 0, E = MappedFileContents.size(); I != E; ++I) {
      Invocation.mapVirtualFile(MappedFileContents[I].first,
                                MappedFileContents[I].second);
    }
    if (!Invocation.run()) {
      llvm::outs() << "Error while processing " << File << ".\n";
      ProcessingFailed = true;
    }
  }
  return ProcessingFailed ? 1 : 0;
}

} // end namespace tooling
} // end namespace clang
