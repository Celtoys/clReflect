//===- ListWarnings.h - diagtool tool for printing warning flags ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides a diagtool tool that displays warning flags for
// diagnostics.
//
//===----------------------------------------------------------------------===//

#include "DiagTool.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/Support/Format.h"
#include "llvm/ADT/StringMap.h"
#include "clang/AST/ASTDiagnostic.h"
#include "clang/Basic/AllDiagnostics.h"

DEF_DIAGTOOL("list-warnings",
             "List warnings and their corresponding flags",
             ListWarnings)
  
using namespace clang;

namespace {
struct StaticDiagNameIndexRec {
  const char *NameStr;
  unsigned short DiagID;
  uint8_t NameLen;

  StringRef getName() const {
    return StringRef(NameStr, NameLen);
  }
};
}

static const StaticDiagNameIndexRec StaticDiagNameIndex[] = {
#define DIAG_NAME_INDEX(ENUM) { #ENUM, diag::ENUM, STR_SIZE(#ENUM, uint8_t) },
#include "clang/Basic/DiagnosticIndexName.inc"
#undef DIAG_NAME_INDEX
  { 0, 0, 0 }
};

static const unsigned StaticDiagNameIndexSize =
  sizeof(StaticDiagNameIndex)/sizeof(StaticDiagNameIndex[0])-1;

namespace {
struct Entry {
  llvm::StringRef DiagName;
  llvm::StringRef Flag;
  
  Entry(llvm::StringRef diagN, llvm::StringRef flag)
    : DiagName(diagN), Flag(flag) {}
  
  bool operator<(const Entry &x) const { return DiagName < x.DiagName; }
};
}

static void printEntries(std::vector<Entry> &entries, llvm::raw_ostream &out) {
  for (std::vector<Entry>::iterator it = entries.begin(), ei = entries.end();
       it != ei; ++it) {
    out << "  " << it->DiagName;
    if (!it->Flag.empty())
      out << " [-W" << it->Flag << "]";
    out << '\n';
  }
}

int ListWarnings::run(unsigned int argc, char **argv, llvm::raw_ostream &out) {
  std::vector<Entry> Flagged, Unflagged;
  llvm::StringMap<std::vector<unsigned> > flagHistogram;
  
  for (const StaticDiagNameIndexRec *di = StaticDiagNameIndex, *de = StaticDiagNameIndex + StaticDiagNameIndexSize;
       di != de; ++di) {
    
    unsigned diagID = di->DiagID;
    
    if (DiagnosticIDs::isBuiltinNote(diagID))
      continue;
        
    if (!DiagnosticIDs::isBuiltinWarningOrExtension(diagID))
      continue;
  
    Entry entry(di->getName(),
                DiagnosticIDs::getWarningOptionForDiag(diagID));
    
    if (entry.Flag.empty())
      Unflagged.push_back(entry);
    else {
      Flagged.push_back(entry);
      flagHistogram.GetOrCreateValue(entry.Flag).getValue().push_back(diagID);
    }
  }
  
  std::sort(Flagged.begin(), Flagged.end());
  std::sort(Unflagged.begin(), Unflagged.end());

  out << "Warnings with flags (" << Flagged.size() << "):\n";
  printEntries(Flagged, out);
  
  out << "Warnings without flags (" << Unflagged.size() << "):\n";
  printEntries(Unflagged, out);

  out << "\nSTATISTICS:\n\n";

  double percentFlagged = ((double) Flagged.size()) 
    / (Flagged.size() + Unflagged.size()) * 100.0;
  
  out << "  Percentage of warnings with flags: " 
      << llvm::format("%.4g",percentFlagged) << "%\n";
  
  out << "  Number of unique flags: "
      << flagHistogram.size() << '\n';
  
  double avgDiagsPerFlag = (double) Flagged.size() / flagHistogram.size();
  out << "  Average number of diagnostics per flag: "
      << llvm::format("%.4g", avgDiagsPerFlag) << '\n';
    
  out << '\n';
  
  return 0;
}

