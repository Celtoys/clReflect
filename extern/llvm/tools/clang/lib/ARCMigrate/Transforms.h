//===-- Transforms.h - Tranformations to ARC mode ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_ARCMIGRATE_TRANSFORMS_H
#define LLVM_CLANG_LIB_ARCMIGRATE_TRANSFORMS_H

#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/ParentMap.h"
#include "llvm/ADT/DenseSet.h"

namespace clang {
  class Decl;
  class Stmt;
  class BlockDecl;
  class ObjCMethodDecl;
  class FunctionDecl;

namespace arcmt {
  class MigrationPass;

namespace trans {

  class MigrationContext;

//===----------------------------------------------------------------------===//
// Transformations.
//===----------------------------------------------------------------------===//

void rewriteAutoreleasePool(MigrationPass &pass);
void rewriteUnbridgedCasts(MigrationPass &pass);
void makeAssignARCSafe(MigrationPass &pass);
void removeRetainReleaseDeallocFinalize(MigrationPass &pass);
void removeZeroOutPropsInDeallocFinalize(MigrationPass &pass);
void rewriteUnusedInitDelegate(MigrationPass &pass);
void checkAPIUses(MigrationPass &pass);

void removeEmptyStatementsAndDeallocFinalize(MigrationPass &pass);

class BodyContext {
  MigrationContext &MigrateCtx;
  ParentMap PMap;
  Stmt *TopStmt;

public:
  BodyContext(MigrationContext &MigrateCtx, Stmt *S)
    : MigrateCtx(MigrateCtx), PMap(S), TopStmt(S) {}

  MigrationContext &getMigrationContext() { return MigrateCtx; }
  ParentMap &getParentMap() { return PMap; }
  Stmt *getTopStmt() { return TopStmt; }
};

class ObjCImplementationContext {
  MigrationContext &MigrateCtx;
  ObjCImplementationDecl *ImpD;

public:
  ObjCImplementationContext(MigrationContext &MigrateCtx,
                            ObjCImplementationDecl *D)
    : MigrateCtx(MigrateCtx), ImpD(D) {}

  MigrationContext &getMigrationContext() { return MigrateCtx; }
  ObjCImplementationDecl *getImplementationDecl() { return ImpD; }
};

class ASTTraverser {
public:
  virtual ~ASTTraverser();
  virtual void traverseTU(MigrationContext &MigrateCtx) { }
  virtual void traverseBody(BodyContext &BodyCtx) { }
  virtual void traverseObjCImplementation(ObjCImplementationContext &ImplCtx) {}
};

class MigrationContext {
  std::vector<ASTTraverser *> Traversers;

public:
  MigrationPass &Pass;

  struct GCAttrOccurrence {
    enum AttrKind { Weak, Strong } Kind;
    SourceLocation Loc;
    QualType ModifiedType;
    Decl *Dcl;
    /// \brief true if the attribute is owned, e.g. it is in a body and not just
    /// in an interface.
    bool FullyMigratable;
  };
  std::vector<GCAttrOccurrence> GCAttrs;
  llvm::DenseSet<unsigned> AttrSet;
  llvm::DenseSet<unsigned> RemovedAttrSet;

  /// \brief Set of raw '@' locations for 'assign' properties group that contain
  /// GC __weak.
  llvm::DenseSet<unsigned> AtPropsWeak;

  explicit MigrationContext(MigrationPass &pass) : Pass(pass) {}
  ~MigrationContext();
  
  typedef std::vector<ASTTraverser *>::iterator traverser_iterator;
  traverser_iterator traversers_begin() { return Traversers.begin(); }
  traverser_iterator traversers_end() { return Traversers.end(); }

  void addTraverser(ASTTraverser *traverser) {
    Traversers.push_back(traverser);
  }

  bool isGCOwnedNonObjC(QualType T);
  bool removePropertyAttribute(StringRef fromAttr, SourceLocation atLoc) {
    return rewritePropertyAttribute(fromAttr, StringRef(), atLoc);
  }
  bool rewritePropertyAttribute(StringRef fromAttr, StringRef toAttr,
                                SourceLocation atLoc);
  bool addPropertyAttribute(StringRef attr, SourceLocation atLoc);

  void traverse(TranslationUnitDecl *TU);

  void dumpGCAttrs();
};

class PropertyRewriteTraverser : public ASTTraverser {
public:
  virtual void traverseObjCImplementation(ObjCImplementationContext &ImplCtx);
};

class BlockObjCVariableTraverser : public ASTTraverser {
public:
  virtual void traverseBody(BodyContext &BodyCtx);
};

// GC transformations

class GCAttrsTraverser : public ASTTraverser {
public:
  virtual void traverseTU(MigrationContext &MigrateCtx);
};

class GCCollectableCallsTraverser : public ASTTraverser {
public:
  virtual void traverseBody(BodyContext &BodyCtx);
};

//===----------------------------------------------------------------------===//
// Helpers.
//===----------------------------------------------------------------------===//

/// \brief Determine whether we can add weak to the given type.
bool canApplyWeak(ASTContext &Ctx, QualType type,
                  bool AllowOnUnknownClass = false);

/// \brief 'Loc' is the end of a statement range. This returns the location
/// immediately after the semicolon following the statement.
/// If no semicolon is found or the location is inside a macro, the returned
/// source location will be invalid.
SourceLocation findLocationAfterSemi(SourceLocation loc, ASTContext &Ctx);

/// \brief \arg Loc is the end of a statement range. This returns the location
/// of the semicolon following the statement.
/// If no semicolon is found or the location is inside a macro, the returned
/// source location will be invalid.
SourceLocation findSemiAfterLocation(SourceLocation loc, ASTContext &Ctx);

bool hasSideEffects(Expr *E, ASTContext &Ctx);
bool isGlobalVar(Expr *E);
/// \brief Returns "nil" or "0" if 'nil' macro is not actually defined.
StringRef getNilString(ASTContext &Ctx);

template <typename BODY_TRANS>
class BodyTransform : public RecursiveASTVisitor<BodyTransform<BODY_TRANS> > {
  MigrationPass &Pass;

public:
  BodyTransform(MigrationPass &pass) : Pass(pass) { }

  bool TraverseStmt(Stmt *rootS) {
    if (rootS)
      BODY_TRANS(Pass).transformBody(rootS);
    return true;
  }
};

typedef llvm::DenseSet<Expr *> ExprSet;

void clearRefsIn(Stmt *S, ExprSet &refs);
template <typename iterator>
void clearRefsIn(iterator begin, iterator end, ExprSet &refs) {
  for (; begin != end; ++begin)
    clearRefsIn(*begin, refs);
}

void collectRefs(ValueDecl *D, Stmt *S, ExprSet &refs);

void collectRemovables(Stmt *S, ExprSet &exprs);

} // end namespace trans

} // end namespace arcmt

} // end namespace clang

#endif
