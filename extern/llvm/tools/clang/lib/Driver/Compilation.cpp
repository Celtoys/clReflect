//===--- Compilation.cpp - Compilation Task Implementation ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Compilation.h"

#include "clang/Driver/Action.h"
#include "clang/Driver/ArgList.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/ToolChain.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Program.h"
#include <sys/stat.h>
#include <errno.h>

using namespace clang::driver;
using namespace clang;

Compilation::Compilation(const Driver &D, const ToolChain &_DefaultToolChain,
                         InputArgList *_Args, DerivedArgList *_TranslatedArgs)
  : TheDriver(D), DefaultToolChain(_DefaultToolChain), Args(_Args),
    TranslatedArgs(_TranslatedArgs), Redirects(0) {
}

Compilation::~Compilation() {
  delete TranslatedArgs;
  delete Args;

  // Free any derived arg lists.
  for (llvm::DenseMap<std::pair<const ToolChain*, const char*>,
                      DerivedArgList*>::iterator it = TCArgs.begin(),
         ie = TCArgs.end(); it != ie; ++it)
    if (it->second != TranslatedArgs)
      delete it->second;

  // Free the actions, if built.
  for (ActionList::iterator it = Actions.begin(), ie = Actions.end();
       it != ie; ++it)
    delete *it;

  // Free redirections of stdout/stderr.
  if (Redirects) {
    delete Redirects[1];
    delete Redirects[2];
    delete [] Redirects;
  }
}

const DerivedArgList &Compilation::getArgsForToolChain(const ToolChain *TC,
                                                       const char *BoundArch) {
  if (!TC)
    TC = &DefaultToolChain;

  DerivedArgList *&Entry = TCArgs[std::make_pair(TC, BoundArch)];
  if (!Entry) {
    Entry = TC->TranslateArgs(*TranslatedArgs, BoundArch);
    if (!Entry)
      Entry = TranslatedArgs;
  }

  return *Entry;
}

void Compilation::PrintJob(raw_ostream &OS, const Job &J,
                           const char *Terminator, bool Quote) const {
  if (const Command *C = dyn_cast<Command>(&J)) {
    OS << " \"" << C->getExecutable() << '"';
    for (ArgStringList::const_iterator it = C->getArguments().begin(),
           ie = C->getArguments().end(); it != ie; ++it) {
      OS << ' ';
      if (!Quote && !std::strpbrk(*it, " \"\\$")) {
        OS << *it;
        continue;
      }

      // Quote the argument and escape shell special characters; this isn't
      // really complete but is good enough.
      OS << '"';
      for (const char *s = *it; *s; ++s) {
        if (*s == '"' || *s == '\\' || *s == '$')
          OS << '\\';
        OS << *s;
      }
      OS << '"';
    }
    OS << Terminator;
  } else {
    const JobList *Jobs = cast<JobList>(&J);
    for (JobList::const_iterator
           it = Jobs->begin(), ie = Jobs->end(); it != ie; ++it)
      PrintJob(OS, **it, Terminator, Quote);
  }
}

bool Compilation::CleanupFileList(const ArgStringList &Files,
                                  bool IssueErrors) const {
  bool Success = true;

  for (ArgStringList::const_iterator
         it = Files.begin(), ie = Files.end(); it != ie; ++it) {

    llvm::sys::Path P(*it);
    std::string Error;

    // Don't try to remove files which we don't have write access to (but may be
    // able to remove). Underlying tools may have intentionally not overwritten
    // them.
    if (!P.canWrite())
      continue;

    if (P.eraseFromDisk(false, &Error)) {
      // Failure is only failure if the file exists and is "regular". There is
      // a race condition here due to the limited interface of
      // llvm::sys::Path, we want to know if the removal gave ENOENT.

      // FIXME: Grumble, P.exists() is broken. PR3837.
      struct stat buf;
      if (::stat(P.c_str(), &buf) == 0 ? (buf.st_mode & S_IFMT) == S_IFREG :
                                         (errno != ENOENT)) {
        if (IssueErrors)
          getDriver().Diag(clang::diag::err_drv_unable_to_remove_file)
            << Error;
        Success = false;
      }
    }
  }

  return Success;
}

int Compilation::ExecuteCommand(const Command &C,
                                const Command *&FailingCommand) const {
  llvm::sys::Path Prog(C.getExecutable());
  const char **Argv = new const char*[C.getArguments().size() + 2];
  Argv[0] = C.getExecutable();
  std::copy(C.getArguments().begin(), C.getArguments().end(), Argv+1);
  Argv[C.getArguments().size() + 1] = 0;

  if ((getDriver().CCCEcho || getDriver().CCPrintOptions ||
       getArgs().hasArg(options::OPT_v)) && !getDriver().CCGenDiagnostics) {
    raw_ostream *OS = &llvm::errs();

    // Follow gcc implementation of CC_PRINT_OPTIONS; we could also cache the
    // output stream.
    if (getDriver().CCPrintOptions && getDriver().CCPrintOptionsFilename) {
      std::string Error;
      OS = new llvm::raw_fd_ostream(getDriver().CCPrintOptionsFilename,
                                    Error,
                                    llvm::raw_fd_ostream::F_Append);
      if (!Error.empty()) {
        getDriver().Diag(clang::diag::err_drv_cc_print_options_failure)
          << Error;
        FailingCommand = &C;
        delete OS;
        return 1;
      }
    }

    if (getDriver().CCPrintOptions)
      *OS << "[Logging clang options]";

    PrintJob(*OS, C, "\n", /*Quote=*/getDriver().CCPrintOptions);

    if (OS != &llvm::errs())
      delete OS;
  }

  std::string Error;
  int Res =
    llvm::sys::Program::ExecuteAndWait(Prog, Argv,
                                       /*env*/0, Redirects,
                                       /*secondsToWait*/0, /*memoryLimit*/0,
                                       &Error);
  if (!Error.empty()) {
    assert(Res && "Error string set with 0 result code!");
    getDriver().Diag(clang::diag::err_drv_command_failure) << Error;
  }

  if (Res)
    FailingCommand = &C;

  delete[] Argv;
  return Res;
}

int Compilation::ExecuteJob(const Job &J,
                            const Command *&FailingCommand) const {
  if (const Command *C = dyn_cast<Command>(&J)) {
    return ExecuteCommand(*C, FailingCommand);
  } else {
    const JobList *Jobs = cast<JobList>(&J);
    for (JobList::const_iterator
           it = Jobs->begin(), ie = Jobs->end(); it != ie; ++it)
      if (int Res = ExecuteJob(**it, FailingCommand))
        return Res;
    return 0;
  }
}

void Compilation::initCompilationForDiagnostics(void) {
  // Free actions and jobs.
  DeleteContainerPointers(Actions);
  Jobs.clear();

  // Clear temporary/results file lists.
  TempFiles.clear();
  ResultFiles.clear();

  // Remove any user specified output.  Claim any unclaimed arguments, so as
  // to avoid emitting warnings about unused args.
  if (TranslatedArgs->hasArg(options::OPT_o))
    TranslatedArgs->eraseArg(options::OPT_o);
  TranslatedArgs->ClaimAllArgs();

  // Redirect stdout/stderr to /dev/null.
  Redirects = new const llvm::sys::Path*[3]();
  Redirects[1] = new const llvm::sys::Path();
  Redirects[2] = new const llvm::sys::Path();
}
