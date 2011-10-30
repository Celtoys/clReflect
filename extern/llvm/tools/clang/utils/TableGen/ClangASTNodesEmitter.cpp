//=== ClangASTNodesEmitter.cpp - Generate Clang AST node tables -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// These tablegen backends emit Clang AST node tables
//
//===----------------------------------------------------------------------===//

#include "ClangASTNodesEmitter.h"
#include <set>
using namespace llvm;

//===----------------------------------------------------------------------===//
// Statement Node Tables (.inc file) generation.
//===----------------------------------------------------------------------===//

// Returns the first and last non-abstract subrecords
// Called recursively to ensure that nodes remain contiguous
std::pair<Record *, Record *> ClangASTNodesEmitter::EmitNode(
                                                           const ChildMap &Tree,
                                                           raw_ostream &OS,
                                                           Record *Base) {
  std::string BaseName = macroName(Base->getName());

  ChildIterator i = Tree.lower_bound(Base), e = Tree.upper_bound(Base);

  Record *First = 0, *Last = 0;
  // This might be the pseudo-node for Stmt; don't assume it has an Abstract
  // bit
  if (Base->getValue("Abstract") && !Base->getValueAsBit("Abstract"))
    First = Last = Base;

  for (; i != e; ++i) {
    Record *R = i->second;
    bool Abstract = R->getValueAsBit("Abstract");
    std::string NodeName = macroName(R->getName());

    OS << "#ifndef " << NodeName << "\n";
    OS << "#  define " << NodeName << "(Type, Base) "
        << BaseName << "(Type, Base)\n";
    OS << "#endif\n";

    if (Abstract)
      OS << "ABSTRACT_" << macroName(Root.getName()) << "(" << NodeName << "("
          << R->getName() << ", " << baseName(*Base) << "))\n";
    else
      OS << NodeName << "(" << R->getName() << ", "
          << baseName(*Base) << ")\n";

    if (Tree.find(R) != Tree.end()) {
      const std::pair<Record *, Record *> &Result
        = EmitNode(Tree, OS, R);
      if (!First && Result.first)
        First = Result.first;
      if (Result.second)
        Last = Result.second;
    } else {
      if (!Abstract) {
        Last = R;

        if (!First)
          First = R;
      }
    }

    OS << "#undef " << NodeName << "\n\n";
  }

  if (First) {
    assert (Last && "Got a first node but not a last node for a range!");
    if (Base == &Root)
      OS << "LAST_" << macroName(Root.getName()) << "_RANGE(";
    else
      OS << macroName(Root.getName()) << "_RANGE(";
    OS << Base->getName() << ", " << First->getName() << ", "
       << Last->getName() << ")\n\n";
  }

  return std::make_pair(First, Last);
}

void ClangASTNodesEmitter::run(raw_ostream &OS) {
  // Write the preamble
  OS << "#ifndef ABSTRACT_" << macroName(Root.getName()) << "\n";
  OS << "#  define ABSTRACT_" << macroName(Root.getName()) << "(Type) Type\n";
  OS << "#endif\n";

  OS << "#ifndef " << macroName(Root.getName()) << "_RANGE\n";
  OS << "#  define "
     << macroName(Root.getName()) << "_RANGE(Base, First, Last)\n";
  OS << "#endif\n\n";

  OS << "#ifndef LAST_" << macroName(Root.getName()) << "_RANGE\n";
  OS << "#  define LAST_" 
     << macroName(Root.getName()) << "_RANGE(Base, First, Last) " 
     << macroName(Root.getName()) << "_RANGE(Base, First, Last)\n";
  OS << "#endif\n\n";
 
  // Emit statements
  const std::vector<Record*> Stmts
    = Records.getAllDerivedDefinitions(Root.getName());

  ChildMap Tree;

  for (unsigned i = 0, e = Stmts.size(); i != e; ++i) {
    Record *R = Stmts[i];

    if (R->getValue("Base"))
      Tree.insert(std::make_pair(R->getValueAsDef("Base"), R));
    else
      Tree.insert(std::make_pair(&Root, R));
  }

  EmitNode(Tree, OS, &Root);

  OS << "#undef " << macroName(Root.getName()) << "\n";
  OS << "#undef " << macroName(Root.getName()) << "_RANGE\n";
  OS << "#undef LAST_" << macroName(Root.getName()) << "_RANGE\n";
  OS << "#undef ABSTRACT_" << macroName(Root.getName()) << "\n";
}

void ClangDeclContextEmitter::run(raw_ostream &OS) {
  // FIXME: Find a .td file format to allow for this to be represented better.

  OS << "#ifndef DECL_CONTEXT\n";
  OS << "#  define DECL_CONTEXT(DECL)\n";
  OS << "#endif\n";
  
  OS << "#ifndef DECL_CONTEXT_BASE\n";
  OS << "#  define DECL_CONTEXT_BASE(DECL) DECL_CONTEXT(DECL)\n";
  OS << "#endif\n";
  
  typedef std::set<Record*> RecordSet;
  typedef std::vector<Record*> RecordVector;
  
  RecordVector DeclContextsVector
    = Records.getAllDerivedDefinitions("DeclContext");
  RecordVector Decls = Records.getAllDerivedDefinitions("Decl");
  RecordSet DeclContexts (DeclContextsVector.begin(), DeclContextsVector.end());
   
  for (RecordVector::iterator i = Decls.begin(), e = Decls.end(); i != e; ++i) {
    Record *R = *i;

    if (R->getValue("Base")) {
      Record *B = R->getValueAsDef("Base");
      if (DeclContexts.find(B) != DeclContexts.end()) {
        OS << "DECL_CONTEXT_BASE(" << B->getName() << ")\n";
        DeclContexts.erase(B);
      }
    }
  }

  // To keep identical order, RecordVector may be used
  // instead of RecordSet.
  for (RecordVector::iterator
         i = DeclContextsVector.begin(), e = DeclContextsVector.end();
       i != e; ++i)
    if (DeclContexts.find(*i) != DeclContexts.end())
      OS << "DECL_CONTEXT(" << (*i)->getName() << ")\n";

  OS << "#undef DECL_CONTEXT\n";
  OS << "#undef DECL_CONTEXT_BASE\n";
}
