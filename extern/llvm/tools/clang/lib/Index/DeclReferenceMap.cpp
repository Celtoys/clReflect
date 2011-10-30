//===--- DeclReferenceMap.cpp - Map Decls to their references -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  DeclReferenceMap creates a mapping from Decls to the ASTLocations that
//  reference them.
//
//===----------------------------------------------------------------------===//

#include "clang/Index/DeclReferenceMap.h"
#include "clang/Index/ASTLocation.h"
#include "ASTVisitor.h"
using namespace clang;
using namespace idx;

namespace {

class RefMapper : public ASTVisitor<RefMapper> {
  DeclReferenceMap::MapTy &Map;

public:
  RefMapper(DeclReferenceMap::MapTy &map) : Map(map) { }

  void VisitDeclRefExpr(DeclRefExpr *Node);
  void VisitMemberExpr(MemberExpr *Node);
  void VisitObjCIvarRefExpr(ObjCIvarRefExpr *Node);
  
  void VisitTypedefTypeLoc(TypedefTypeLoc TL);
  void VisitObjCInterfaceTypeLoc(ObjCInterfaceTypeLoc TL);
};

} // anonymous namespace

//===----------------------------------------------------------------------===//
// RefMapper Implementation
//===----------------------------------------------------------------------===//

void RefMapper::VisitDeclRefExpr(DeclRefExpr *Node) {
  NamedDecl *PrimD = cast<NamedDecl>(Node->getDecl()->getCanonicalDecl());
  Map.insert(std::make_pair(PrimD, ASTLocation(CurrentDecl, Node)));
}

void RefMapper::VisitMemberExpr(MemberExpr *Node) {
  NamedDecl *PrimD = cast<NamedDecl>(Node->getMemberDecl()->getCanonicalDecl());
  Map.insert(std::make_pair(PrimD, ASTLocation(CurrentDecl, Node)));
}

void RefMapper::VisitObjCIvarRefExpr(ObjCIvarRefExpr *Node) {
  Map.insert(std::make_pair(Node->getDecl(), ASTLocation(CurrentDecl, Node)));
}

void RefMapper::VisitTypedefTypeLoc(TypedefTypeLoc TL) {
  NamedDecl *ND = TL.getTypedefNameDecl();
  Map.insert(std::make_pair(ND, ASTLocation(CurrentDecl, ND, TL.getNameLoc())));
}

void RefMapper::VisitObjCInterfaceTypeLoc(ObjCInterfaceTypeLoc TL) {
  NamedDecl *ND = TL.getIFaceDecl();
  Map.insert(std::make_pair(ND, ASTLocation(CurrentDecl, ND, TL.getNameLoc())));
}

//===----------------------------------------------------------------------===//
// DeclReferenceMap Implementation
//===----------------------------------------------------------------------===//

DeclReferenceMap::DeclReferenceMap(ASTContext &Ctx) {
  RefMapper(Map).Visit(Ctx.getTranslationUnitDecl());
}

DeclReferenceMap::astlocation_iterator
DeclReferenceMap::refs_begin(NamedDecl *D) const {
  NamedDecl *Prim = cast<NamedDecl>(D->getCanonicalDecl());
  return astlocation_iterator(Map.lower_bound(Prim));
}

DeclReferenceMap::astlocation_iterator
DeclReferenceMap::refs_end(NamedDecl *D) const {
  NamedDecl *Prim = cast<NamedDecl>(D->getCanonicalDecl());
  return astlocation_iterator(Map.upper_bound(Prim));
}

bool DeclReferenceMap::refs_empty(NamedDecl *D) const {
  NamedDecl *Prim = cast<NamedDecl>(D->getCanonicalDecl());
  return refs_begin(Prim) == refs_end(Prim);
}
