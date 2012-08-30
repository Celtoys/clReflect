//== MemRegion.h - Abstract memory regions for static analysis --*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines MemRegion and its subclasses.  MemRegion defines a
//  partially-typed abstraction of memory useful for path-sensitive dataflow
//  analyses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_GR_MEMREGION_H
#define LLVM_CLANG_GR_MEMREGION_H

#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/ExprObjC.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/ADT/FoldingSet.h"
#include <string>

namespace llvm {
class BumpPtrAllocator;
}

namespace clang {

class LocationContext;
class StackFrameContext;

namespace ento {

class MemRegionManager;
class MemSpaceRegion;
class SValBuilder;
class VarRegion;
class CodeTextRegion;

/// Represent a region's offset within the top level base region.
class RegionOffset {
  /// The base region.
  const MemRegion *R;

  /// The bit offset within the base region. It shouldn't be negative.
  int64_t Offset;

public:
  RegionOffset(const MemRegion *r) : R(r), Offset(0) {}
  RegionOffset(const MemRegion *r, int64_t off) : R(r), Offset(off) {}

  const MemRegion *getRegion() const { return R; }
  int64_t getOffset() const { return Offset; }
};

//===----------------------------------------------------------------------===//
// Base region classes.
//===----------------------------------------------------------------------===//

/// MemRegion - The root abstract class for all memory regions.
class MemRegion : public llvm::FoldingSetNode {
  friend class MemRegionManager;
public:
  enum Kind {
    // Memory spaces.
    GenericMemSpaceRegionKind,
    StackLocalsSpaceRegionKind,
    StackArgumentsSpaceRegionKind,
    HeapSpaceRegionKind,
    UnknownSpaceRegionKind,
    StaticGlobalSpaceRegionKind,
    GlobalInternalSpaceRegionKind,
    GlobalSystemSpaceRegionKind,
    GlobalImmutableSpaceRegionKind,
    BEG_NON_STATIC_GLOBAL_MEMSPACES = GlobalInternalSpaceRegionKind,
    END_NON_STATIC_GLOBAL_MEMSPACES = GlobalImmutableSpaceRegionKind,
    BEG_GLOBAL_MEMSPACES = StaticGlobalSpaceRegionKind,
    END_GLOBAL_MEMSPACES = GlobalImmutableSpaceRegionKind,
    BEG_MEMSPACES = GenericMemSpaceRegionKind,
    END_MEMSPACES = GlobalImmutableSpaceRegionKind,
    // Untyped regions.
    SymbolicRegionKind,
    AllocaRegionKind,
    BlockDataRegionKind,
    // Typed regions.
    BEG_TYPED_REGIONS,
    FunctionTextRegionKind = BEG_TYPED_REGIONS,
    BlockTextRegionKind,
    BEG_TYPED_VALUE_REGIONS,
    CompoundLiteralRegionKind = BEG_TYPED_VALUE_REGIONS,
    CXXThisRegionKind,
    StringRegionKind,
    ObjCStringRegionKind,
    ElementRegionKind,
    // Decl Regions.
    BEG_DECL_REGIONS,
    VarRegionKind = BEG_DECL_REGIONS,
    FieldRegionKind,
    ObjCIvarRegionKind,
    END_DECL_REGIONS = ObjCIvarRegionKind,
    CXXTempObjectRegionKind,
    CXXBaseObjectRegionKind,
    END_TYPED_VALUE_REGIONS = CXXBaseObjectRegionKind,
    END_TYPED_REGIONS = CXXBaseObjectRegionKind
  };
    
private:
  const Kind kind;

protected:
  MemRegion(Kind k) : kind(k) {}
  virtual ~MemRegion();

public:
  ASTContext &getContext() const;

  virtual void Profile(llvm::FoldingSetNodeID& ID) const = 0;

  virtual MemRegionManager* getMemRegionManager() const = 0;

  const MemSpaceRegion *getMemorySpace() const;

  const MemRegion *getBaseRegion() const;

  const MemRegion *StripCasts() const;

  bool hasGlobalsOrParametersStorage() const;

  bool hasStackStorage() const;
  
  bool hasStackNonParametersStorage() const;
  
  bool hasStackParametersStorage() const;

  /// Compute the offset within the top level memory object.
  RegionOffset getAsOffset() const;

  /// \brief Get a string representation of a region for debug use.
  std::string getString() const;

  virtual void dumpToStream(raw_ostream &os) const;

  void dump() const;

  /// \brief Print the region for use in diagnostics.
  virtual void dumpPretty(raw_ostream &os) const;

  Kind getKind() const { return kind; }

  template<typename RegionTy> const RegionTy* getAs() const;

  virtual bool isBoundable() const { return false; }

  static bool classof(const MemRegion*) { return true; }
};

/// MemSpaceRegion - A memory region that represents a "memory space";
///  for example, the set of global variables, the stack frame, etc.
class MemSpaceRegion : public MemRegion {
protected:
  friend class MemRegionManager;
  
  MemRegionManager *Mgr;

  MemSpaceRegion(MemRegionManager *mgr, Kind k = GenericMemSpaceRegionKind)
    : MemRegion(k), Mgr(mgr) {
    assert(classof(this));
  }

  MemRegionManager* getMemRegionManager() const { return Mgr; }

public:
  bool isBoundable() const { return false; }
  
  void Profile(llvm::FoldingSetNodeID &ID) const;

  static bool classof(const MemRegion *R) {
    Kind k = R->getKind();
    return k >= BEG_MEMSPACES && k <= END_MEMSPACES;
  }
};
  
class GlobalsSpaceRegion : public MemSpaceRegion {
  virtual void anchor();
protected:
  GlobalsSpaceRegion(MemRegionManager *mgr, Kind k)
    : MemSpaceRegion(mgr, k) {}
public:
  static bool classof(const MemRegion *R) {
    Kind k = R->getKind();
    return k >= BEG_GLOBAL_MEMSPACES && k <= END_GLOBAL_MEMSPACES;
  }
};

/// \class The region of the static variables within the current CodeTextRegion
/// scope.
/// Currently, only the static locals are placed there, so we know that these
/// variables do not get invalidated by calls to other functions.
class StaticGlobalSpaceRegion : public GlobalsSpaceRegion {
  friend class MemRegionManager;

  const CodeTextRegion *CR;
  
  StaticGlobalSpaceRegion(MemRegionManager *mgr, const CodeTextRegion *cr)
    : GlobalsSpaceRegion(mgr, StaticGlobalSpaceRegionKind), CR(cr) {}

public:
  void Profile(llvm::FoldingSetNodeID &ID) const;
  
  void dumpToStream(raw_ostream &os) const;

  const CodeTextRegion *getCodeRegion() const { return CR; }

  static bool classof(const MemRegion *R) {
    return R->getKind() == StaticGlobalSpaceRegionKind;
  }
};

/// \class The region for all the non-static global variables.
///
/// This class is further split into subclasses for efficient implementation of
/// invalidating a set of related global values as is done in
/// RegionStoreManager::invalidateRegions (instead of finding all the dependent
/// globals, we invalidate the whole parent region).
class NonStaticGlobalSpaceRegion : public GlobalsSpaceRegion {
  friend class MemRegionManager;
  
protected:
  NonStaticGlobalSpaceRegion(MemRegionManager *mgr, Kind k)
    : GlobalsSpaceRegion(mgr, k) {}
  
public:

  void dumpToStream(raw_ostream &os) const;

  static bool classof(const MemRegion *R) {
    Kind k = R->getKind();
    return k >= BEG_NON_STATIC_GLOBAL_MEMSPACES &&
           k <= END_NON_STATIC_GLOBAL_MEMSPACES;
  }
};

/// \class The region containing globals which are defined in system/external
/// headers and are considered modifiable by system calls (ex: errno).
class GlobalSystemSpaceRegion : public NonStaticGlobalSpaceRegion {
  friend class MemRegionManager;

  GlobalSystemSpaceRegion(MemRegionManager *mgr)
    : NonStaticGlobalSpaceRegion(mgr, GlobalSystemSpaceRegionKind) {}

public:

  void dumpToStream(raw_ostream &os) const;

  static bool classof(const MemRegion *R) {
    return R->getKind() == GlobalSystemSpaceRegionKind;
  }
};

/// \class The region containing globals which are considered not to be modified
/// or point to data which could be modified as a result of a function call
/// (system or internal). Ex: Const global scalars would be modeled as part of
/// this region. This region also includes most system globals since they have
/// low chance of being modified.
class GlobalImmutableSpaceRegion : public NonStaticGlobalSpaceRegion {
  friend class MemRegionManager;

  GlobalImmutableSpaceRegion(MemRegionManager *mgr)
    : NonStaticGlobalSpaceRegion(mgr, GlobalImmutableSpaceRegionKind) {}

public:

  void dumpToStream(raw_ostream &os) const;

  static bool classof(const MemRegion *R) {
    return R->getKind() == GlobalImmutableSpaceRegionKind;
  }
};

/// \class The region containing globals which can be modified by calls to
/// "internally" defined functions - (for now just) functions other then system
/// calls.
class GlobalInternalSpaceRegion : public NonStaticGlobalSpaceRegion {
  friend class MemRegionManager;

  GlobalInternalSpaceRegion(MemRegionManager *mgr)
    : NonStaticGlobalSpaceRegion(mgr, GlobalInternalSpaceRegionKind) {}

public:

  void dumpToStream(raw_ostream &os) const;

  static bool classof(const MemRegion *R) {
    return R->getKind() == GlobalInternalSpaceRegionKind;
  }
};

class HeapSpaceRegion : public MemSpaceRegion {
  virtual void anchor();
  friend class MemRegionManager;
  
  HeapSpaceRegion(MemRegionManager *mgr)
    : MemSpaceRegion(mgr, HeapSpaceRegionKind) {}
public:
  static bool classof(const MemRegion *R) {
    return R->getKind() == HeapSpaceRegionKind;
  }
};
  
class UnknownSpaceRegion : public MemSpaceRegion {
  virtual void anchor();
  friend class MemRegionManager;
  UnknownSpaceRegion(MemRegionManager *mgr)
    : MemSpaceRegion(mgr, UnknownSpaceRegionKind) {}
public:
  static bool classof(const MemRegion *R) {
    return R->getKind() == UnknownSpaceRegionKind;
  }
};
  
class StackSpaceRegion : public MemSpaceRegion {
private:
  const StackFrameContext *SFC;

protected:
  StackSpaceRegion(MemRegionManager *mgr, Kind k, const StackFrameContext *sfc)
    : MemSpaceRegion(mgr, k), SFC(sfc) {
    assert(classof(this));
  }

public:  
  const StackFrameContext *getStackFrame() const { return SFC; }
  
  void Profile(llvm::FoldingSetNodeID &ID) const;

  static bool classof(const MemRegion *R) {
    Kind k = R->getKind();
    return k >= StackLocalsSpaceRegionKind &&
           k <= StackArgumentsSpaceRegionKind;
  }  
};
  
class StackLocalsSpaceRegion : public StackSpaceRegion {
  virtual void anchor();
  friend class MemRegionManager;
  StackLocalsSpaceRegion(MemRegionManager *mgr, const StackFrameContext *sfc)
    : StackSpaceRegion(mgr, StackLocalsSpaceRegionKind, sfc) {}
public:
  static bool classof(const MemRegion *R) {
    return R->getKind() == StackLocalsSpaceRegionKind;
  }
};

class StackArgumentsSpaceRegion : public StackSpaceRegion {
private:
  virtual void anchor();
  friend class MemRegionManager;
  StackArgumentsSpaceRegion(MemRegionManager *mgr, const StackFrameContext *sfc)
    : StackSpaceRegion(mgr, StackArgumentsSpaceRegionKind, sfc) {}
public:
  static bool classof(const MemRegion *R) {
    return R->getKind() == StackArgumentsSpaceRegionKind;
  }
};


/// SubRegion - A region that subsets another larger region.  Most regions
///  are subclasses of SubRegion.
class SubRegion : public MemRegion {
private:
  virtual void anchor();
protected:
  const MemRegion* superRegion;
  SubRegion(const MemRegion* sReg, Kind k) : MemRegion(k), superRegion(sReg) {}
public:
  const MemRegion* getSuperRegion() const {
    return superRegion;
  }

  /// getExtent - Returns the size of the region in bytes.
  virtual DefinedOrUnknownSVal getExtent(SValBuilder &svalBuilder) const {
    return UnknownVal();
  }

  MemRegionManager* getMemRegionManager() const;

  bool isSubRegionOf(const MemRegion* R) const;

  static bool classof(const MemRegion* R) {
    return R->getKind() > END_MEMSPACES;
  }
};

//===----------------------------------------------------------------------===//
// MemRegion subclasses.
//===----------------------------------------------------------------------===//

/// AllocaRegion - A region that represents an untyped blob of bytes created
///  by a call to 'alloca'.
class AllocaRegion : public SubRegion {
  friend class MemRegionManager;
protected:
  unsigned Cnt; // Block counter.  Used to distinguish different pieces of
                // memory allocated by alloca at the same call site.
  const Expr *Ex;

  AllocaRegion(const Expr *ex, unsigned cnt, const MemRegion *superRegion)
    : SubRegion(superRegion, AllocaRegionKind), Cnt(cnt), Ex(ex) {}

public:

  const Expr *getExpr() const { return Ex; }

  bool isBoundable() const { return true; }

  DefinedOrUnknownSVal getExtent(SValBuilder &svalBuilder) const;

  void Profile(llvm::FoldingSetNodeID& ID) const;

  static void ProfileRegion(llvm::FoldingSetNodeID& ID, const Expr *Ex,
                            unsigned Cnt, const MemRegion *superRegion);

  void dumpToStream(raw_ostream &os) const;

  static bool classof(const MemRegion* R) {
    return R->getKind() == AllocaRegionKind;
  }
};

/// TypedRegion - An abstract class representing regions that are typed.
class TypedRegion : public SubRegion {
public:
  virtual void anchor();
protected:
  TypedRegion(const MemRegion* sReg, Kind k) : SubRegion(sReg, k) {}

public:
  virtual QualType getLocationType() const = 0;

  QualType getDesugaredLocationType(ASTContext &Context) const {
    return getLocationType().getDesugaredType(Context);
  }

  bool isBoundable() const { return true; }

  static bool classof(const MemRegion* R) {
    unsigned k = R->getKind();
    return k >= BEG_TYPED_REGIONS && k <= END_TYPED_REGIONS;
  }
};

/// TypedValueRegion - An abstract class representing regions having a typed value.
class TypedValueRegion : public TypedRegion {
public:
  virtual void anchor();
protected:
  TypedValueRegion(const MemRegion* sReg, Kind k) : TypedRegion(sReg, k) {}

public:
  virtual QualType getValueType() const = 0;

  virtual QualType getLocationType() const {
    // FIXME: We can possibly optimize this later to cache this value.
    QualType T = getValueType();
    ASTContext &ctx = getContext();
    if (T->getAs<ObjCObjectType>())
      return ctx.getObjCObjectPointerType(T);
    return ctx.getPointerType(getValueType());
  }

  QualType getDesugaredValueType(ASTContext &Context) const {
    QualType T = getValueType();
    return T.getTypePtrOrNull() ? T.getDesugaredType(Context) : T;
  }

  static bool classof(const MemRegion* R) {
    unsigned k = R->getKind();
    return k >= BEG_TYPED_VALUE_REGIONS && k <= END_TYPED_VALUE_REGIONS;
  }
};


class CodeTextRegion : public TypedRegion {
public:
  virtual void anchor();
protected:
  CodeTextRegion(const MemRegion *sreg, Kind k) : TypedRegion(sreg, k) {}
public:
  bool isBoundable() const { return false; }
    
  static bool classof(const MemRegion* R) {
    Kind k = R->getKind();
    return k >= FunctionTextRegionKind && k <= BlockTextRegionKind;
  }
};

/// FunctionTextRegion - A region that represents code texts of function.
class FunctionTextRegion : public CodeTextRegion {
  const FunctionDecl *FD;
public:
  FunctionTextRegion(const FunctionDecl *fd, const MemRegion* sreg)
    : CodeTextRegion(sreg, FunctionTextRegionKind), FD(fd) {}
  
  QualType getLocationType() const {
    return getContext().getPointerType(FD->getType());
  }
  
  const FunctionDecl *getDecl() const {
    return FD;
  }
    
  virtual void dumpToStream(raw_ostream &os) const;
  
  void Profile(llvm::FoldingSetNodeID& ID) const;
  
  static void ProfileRegion(llvm::FoldingSetNodeID& ID, const FunctionDecl *FD,
                            const MemRegion*);
  
  static bool classof(const MemRegion* R) {
    return R->getKind() == FunctionTextRegionKind;
  }
};
  
  
/// BlockTextRegion - A region that represents code texts of blocks (closures).
///  Blocks are represented with two kinds of regions.  BlockTextRegions
///  represent the "code", while BlockDataRegions represent instances of blocks,
///  which correspond to "code+data".  The distinction is important, because
///  like a closure a block captures the values of externally referenced
///  variables.
class BlockTextRegion : public CodeTextRegion {
  friend class MemRegionManager;

  const BlockDecl *BD;
  AnalysisDeclContext *AC;
  CanQualType locTy;

  BlockTextRegion(const BlockDecl *bd, CanQualType lTy,
                  AnalysisDeclContext *ac, const MemRegion* sreg)
    : CodeTextRegion(sreg, BlockTextRegionKind), BD(bd), AC(ac), locTy(lTy) {}

public:
  QualType getLocationType() const {
    return locTy;
  }
  
  const BlockDecl *getDecl() const {
    return BD;
  }

  AnalysisDeclContext *getAnalysisDeclContext() const { return AC; }
    
  virtual void dumpToStream(raw_ostream &os) const;
  
  void Profile(llvm::FoldingSetNodeID& ID) const;
  
  static void ProfileRegion(llvm::FoldingSetNodeID& ID, const BlockDecl *BD,
                            CanQualType, const AnalysisDeclContext*,
                            const MemRegion*);
  
  static bool classof(const MemRegion* R) {
    return R->getKind() == BlockTextRegionKind;
  }
};
  
/// BlockDataRegion - A region that represents a block instance.
///  Blocks are represented with two kinds of regions.  BlockTextRegions
///  represent the "code", while BlockDataRegions represent instances of blocks,
///  which correspond to "code+data".  The distinction is important, because
///  like a closure a block captures the values of externally referenced
///  variables.
class BlockDataRegion : public SubRegion {
  friend class MemRegionManager;
  const BlockTextRegion *BC;
  const LocationContext *LC; // Can be null */
  void *ReferencedVars;

  BlockDataRegion(const BlockTextRegion *bc, const LocationContext *lc,
                  const MemRegion *sreg)
  : SubRegion(sreg, BlockDataRegionKind), BC(bc), LC(lc), ReferencedVars(0) {}

public:  
  const BlockTextRegion *getCodeRegion() const { return BC; }
  
  const BlockDecl *getDecl() const { return BC->getDecl(); }
  
  class referenced_vars_iterator {
    const MemRegion * const *R;
  public:
    explicit referenced_vars_iterator(const MemRegion * const *r) : R(r) {}
    
    operator const MemRegion * const *() const {
      return R;
    }
    
    const VarRegion* operator*() const {
      return cast<VarRegion>(*R);
    }
    
    bool operator==(const referenced_vars_iterator &I) const {
      return I.R == R;
    }
    bool operator!=(const referenced_vars_iterator &I) const {
      return I.R != R;
    }
    referenced_vars_iterator &operator++() {
      ++R;
      return *this;
    }
  };
      
  referenced_vars_iterator referenced_vars_begin() const;
  referenced_vars_iterator referenced_vars_end() const;  
    
  virtual void dumpToStream(raw_ostream &os) const;
    
  void Profile(llvm::FoldingSetNodeID& ID) const;
    
  static void ProfileRegion(llvm::FoldingSetNodeID&, const BlockTextRegion *,
                            const LocationContext *, const MemRegion *);
    
  static bool classof(const MemRegion* R) {
    return R->getKind() == BlockDataRegionKind;
  }
private:
  void LazyInitializeReferencedVars();
};

/// SymbolicRegion - A special, "non-concrete" region. Unlike other region
///  clases, SymbolicRegion represents a region that serves as an alias for
///  either a real region, a NULL pointer, etc.  It essentially is used to
///  map the concept of symbolic values into the domain of regions.  Symbolic
///  regions do not need to be typed.
class SymbolicRegion : public SubRegion {
protected:
  const SymbolRef sym;

public:
  SymbolicRegion(const SymbolRef s, const MemRegion* sreg)
    : SubRegion(sreg, SymbolicRegionKind), sym(s) {}

  SymbolRef getSymbol() const {
    return sym;
  }

  bool isBoundable() const { return true; }

  DefinedOrUnknownSVal getExtent(SValBuilder &svalBuilder) const;

  void Profile(llvm::FoldingSetNodeID& ID) const;

  static void ProfileRegion(llvm::FoldingSetNodeID& ID,
                            SymbolRef sym,
                            const MemRegion* superRegion);

  void dumpToStream(raw_ostream &os) const;

  static bool classof(const MemRegion* R) {
    return R->getKind() == SymbolicRegionKind;
  }
};

/// StringRegion - Region associated with a StringLiteral.
class StringRegion : public TypedValueRegion {
  friend class MemRegionManager;
  const StringLiteral* Str;
protected:

  StringRegion(const StringLiteral* str, const MemRegion* sreg)
    : TypedValueRegion(sreg, StringRegionKind), Str(str) {}

  static void ProfileRegion(llvm::FoldingSetNodeID& ID,
                            const StringLiteral* Str,
                            const MemRegion* superRegion);

public:

  const StringLiteral* getStringLiteral() const { return Str; }

  QualType getValueType() const {
    return Str->getType();
  }

  DefinedOrUnknownSVal getExtent(SValBuilder &svalBuilder) const;

  bool isBoundable() const { return false; }

  void Profile(llvm::FoldingSetNodeID& ID) const {
    ProfileRegion(ID, Str, superRegion);
  }

  void dumpToStream(raw_ostream &os) const;

  static bool classof(const MemRegion* R) {
    return R->getKind() == StringRegionKind;
  }
};
  
/// The region associated with an ObjCStringLiteral.
class ObjCStringRegion : public TypedValueRegion {
  friend class MemRegionManager;
  const ObjCStringLiteral* Str;
protected:
  
  ObjCStringRegion(const ObjCStringLiteral* str, const MemRegion* sreg)
  : TypedValueRegion(sreg, ObjCStringRegionKind), Str(str) {}
  
  static void ProfileRegion(llvm::FoldingSetNodeID& ID,
                            const ObjCStringLiteral* Str,
                            const MemRegion* superRegion);
  
public:
  
  const ObjCStringLiteral* getObjCStringLiteral() const { return Str; }
  
  QualType getValueType() const {
    return Str->getType();
  }
  
  bool isBoundable() const { return false; }
  
  void Profile(llvm::FoldingSetNodeID& ID) const {
    ProfileRegion(ID, Str, superRegion);
  }
  
  void dumpToStream(raw_ostream &os) const;
  
  static bool classof(const MemRegion* R) {
    return R->getKind() == ObjCStringRegionKind;
  }
};

/// CompoundLiteralRegion - A memory region representing a compound literal.
///   Compound literals are essentially temporaries that are stack allocated
///   or in the global constant pool.
class CompoundLiteralRegion : public TypedValueRegion {
private:
  friend class MemRegionManager;
  const CompoundLiteralExpr *CL;

  CompoundLiteralRegion(const CompoundLiteralExpr *cl, const MemRegion* sReg)
    : TypedValueRegion(sReg, CompoundLiteralRegionKind), CL(cl) {}

  static void ProfileRegion(llvm::FoldingSetNodeID& ID,
                            const CompoundLiteralExpr *CL,
                            const MemRegion* superRegion);
public:
  QualType getValueType() const {
    return CL->getType();
  }

  bool isBoundable() const { return !CL->isFileScope(); }

  void Profile(llvm::FoldingSetNodeID& ID) const;

  void dumpToStream(raw_ostream &os) const;

  const CompoundLiteralExpr *getLiteralExpr() const { return CL; }

  static bool classof(const MemRegion* R) {
    return R->getKind() == CompoundLiteralRegionKind;
  }
};

class DeclRegion : public TypedValueRegion {
protected:
  const Decl *D;

  DeclRegion(const Decl *d, const MemRegion* sReg, Kind k)
    : TypedValueRegion(sReg, k), D(d) {}

  static void ProfileRegion(llvm::FoldingSetNodeID& ID, const Decl *D,
                      const MemRegion* superRegion, Kind k);

public:
  const Decl *getDecl() const { return D; }
  void Profile(llvm::FoldingSetNodeID& ID) const;

  DefinedOrUnknownSVal getExtent(SValBuilder &svalBuilder) const;

  static bool classof(const MemRegion* R) {
    unsigned k = R->getKind();
    return k >= BEG_DECL_REGIONS && k <= END_DECL_REGIONS;
  }
};

class VarRegion : public DeclRegion {
  friend class MemRegionManager;

  // Constructors and private methods.
  VarRegion(const VarDecl *vd, const MemRegion* sReg)
    : DeclRegion(vd, sReg, VarRegionKind) {}

  static void ProfileRegion(llvm::FoldingSetNodeID& ID, const VarDecl *VD,
                            const MemRegion *superRegion) {
    DeclRegion::ProfileRegion(ID, VD, superRegion, VarRegionKind);
  }

  void Profile(llvm::FoldingSetNodeID& ID) const;

public:
  const VarDecl *getDecl() const { return cast<VarDecl>(D); }

  const StackFrameContext *getStackFrame() const;
  
  QualType getValueType() const {
    // FIXME: We can cache this if needed.
    return getDecl()->getType();
  }

  void dumpToStream(raw_ostream &os) const;

  static bool classof(const MemRegion* R) {
    return R->getKind() == VarRegionKind;
  }

  void dumpPretty(raw_ostream &os) const;
};
  
/// CXXThisRegion - Represents the region for the implicit 'this' parameter
///  in a call to a C++ method.  This region doesn't represent the object
///  referred to by 'this', but rather 'this' itself.
class CXXThisRegion : public TypedValueRegion {
  friend class MemRegionManager;
  CXXThisRegion(const PointerType *thisPointerTy,
                const MemRegion *sReg)
    : TypedValueRegion(sReg, CXXThisRegionKind), ThisPointerTy(thisPointerTy) {}

  static void ProfileRegion(llvm::FoldingSetNodeID &ID,
                            const PointerType *PT,
                            const MemRegion *sReg);

  void Profile(llvm::FoldingSetNodeID &ID) const;

public:  
  QualType getValueType() const {
    return QualType(ThisPointerTy, 0);
  }

  void dumpToStream(raw_ostream &os) const;
  
  static bool classof(const MemRegion* R) {
    return R->getKind() == CXXThisRegionKind;
  }

private:
  const PointerType *ThisPointerTy;
};

class FieldRegion : public DeclRegion {
  friend class MemRegionManager;

  FieldRegion(const FieldDecl *fd, const MemRegion* sReg)
    : DeclRegion(fd, sReg, FieldRegionKind) {}

public:
  const FieldDecl *getDecl() const { return cast<FieldDecl>(D); }

  QualType getValueType() const {
    // FIXME: We can cache this if needed.
    return getDecl()->getType();
  }

  DefinedOrUnknownSVal getExtent(SValBuilder &svalBuilder) const;

  static void ProfileRegion(llvm::FoldingSetNodeID& ID, const FieldDecl *FD,
                            const MemRegion* superRegion) {
    DeclRegion::ProfileRegion(ID, FD, superRegion, FieldRegionKind);
  }

  static bool classof(const MemRegion* R) {
    return R->getKind() == FieldRegionKind;
  }

  void dumpToStream(raw_ostream &os) const;
  void dumpPretty(raw_ostream &os) const;
};

class ObjCIvarRegion : public DeclRegion {

  friend class MemRegionManager;

  ObjCIvarRegion(const ObjCIvarDecl *ivd, const MemRegion* sReg);

  static void ProfileRegion(llvm::FoldingSetNodeID& ID, const ObjCIvarDecl *ivd,
                            const MemRegion* superRegion);

public:
  const ObjCIvarDecl *getDecl() const;
  QualType getValueType() const;

  void dumpToStream(raw_ostream &os) const;

  static bool classof(const MemRegion* R) {
    return R->getKind() == ObjCIvarRegionKind;
  }
};
//===----------------------------------------------------------------------===//
// Auxiliary data classes for use with MemRegions.
//===----------------------------------------------------------------------===//

class ElementRegion;

class RegionRawOffset {
private:
  friend class ElementRegion;

  const MemRegion *Region;
  CharUnits Offset;

  RegionRawOffset(const MemRegion* reg, CharUnits offset = CharUnits::Zero())
    : Region(reg), Offset(offset) {}

public:
  // FIXME: Eventually support symbolic offsets.
  CharUnits getOffset() const { return Offset; }
  const MemRegion *getRegion() const { return Region; }

  void dumpToStream(raw_ostream &os) const;
  void dump() const;
};

/// \brief ElementRegin is used to represent both array elements and casts.
class ElementRegion : public TypedValueRegion {
  friend class MemRegionManager;

  QualType ElementType;
  NonLoc Index;

  ElementRegion(QualType elementType, NonLoc Idx, const MemRegion* sReg)
    : TypedValueRegion(sReg, ElementRegionKind),
      ElementType(elementType), Index(Idx) {
    assert((!isa<nonloc::ConcreteInt>(&Idx) ||
           cast<nonloc::ConcreteInt>(&Idx)->getValue().isSigned()) &&
           "The index must be signed");
  }

  static void ProfileRegion(llvm::FoldingSetNodeID& ID, QualType elementType,
                            SVal Idx, const MemRegion* superRegion);

public:

  NonLoc getIndex() const { return Index; }

  QualType getValueType() const {
    return ElementType;
  }

  QualType getElementType() const {
    return ElementType;
  }
  /// Compute the offset within the array. The array might also be a subobject.
  RegionRawOffset getAsArrayOffset() const;

  void dumpToStream(raw_ostream &os) const;

  void Profile(llvm::FoldingSetNodeID& ID) const;

  static bool classof(const MemRegion* R) {
    return R->getKind() == ElementRegionKind;
  }
};

// C++ temporary object associated with an expression.
class CXXTempObjectRegion : public TypedValueRegion {
  friend class MemRegionManager;

  Expr const *Ex;

  CXXTempObjectRegion(Expr const *E, MemRegion const *sReg) 
    : TypedValueRegion(sReg, CXXTempObjectRegionKind), Ex(E) {}

  static void ProfileRegion(llvm::FoldingSetNodeID &ID,
                            Expr const *E, const MemRegion *sReg);
  
public:
  const Expr *getExpr() const { return Ex; }

  QualType getValueType() const {
    return Ex->getType();
  }

  void dumpToStream(raw_ostream &os) const;

  void Profile(llvm::FoldingSetNodeID &ID) const;

  static bool classof(const MemRegion* R) {
    return R->getKind() == CXXTempObjectRegionKind;
  }
};

// CXXBaseObjectRegion represents a base object within a C++ object. It is 
// identified by the base class declaration and the region of its parent object.
class CXXBaseObjectRegion : public TypedValueRegion {
  friend class MemRegionManager;

  const CXXRecordDecl *decl;

  CXXBaseObjectRegion(const CXXRecordDecl *d, const MemRegion *sReg)
    : TypedValueRegion(sReg, CXXBaseObjectRegionKind), decl(d) {}

  static void ProfileRegion(llvm::FoldingSetNodeID &ID,
                            const CXXRecordDecl *decl, const MemRegion *sReg);

public:
  const CXXRecordDecl *getDecl() const { return decl; }

  QualType getValueType() const;

  void dumpToStream(raw_ostream &os) const;

  void Profile(llvm::FoldingSetNodeID &ID) const;

  static bool classof(const MemRegion *region) {
    return region->getKind() == CXXBaseObjectRegionKind;
  }
};

template<typename RegionTy>
const RegionTy* MemRegion::getAs() const {
  if (const RegionTy* RT = dyn_cast<RegionTy>(this))
    return RT;

  return NULL;
}

//===----------------------------------------------------------------------===//
// MemRegionManager - Factory object for creating regions.
//===----------------------------------------------------------------------===//

class MemRegionManager {
  ASTContext &C;
  llvm::BumpPtrAllocator& A;
  llvm::FoldingSet<MemRegion> Regions;

  GlobalInternalSpaceRegion *InternalGlobals;
  GlobalSystemSpaceRegion *SystemGlobals;
  GlobalImmutableSpaceRegion *ImmutableGlobals;

  
  llvm::DenseMap<const StackFrameContext *, StackLocalsSpaceRegion *> 
    StackLocalsSpaceRegions;
  llvm::DenseMap<const StackFrameContext *, StackArgumentsSpaceRegion *>
    StackArgumentsSpaceRegions;
  llvm::DenseMap<const CodeTextRegion *, StaticGlobalSpaceRegion *>
    StaticsGlobalSpaceRegions;

  HeapSpaceRegion *heap;
  UnknownSpaceRegion *unknown;
  MemSpaceRegion *code;

public:
  MemRegionManager(ASTContext &c, llvm::BumpPtrAllocator& a)
    : C(c), A(a), InternalGlobals(0), SystemGlobals(0), ImmutableGlobals(0),
      heap(0), unknown(0), code(0) {}

  ~MemRegionManager();

  ASTContext &getContext() { return C; }
  
  llvm::BumpPtrAllocator &getAllocator() { return A; }

  /// getStackLocalsRegion - Retrieve the memory region associated with the
  ///  specified stack frame.
  const StackLocalsSpaceRegion *
  getStackLocalsRegion(const StackFrameContext *STC);

  /// getStackArgumentsRegion - Retrieve the memory region associated with
  ///  function/method arguments of the specified stack frame.
  const StackArgumentsSpaceRegion *
  getStackArgumentsRegion(const StackFrameContext *STC);

  /// getGlobalsRegion - Retrieve the memory region associated with
  ///  global variables.
  const GlobalsSpaceRegion *getGlobalsRegion(
      MemRegion::Kind K = MemRegion::GlobalInternalSpaceRegionKind,
      const CodeTextRegion *R = 0);

  /// getHeapRegion - Retrieve the memory region associated with the
  ///  generic "heap".
  const HeapSpaceRegion *getHeapRegion();

  /// getUnknownRegion - Retrieve the memory region associated with unknown
  /// memory space.
  const MemSpaceRegion *getUnknownRegion();

  const MemSpaceRegion *getCodeRegion();

  /// getAllocaRegion - Retrieve a region associated with a call to alloca().
  const AllocaRegion *getAllocaRegion(const Expr *Ex, unsigned Cnt,
                                      const LocationContext *LC);

  /// getCompoundLiteralRegion - Retrieve the region associated with a
  ///  given CompoundLiteral.
  const CompoundLiteralRegion*
  getCompoundLiteralRegion(const CompoundLiteralExpr *CL,
                           const LocationContext *LC);
  
  /// getCXXThisRegion - Retrieve the [artificial] region associated with the
  ///  parameter 'this'.
  const CXXThisRegion *getCXXThisRegion(QualType thisPointerTy,
                                        const LocationContext *LC);

  /// getSymbolicRegion - Retrieve or create a "symbolic" memory region.
  const SymbolicRegion* getSymbolicRegion(SymbolRef sym);

  const StringRegion *getStringRegion(const StringLiteral* Str);

  const ObjCStringRegion *getObjCStringRegion(const ObjCStringLiteral *Str);

  /// getVarRegion - Retrieve or create the memory region associated with
  ///  a specified VarDecl and LocationContext.
  const VarRegion* getVarRegion(const VarDecl *D, const LocationContext *LC);

  /// getVarRegion - Retrieve or create the memory region associated with
  ///  a specified VarDecl and super region.
  const VarRegion* getVarRegion(const VarDecl *D, const MemRegion *superR);
  
  /// getElementRegion - Retrieve the memory region associated with the
  ///  associated element type, index, and super region.
  const ElementRegion *getElementRegion(QualType elementType, NonLoc Idx,
                                        const MemRegion *superRegion,
                                        ASTContext &Ctx);

  const ElementRegion *getElementRegionWithSuper(const ElementRegion *ER,
                                                 const MemRegion *superRegion) {
    return getElementRegion(ER->getElementType(), ER->getIndex(),
                            superRegion, ER->getContext());
  }

  /// getFieldRegion - Retrieve or create the memory region associated with
  ///  a specified FieldDecl.  'superRegion' corresponds to the containing
  ///  memory region (which typically represents the memory representing
  ///  a structure or class).
  const FieldRegion *getFieldRegion(const FieldDecl *fd,
                                    const MemRegion* superRegion);

  const FieldRegion *getFieldRegionWithSuper(const FieldRegion *FR,
                                             const MemRegion *superRegion) {
    return getFieldRegion(FR->getDecl(), superRegion);
  }

  /// getObjCIvarRegion - Retrieve or create the memory region associated with
  ///   a specified Objective-c instance variable.  'superRegion' corresponds
  ///   to the containing region (which typically represents the Objective-C
  ///   object).
  const ObjCIvarRegion *getObjCIvarRegion(const ObjCIvarDecl *ivd,
                                          const MemRegion* superRegion);

  const CXXTempObjectRegion *getCXXTempObjectRegion(Expr const *Ex,
                                                    LocationContext const *LC);

  const CXXBaseObjectRegion *getCXXBaseObjectRegion(const CXXRecordDecl *decl,
                                                  const MemRegion *superRegion);

  /// Create a CXXBaseObjectRegion with the same CXXRecordDecl but a different
  /// super region.
  const CXXBaseObjectRegion *
  getCXXBaseObjectRegionWithSuper(const CXXBaseObjectRegion *baseReg, 
                                  const MemRegion *superRegion) {
    return getCXXBaseObjectRegion(baseReg->getDecl(), superRegion);
  }

  const FunctionTextRegion *getFunctionTextRegion(const FunctionDecl *FD);
  const BlockTextRegion *getBlockTextRegion(const BlockDecl *BD,
                                            CanQualType locTy,
                                            AnalysisDeclContext *AC);
  
  /// getBlockDataRegion - Get the memory region associated with an instance
  ///  of a block.  Unlike many other MemRegions, the LocationContext*
  ///  argument is allowed to be NULL for cases where we have no known
  ///  context.
  const BlockDataRegion *getBlockDataRegion(const BlockTextRegion *bc,
                                            const LocationContext *lc = NULL);

private:
  template <typename RegionTy, typename A1>
  RegionTy* getRegion(const A1 a1);

  template <typename RegionTy, typename A1>
  RegionTy* getSubRegion(const A1 a1, const MemRegion* superRegion);

  template <typename RegionTy, typename A1, typename A2>
  RegionTy* getRegion(const A1 a1, const A2 a2);

  template <typename RegionTy, typename A1, typename A2>
  RegionTy* getSubRegion(const A1 a1, const A2 a2,
                         const MemRegion* superRegion);

  template <typename RegionTy, typename A1, typename A2, typename A3>
  RegionTy* getSubRegion(const A1 a1, const A2 a2, const A3 a3,
                         const MemRegion* superRegion);
  
  template <typename REG>
  const REG* LazyAllocate(REG*& region);
  
  template <typename REG, typename ARG>
  const REG* LazyAllocate(REG*& region, ARG a);
};

//===----------------------------------------------------------------------===//
// Out-of-line member definitions.
//===----------------------------------------------------------------------===//

inline ASTContext &MemRegion::getContext() const {
  return getMemRegionManager()->getContext();
}
  
} // end GR namespace

} // end clang namespace

//===----------------------------------------------------------------------===//
// Pretty-printing regions.
//===----------------------------------------------------------------------===//

namespace llvm {
static inline raw_ostream &operator<<(raw_ostream &os,
                                      const clang::ento::MemRegion* R) {
  R->dumpToStream(os);
  return os;
}
} // end llvm namespace

#endif
