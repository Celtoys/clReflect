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

DEF_DIAGTOOL("list-warnings",
             "List warnings and their corresponding flags",
             ListWarnings)
  
using namespace clang;


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
  llvm::IntrusiveRefCntPtr<DiagnosticIDs> Diags(new DiagnosticIDs);
  DiagnosticsEngine D(Diags);
  
  std::vector<Entry> Flagged, Unflagged;
  llvm::StringMap<std::vector<unsigned> > flagHistogram;
  
  for (DiagnosticIDs::diag_iterator di = DiagnosticIDs::diags_begin(),
       de = DiagnosticIDs::diags_end(); di != de; ++di) {
    
    unsigned diagID = di.getDiagID();
    
    if (DiagnosticIDs::isBuiltinNote(diagID))
      continue;
        
    if (!DiagnosticIDs::isBuiltinWarningOrExtension(diagID))
      continue;
  
    Entry entry(di.getDiagName(),
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

