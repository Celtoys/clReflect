//===--- PlistDiagnostics.cpp - Plist Diagnostics for Paths -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the PlistDiagnostics object.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathDiagnosticConsumers.h"
#include "clang/StaticAnalyzer/Core/BugReporter/PathDiagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/FileManager.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Casting.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
using namespace clang;
using namespace ento;

typedef llvm::DenseMap<FileID, unsigned> FIDMap;


namespace {
  class PlistDiagnostics : public PathDiagnosticConsumer {
    const std::string OutputFile;
    const LangOptions &LangOpts;
    OwningPtr<PathDiagnosticConsumer> SubPD;
    bool flushed;
    const bool SupportsCrossFileDiagnostics;
  public:
    PlistDiagnostics(const std::string& prefix, const LangOptions &LangOpts,
                     bool supportsMultipleFiles,
                     PathDiagnosticConsumer *subPD);

    virtual ~PlistDiagnostics() {}

    void FlushDiagnosticsImpl(std::vector<const PathDiagnostic *> &Diags,
                              SmallVectorImpl<std::string> *FilesMade);
    
    virtual StringRef getName() const {
      return "PlistDiagnostics";
    }

    PathGenerationScheme getGenerationScheme() const;
    bool supportsLogicalOpControlFlow() const { return true; }
    bool supportsAllBlockEdges() const { return true; }
    virtual bool useVerboseDescription() const { return false; }
    virtual bool supportsCrossFileDiagnostics() const {
      return SupportsCrossFileDiagnostics;
    }
  };
} // end anonymous namespace

PlistDiagnostics::PlistDiagnostics(const std::string& output,
                                   const LangOptions &LO,
                                   bool supportsMultipleFiles,
                                   PathDiagnosticConsumer *subPD)
  : OutputFile(output), LangOpts(LO), SubPD(subPD), flushed(false),
    SupportsCrossFileDiagnostics(supportsMultipleFiles) {}

PathDiagnosticConsumer*
ento::createPlistDiagnosticConsumer(const std::string& s, const Preprocessor &PP,
                                  PathDiagnosticConsumer *subPD) {
  return new PlistDiagnostics(s, PP.getLangOpts(), false, subPD);
}

PathDiagnosticConsumer*
ento::createPlistMultiFileDiagnosticConsumer(const std::string &s,
                                              const Preprocessor &PP) {
  return new PlistDiagnostics(s, PP.getLangOpts(), true, 0);
}

PathDiagnosticConsumer::PathGenerationScheme
PlistDiagnostics::getGenerationScheme() const {
  if (const PathDiagnosticConsumer *PD = SubPD.get())
    return PD->getGenerationScheme();

  return Extensive;
}

static void AddFID(FIDMap &FIDs, SmallVectorImpl<FileID> &V,
                   const SourceManager* SM, SourceLocation L) {

  FileID FID = SM->getFileID(SM->getExpansionLoc(L));
  FIDMap::iterator I = FIDs.find(FID);
  if (I != FIDs.end()) return;
  FIDs[FID] = V.size();
  V.push_back(FID);
}

static unsigned GetFID(const FIDMap& FIDs, const SourceManager &SM,
                       SourceLocation L) {
  FileID FID = SM.getFileID(SM.getExpansionLoc(L));
  FIDMap::const_iterator I = FIDs.find(FID);
  assert(I != FIDs.end());
  return I->second;
}

static raw_ostream &Indent(raw_ostream &o, const unsigned indent) {
  for (unsigned i = 0; i < indent; ++i) o << ' ';
  return o;
}

static void EmitLocation(raw_ostream &o, const SourceManager &SM,
                         const LangOptions &LangOpts,
                         SourceLocation L, const FIDMap &FM,
                         unsigned indent, bool extend = false) {

  FullSourceLoc Loc(SM.getExpansionLoc(L), const_cast<SourceManager&>(SM));

  // Add in the length of the token, so that we cover multi-char tokens.
  unsigned offset =
    extend ? Lexer::MeasureTokenLength(Loc, SM, LangOpts) - 1 : 0;

  Indent(o, indent) << "<dict>\n";
  Indent(o, indent) << " <key>line</key><integer>"
                    << Loc.getExpansionLineNumber() << "</integer>\n";
  Indent(o, indent) << " <key>col</key><integer>"
                    << Loc.getExpansionColumnNumber() + offset << "</integer>\n";
  Indent(o, indent) << " <key>file</key><integer>"
                    << GetFID(FM, SM, Loc) << "</integer>\n";
  Indent(o, indent) << "</dict>\n";
}

static void EmitLocation(raw_ostream &o, const SourceManager &SM,
                         const LangOptions &LangOpts,
                         const PathDiagnosticLocation &L, const FIDMap& FM,
                         unsigned indent, bool extend = false) {
  EmitLocation(o, SM, LangOpts, L.asLocation(), FM, indent, extend);
}

static void EmitRange(raw_ostream &o, const SourceManager &SM,
                      const LangOptions &LangOpts,
                      PathDiagnosticRange R, const FIDMap &FM,
                      unsigned indent) {
  Indent(o, indent) << "<array>\n";
  EmitLocation(o, SM, LangOpts, R.getBegin(), FM, indent+1);
  EmitLocation(o, SM, LangOpts, R.getEnd(), FM, indent+1, !R.isPoint);
  Indent(o, indent) << "</array>\n";
}

static raw_ostream &EmitString(raw_ostream &o, StringRef s) {
  o << "<string>";
  for (StringRef::const_iterator I = s.begin(), E = s.end(); I != E; ++I) {
    char c = *I;
    switch (c) {
    default:   o << c; break;
    case '&':  o << "&amp;"; break;
    case '<':  o << "&lt;"; break;
    case '>':  o << "&gt;"; break;
    case '\'': o << "&apos;"; break;
    case '\"': o << "&quot;"; break;
    }
  }
  o << "</string>";
  return o;
}

static void ReportControlFlow(raw_ostream &o,
                              const PathDiagnosticControlFlowPiece& P,
                              const FIDMap& FM,
                              const SourceManager &SM,
                              const LangOptions &LangOpts,
                              unsigned indent) {

  Indent(o, indent) << "<dict>\n";
  ++indent;

  Indent(o, indent) << "<key>kind</key><string>control</string>\n";

  // Emit edges.
  Indent(o, indent) << "<key>edges</key>\n";
  ++indent;
  Indent(o, indent) << "<array>\n";
  ++indent;
  for (PathDiagnosticControlFlowPiece::const_iterator I=P.begin(), E=P.end();
       I!=E; ++I) {
    Indent(o, indent) << "<dict>\n";
    ++indent;
    Indent(o, indent) << "<key>start</key>\n";
    EmitRange(o, SM, LangOpts, I->getStart().asRange(), FM, indent+1);
    Indent(o, indent) << "<key>end</key>\n";
    EmitRange(o, SM, LangOpts, I->getEnd().asRange(), FM, indent+1);
    --indent;
    Indent(o, indent) << "</dict>\n";
  }
  --indent;
  Indent(o, indent) << "</array>\n";
  --indent;

  // Output any helper text.
  const std::string& s = P.getString();
  if (!s.empty()) {
    Indent(o, indent) << "<key>alternate</key>";
    EmitString(o, s) << '\n';
  }

  --indent;
  Indent(o, indent) << "</dict>\n";
}

static void ReportEvent(raw_ostream &o, const PathDiagnosticPiece& P,
                        const FIDMap& FM,
                        const SourceManager &SM,
                        const LangOptions &LangOpts,
                        unsigned indent,
                        unsigned depth) {

  Indent(o, indent) << "<dict>\n";
  ++indent;

  Indent(o, indent) << "<key>kind</key><string>event</string>\n";

  // Output the location.
  FullSourceLoc L = P.getLocation().asLocation();

  Indent(o, indent) << "<key>location</key>\n";
  EmitLocation(o, SM, LangOpts, L, FM, indent);

  // Output the ranges (if any).
  PathDiagnosticPiece::range_iterator RI = P.ranges_begin(),
  RE = P.ranges_end();

  if (RI != RE) {
    Indent(o, indent) << "<key>ranges</key>\n";
    Indent(o, indent) << "<array>\n";
    ++indent;
    for (; RI != RE; ++RI)
      EmitRange(o, SM, LangOpts, *RI, FM, indent+1);
    --indent;
    Indent(o, indent) << "</array>\n";
  }
  
  // Output the call depth.
  Indent(o, indent) << "<key>depth</key>"
                    << "<integer>" << depth << "</integer>\n";

  // Output the text.
  assert(!P.getString().empty());
  Indent(o, indent) << "<key>extended_message</key>\n";
  Indent(o, indent);
  EmitString(o, P.getString()) << '\n';

  // Output the short text.
  // FIXME: Really use a short string.
  Indent(o, indent) << "<key>message</key>\n";
  EmitString(o, P.getString()) << '\n';
  
  // Finish up.
  --indent;
  Indent(o, indent); o << "</dict>\n";
}

static void ReportPiece(raw_ostream &o,
                        const PathDiagnosticPiece &P,
                        const FIDMap& FM, const SourceManager &SM,
                        const LangOptions &LangOpts,
                        unsigned indent,
                        unsigned depth,
                        bool includeControlFlow);

static void ReportCall(raw_ostream &o,
                       const PathDiagnosticCallPiece &P,
                       const FIDMap& FM, const SourceManager &SM,
                       const LangOptions &LangOpts,
                       unsigned indent,
                       unsigned depth) {
  
  IntrusiveRefCntPtr<PathDiagnosticEventPiece> callEnter =
    P.getCallEnterEvent();  

  if (callEnter)
    ReportPiece(o, *callEnter, FM, SM, LangOpts, indent, depth, true);

  IntrusiveRefCntPtr<PathDiagnosticEventPiece> callEnterWithinCaller =
    P.getCallEnterWithinCallerEvent();
  
  ++depth;
  
  if (callEnterWithinCaller)
    ReportPiece(o, *callEnterWithinCaller, FM, SM, LangOpts,
                indent, depth, true);
  
  for (PathPieces::const_iterator I = P.path.begin(), E = P.path.end();I!=E;++I)
    ReportPiece(o, **I, FM, SM, LangOpts, indent, depth, true);
  
  IntrusiveRefCntPtr<PathDiagnosticEventPiece> callExit =
    P.getCallExitEvent();

  if (callExit)
    ReportPiece(o, *callExit, FM, SM, LangOpts, indent, depth, true);
}

static void ReportMacro(raw_ostream &o,
                        const PathDiagnosticMacroPiece& P,
                        const FIDMap& FM, const SourceManager &SM,
                        const LangOptions &LangOpts,
                        unsigned indent,
                        unsigned depth) {

  for (PathPieces::const_iterator I = P.subPieces.begin(), E=P.subPieces.end();
       I!=E; ++I) {
    ReportPiece(o, **I, FM, SM, LangOpts, indent, depth, false);
  }
}

static void ReportDiag(raw_ostream &o, const PathDiagnosticPiece& P,
                       const FIDMap& FM, const SourceManager &SM,
                       const LangOptions &LangOpts) {
  ReportPiece(o, P, FM, SM, LangOpts, 4, 0, true);
}

static void ReportPiece(raw_ostream &o,
                        const PathDiagnosticPiece &P,
                        const FIDMap& FM, const SourceManager &SM,
                        const LangOptions &LangOpts,
                        unsigned indent,
                        unsigned depth,
                        bool includeControlFlow) {
  switch (P.getKind()) {
    case PathDiagnosticPiece::ControlFlow:
      if (includeControlFlow)
        ReportControlFlow(o, cast<PathDiagnosticControlFlowPiece>(P), FM, SM,
                          LangOpts, indent);
      break;
    case PathDiagnosticPiece::Call:
      ReportCall(o, cast<PathDiagnosticCallPiece>(P), FM, SM, LangOpts,
                 indent, depth);
      break;
    case PathDiagnosticPiece::Event:
      ReportEvent(o, cast<PathDiagnosticSpotPiece>(P), FM, SM, LangOpts,
                  indent, depth);
      break;
    case PathDiagnosticPiece::Macro:
      ReportMacro(o, cast<PathDiagnosticMacroPiece>(P), FM, SM, LangOpts,
                  indent, depth);
      break;
  }
}

void PlistDiagnostics::FlushDiagnosticsImpl(
                                    std::vector<const PathDiagnostic *> &Diags,
                                    SmallVectorImpl<std::string> *FilesMade) {
  // Build up a set of FIDs that we use by scanning the locations and
  // ranges of the diagnostics.
  FIDMap FM;
  SmallVector<FileID, 10> Fids;
  const SourceManager* SM = 0;

  if (!Diags.empty())
    SM = &(*(*Diags.begin())->path.begin())->getLocation().getManager();

  
  for (std::vector<const PathDiagnostic*>::iterator DI = Diags.begin(),
       DE = Diags.end(); DI != DE; ++DI) {

    const PathDiagnostic *D = *DI;

    llvm::SmallVector<const PathPieces *, 5> WorkList;
    WorkList.push_back(&D->path);

    while (!WorkList.empty()) {
      const PathPieces &path = *WorkList.back();
      WorkList.pop_back();
    
      for (PathPieces::const_iterator I = path.begin(), E = path.end();
           I!=E; ++I) {
        const PathDiagnosticPiece *piece = I->getPtr();
        AddFID(FM, Fids, SM, piece->getLocation().asLocation());

        for (PathDiagnosticPiece::range_iterator RI = piece->ranges_begin(),
             RE= piece->ranges_end(); RI != RE; ++RI) {
          AddFID(FM, Fids, SM, RI->getBegin());
          AddFID(FM, Fids, SM, RI->getEnd());
        }

        if (const PathDiagnosticCallPiece *call =
            dyn_cast<PathDiagnosticCallPiece>(piece)) {
          WorkList.push_back(&call->path);
        }
        else if (const PathDiagnosticMacroPiece *macro =
                 dyn_cast<PathDiagnosticMacroPiece>(piece)) {
          WorkList.push_back(&macro->subPieces);
        }
      }
    }
  }

  // Open the file.
  std::string ErrMsg;
  llvm::raw_fd_ostream o(OutputFile.c_str(), ErrMsg);
  if (!ErrMsg.empty()) {
    llvm::errs() << "warning: could not create file: " << OutputFile << '\n';
    return;
  }

  // Write the plist header.
  o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
  "<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" "
  "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
  "<plist version=\"1.0\">\n";

  // Write the root object: a <dict> containing...
  //  - "files", an <array> mapping from FIDs to file names
  //  - "diagnostics", an <array> containing the path diagnostics
  o << "<dict>\n"
       " <key>files</key>\n"
       " <array>\n";

  for (SmallVectorImpl<FileID>::iterator I=Fids.begin(), E=Fids.end();
       I!=E; ++I) {
    o << "  ";
    EmitString(o, SM->getFileEntryForID(*I)->getName()) << '\n';
  }

  o << " </array>\n"
       " <key>diagnostics</key>\n"
       " <array>\n";

  for (std::vector<const PathDiagnostic*>::iterator DI=Diags.begin(),
       DE = Diags.end(); DI!=DE; ++DI) {

    o << "  <dict>\n"
         "   <key>path</key>\n";

    const PathDiagnostic *D = *DI;

    o << "   <array>\n";

    for (PathPieces::const_iterator I = D->path.begin(), E = D->path.end(); 
         I != E; ++I)
      ReportDiag(o, **I, FM, *SM, LangOpts);

    o << "   </array>\n";

    // Output the bug type and bug category.
    o << "   <key>description</key>";
    EmitString(o, D->getDescription()) << '\n';
    o << "   <key>category</key>";
    EmitString(o, D->getCategory()) << '\n';
    o << "   <key>type</key>";
    EmitString(o, D->getBugType()) << '\n';
    
    // Output information about the semantic context where
    // the issue occurred.
    if (const Decl *DeclWithIssue = D->getDeclWithIssue()) {
      // FIXME: handle blocks, which have no name.
      if (const NamedDecl *ND = dyn_cast<NamedDecl>(DeclWithIssue)) {
        StringRef declKind;
        switch (ND->getKind()) {
          case Decl::CXXRecord:
            declKind = "C++ class";
            break;
          case Decl::CXXMethod:
            declKind = "C++ method";
            break;
          case Decl::ObjCMethod:
            declKind = "Objective-C method";
            break;
          case Decl::Function:
            declKind = "function";
            break;
          default:
            break;
        }
        if (!declKind.empty()) {
          const std::string &declName = ND->getDeclName().getAsString();
          o << "  <key>issue_context_kind</key>";
          EmitString(o, declKind) << '\n';
          o << "  <key>issue_context</key>";
          EmitString(o, declName) << '\n';
        }
      }
    }

    // Output the location of the bug.
    o << "  <key>location</key>\n";
    EmitLocation(o, *SM, LangOpts, D->getLocation(), FM, 2);

    // Output the diagnostic to the sub-diagnostic client, if any.
    if (SubPD) {
      std::vector<const PathDiagnostic *> SubDiags;
      SubDiags.push_back(D);
      SmallVector<std::string, 1> SubFilesMade;
      SubPD->FlushDiagnosticsImpl(SubDiags, &SubFilesMade);

      if (!SubFilesMade.empty()) {
        o << "  <key>" << SubPD->getName() << "_files</key>\n";
        o << "  <array>\n";
        for (size_t i = 0, n = SubFilesMade.size(); i < n ; ++i)
          o << "   <string>" << SubFilesMade[i] << "</string>\n";
        o << "  </array>\n";
      }
    }

    // Close up the entry.
    o << "  </dict>\n";
  }

  o << " </array>\n";

  // Finish.
  o << "</dict>\n</plist>";
  
  if (FilesMade)
    FilesMade->push_back(OutputFile);
}
