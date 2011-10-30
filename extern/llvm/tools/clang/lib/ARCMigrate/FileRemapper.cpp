//===--- FileRemapper.cpp - File Remapping Helper -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/ARCMigrate/FileRemapper.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Basic/FileManager.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>

using namespace clang;
using namespace arcmt;

FileRemapper::FileRemapper() {
  FileMgr.reset(new FileManager(FileSystemOptions()));
}

FileRemapper::~FileRemapper() {
  clear();
}

void FileRemapper::clear(StringRef outputDir) {
  for (MappingsTy::iterator
         I = FromToMappings.begin(), E = FromToMappings.end(); I != E; ++I)
    resetTarget(I->second);
  FromToMappings.clear();
  assert(ToFromMappings.empty());
  if (!outputDir.empty()) {
    std::string infoFile = getRemapInfoFile(outputDir);
    bool existed;
    llvm::sys::fs::remove(infoFile, existed);
  }
}

std::string FileRemapper::getRemapInfoFile(StringRef outputDir) {
  assert(!outputDir.empty());
  llvm::sys::Path dir(outputDir);
  llvm::sys::Path infoFile = dir;
  infoFile.appendComponent("remap");
  return infoFile.str();
}

bool FileRemapper::initFromDisk(StringRef outputDir, DiagnosticsEngine &Diag,
                                bool ignoreIfFilesChanged) {
  assert(FromToMappings.empty() &&
         "initFromDisk should be called before any remap calls");
  std::string infoFile = getRemapInfoFile(outputDir);
  bool fileExists = false;
  llvm::sys::fs::exists(infoFile, fileExists);
  if (!fileExists)
    return false;

  std::vector<std::pair<const FileEntry *, const FileEntry *> > pairs;
  
  llvm::OwningPtr<llvm::MemoryBuffer> fileBuf;
  if (llvm::error_code ec = llvm::MemoryBuffer::getFile(infoFile.c_str(),
                                                        fileBuf))
    return report("Error opening file: " + infoFile, Diag);
  
  SmallVector<StringRef, 64> lines;
  fileBuf->getBuffer().split(lines, "\n");

  for (unsigned idx = 0; idx+3 <= lines.size(); idx += 3) {
    StringRef fromFilename = lines[idx];
    unsigned long long timeModified;
    lines[idx+1].getAsInteger(10, timeModified);
    StringRef toFilename = lines[idx+2];
    
    const FileEntry *origFE = FileMgr->getFile(fromFilename);
    if (!origFE) {
      if (ignoreIfFilesChanged)
        continue;
      return report("File does not exist: " + fromFilename, Diag);
    }
    const FileEntry *newFE = FileMgr->getFile(toFilename);
    if (!newFE) {
      if (ignoreIfFilesChanged)
        continue;
      return report("File does not exist: " + toFilename, Diag);
    }

    if ((uint64_t)origFE->getModificationTime() != timeModified) {
      if (ignoreIfFilesChanged)
        continue;
      return report("File was modified: " + fromFilename, Diag);
    }

    pairs.push_back(std::make_pair(origFE, newFE));
  }

  for (unsigned i = 0, e = pairs.size(); i != e; ++i)
    remap(pairs[i].first, pairs[i].second);

  return false;
}

bool FileRemapper::flushToDisk(StringRef outputDir, DiagnosticsEngine &Diag) {
  using namespace llvm::sys;

  bool existed;
  if (fs::create_directory(outputDir, existed) != llvm::errc::success)
    return report("Could not create directory: " + outputDir, Diag);

  std::string errMsg;
  std::string infoFile = getRemapInfoFile(outputDir);
  llvm::raw_fd_ostream infoOut(infoFile.c_str(), errMsg,
                               llvm::raw_fd_ostream::F_Binary);
  if (!errMsg.empty())
    return report(errMsg, Diag);

  for (MappingsTy::iterator
         I = FromToMappings.begin(), E = FromToMappings.end(); I != E; ++I) {

    const FileEntry *origFE = I->first;
    llvm::SmallString<200> origPath = StringRef(origFE->getName());
    fs::make_absolute(origPath);
    infoOut << origPath << '\n';
    infoOut << (uint64_t)origFE->getModificationTime() << '\n';

    if (const FileEntry *FE = I->second.dyn_cast<const FileEntry *>()) {
      llvm::SmallString<200> newPath = StringRef(FE->getName());
      fs::make_absolute(newPath);
      infoOut << newPath << '\n';
    } else {

      llvm::SmallString<64> tempPath;
      tempPath = path::filename(origFE->getName());
      tempPath += "-%%%%%%%%";
      tempPath += path::extension(origFE->getName());
      int fd;
      if (fs::unique_file(tempPath.str(), fd, tempPath) != llvm::errc::success)
        return report("Could not create file: " + tempPath.str(), Diag);

      llvm::raw_fd_ostream newOut(fd, /*shouldClose=*/true);
      llvm::MemoryBuffer *mem = I->second.get<llvm::MemoryBuffer *>();
      newOut.write(mem->getBufferStart(), mem->getBufferSize());
      newOut.close();
      
      const FileEntry *newE = FileMgr->getFile(tempPath);
      remap(origFE, newE);
      infoOut << newE->getName() << '\n';
    }
  }

  infoOut.close();
  return false;
}

bool FileRemapper::overwriteOriginal(DiagnosticsEngine &Diag,
                                     StringRef outputDir) {
  using namespace llvm::sys;

  for (MappingsTy::iterator
         I = FromToMappings.begin(), E = FromToMappings.end(); I != E; ++I) {
    const FileEntry *origFE = I->first;
    if (const FileEntry *newFE = I->second.dyn_cast<const FileEntry *>()) {
      if (fs::copy_file(newFE->getName(), origFE->getName(),
                 fs::copy_option::overwrite_if_exists) != llvm::errc::success)
        return report(StringRef("Could not copy file '") + newFE->getName() +
                      "' to file '" + origFE->getName() + "'", Diag);
    } else {

      bool fileExists = false;
      fs::exists(origFE->getName(), fileExists);
      if (!fileExists)
        return report(StringRef("File does not exist: ") + origFE->getName(),
                      Diag);

      std::string errMsg;
      llvm::raw_fd_ostream Out(origFE->getName(), errMsg,
                               llvm::raw_fd_ostream::F_Binary);
      if (!errMsg.empty())
        return report(errMsg, Diag);

      llvm::MemoryBuffer *mem = I->second.get<llvm::MemoryBuffer *>();
      Out.write(mem->getBufferStart(), mem->getBufferSize());
      Out.close();
    }
  }

  clear(outputDir);
  return false;
}

void FileRemapper::applyMappings(CompilerInvocation &CI) const {
  PreprocessorOptions &PPOpts = CI.getPreprocessorOpts();
  for (MappingsTy::const_iterator
         I = FromToMappings.begin(), E = FromToMappings.end(); I != E; ++I) {
    if (const FileEntry *FE = I->second.dyn_cast<const FileEntry *>()) {
      PPOpts.addRemappedFile(I->first->getName(), FE->getName());
    } else {
      llvm::MemoryBuffer *mem = I->second.get<llvm::MemoryBuffer *>();
      PPOpts.addRemappedFile(I->first->getName(), mem);
    }
  }

  PPOpts.RetainRemappedFileBuffers = true;
}

void FileRemapper::transferMappingsAndClear(CompilerInvocation &CI) {
  PreprocessorOptions &PPOpts = CI.getPreprocessorOpts();
  for (MappingsTy::iterator
         I = FromToMappings.begin(), E = FromToMappings.end(); I != E; ++I) {
    if (const FileEntry *FE = I->second.dyn_cast<const FileEntry *>()) {
      PPOpts.addRemappedFile(I->first->getName(), FE->getName());
    } else {
      llvm::MemoryBuffer *mem = I->second.get<llvm::MemoryBuffer *>();
      PPOpts.addRemappedFile(I->first->getName(), mem);
    }
    I->second = Target();
  }

  PPOpts.RetainRemappedFileBuffers = false;
  clear();
}

void FileRemapper::remap(StringRef filePath, llvm::MemoryBuffer *memBuf) {
  remap(getOriginalFile(filePath), memBuf);
}

void FileRemapper::remap(StringRef filePath, StringRef newPath) {
  const FileEntry *file = getOriginalFile(filePath);
  const FileEntry *newfile = FileMgr->getFile(newPath);
  remap(file, newfile);
}

void FileRemapper::remap(const FileEntry *file, llvm::MemoryBuffer *memBuf) {
  assert(file);
  Target &targ = FromToMappings[file];
  resetTarget(targ);
  targ = memBuf;
}

void FileRemapper::remap(const FileEntry *file, const FileEntry *newfile) {
  assert(file && newfile);
  Target &targ = FromToMappings[file];
  resetTarget(targ);
  targ = newfile;
  ToFromMappings[newfile] = file;
}

const FileEntry *FileRemapper::getOriginalFile(StringRef filePath) {
  const FileEntry *file = FileMgr->getFile(filePath);
  // If we are updating a file that overriden an original file,
  // actually update the original file.
  llvm::DenseMap<const FileEntry *, const FileEntry *>::iterator
    I = ToFromMappings.find(file);
  if (I != ToFromMappings.end()) {
    file = I->second;
    assert(FromToMappings.find(file) != FromToMappings.end() &&
           "Original file not in mappings!");
  }
  return file;
}

void FileRemapper::resetTarget(Target &targ) {
  if (!targ)
    return;

  if (llvm::MemoryBuffer *oldmem = targ.dyn_cast<llvm::MemoryBuffer *>()) {
    delete oldmem;
  } else {
    const FileEntry *toFE = targ.get<const FileEntry *>();
    llvm::DenseMap<const FileEntry *, const FileEntry *>::iterator
      I = ToFromMappings.find(toFE);
    if (I != ToFromMappings.end())
      ToFromMappings.erase(I);
  }
}

bool FileRemapper::report(const Twine &err, DiagnosticsEngine &Diag) {
  llvm::SmallString<128> buf;
  unsigned ID = Diag.getDiagnosticIDs()->getCustomDiagID(DiagnosticIDs::Error,
                                                         err.toStringRef(buf));
  Diag.Report(ID);
  return true;
}
