//==- ProgramPoint.h - Program Points for Path-Sensitive Analysis --*- C++ -*-//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the interface ProgramPoint, which identifies a
//  distinct location in a function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_PROGRAM_POINT
#define LLVM_CLANG_ANALYSIS_PROGRAM_POINT

#include "clang/Analysis/AnalysisContext.h"
#include "clang/Analysis/CFG.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/Support/Casting.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>
#include <utility>
#include <string>

namespace clang {

class AnalysisDeclContext;
class FunctionDecl;
class LocationContext;
class ProgramPointTag;
  
class ProgramPoint {
public:
  enum Kind { BlockEdgeKind,
              BlockEntranceKind,
              BlockExitKind,
              PreStmtKind,
              PostStmtKind,
              PreLoadKind,
              PostLoadKind,
              PreStoreKind,
              PostStoreKind,
              PostPurgeDeadSymbolsKind,
              PostConditionKind,
              PostLValueKind,
              PostInitializerKind,
              CallEnterKind,
              CallExitKind,
              MinPostStmtKind = PostStmtKind,
              MaxPostStmtKind = CallExitKind,
              EpsilonKind};

private:
  llvm::PointerIntPair<const void *, 2, unsigned> Data1;
  llvm::PointerIntPair<const void *, 2, unsigned> Data2;

  // The LocationContext could be NULL to allow ProgramPoint to be used in
  // context insensitive analysis.
  llvm::PointerIntPair<const LocationContext *, 2, unsigned> L;

  const ProgramPointTag *Tag;

  ProgramPoint();
  
protected:
  ProgramPoint(const void *P,
               Kind k,
               const LocationContext *l,
               const ProgramPointTag *tag = 0)
    : Data1(P, ((unsigned) k) & 0x3),
      Data2(0, (((unsigned) k) >> 2) & 0x3),
      L(l, (((unsigned) k) >> 4) & 0x3),
      Tag(tag) {
        assert(getKind() == k);
        assert(getLocationContext() == l);
        assert(getData1() == P);
      }
        
  ProgramPoint(const void *P1,
               const void *P2,
               Kind k,
               const LocationContext *l,
               const ProgramPointTag *tag = 0)
    : Data1(P1, ((unsigned) k) & 0x3),
      Data2(P2, (((unsigned) k) >> 2) & 0x3),
      L(l, (((unsigned) k) >> 4) & 0x3),
      Tag(tag) {}

protected:
  const void *getData1() const { return Data1.getPointer(); }
  const void *getData2() const { return Data2.getPointer(); }
  void setData2(const void *d) { Data2.setPointer(d); }

public:
  /// Create a new ProgramPoint object that is the same as the original
  /// except for using the specified tag value.
  ProgramPoint withTag(const ProgramPointTag *tag) const {
    return ProgramPoint(getData1(), getData2(), getKind(),
                        getLocationContext(), tag);
  }

  Kind getKind() const {
    unsigned x = L.getInt();
    x <<= 2;
    x |= Data2.getInt();
    x <<= 2;
    x |= Data1.getInt();
    return (Kind) x;
  }

  const ProgramPointTag *getTag() const { return Tag; }

  const LocationContext *getLocationContext() const {
    return L.getPointer();
  }

  // For use with DenseMap.  This hash is probably slow.
  unsigned getHashValue() const {
    llvm::FoldingSetNodeID ID;
    Profile(ID);
    return ID.ComputeHash();
  }

  static bool classof(const ProgramPoint*) { return true; }

  bool operator==(const ProgramPoint & RHS) const {
    return Data1 == RHS.Data1 &&
           Data2 == RHS.Data2 &&
           L == RHS.L &&
           Tag == RHS.Tag;
  }

  bool operator!=(const ProgramPoint &RHS) const {
    return Data1 != RHS.Data1 ||
           Data2 != RHS.Data2 ||
           L != RHS.L ||
           Tag != RHS.Tag;
  }

  void Profile(llvm::FoldingSetNodeID& ID) const {
    ID.AddInteger((unsigned) getKind());
    ID.AddPointer(getData1());
    ID.AddPointer(getData2());
    ID.AddPointer(getLocationContext());
    ID.AddPointer(Tag);
  }

  static ProgramPoint getProgramPoint(const Stmt *S, ProgramPoint::Kind K,
                                      const LocationContext *LC,
                                      const ProgramPointTag *tag);
};

class BlockEntrance : public ProgramPoint {
public:
  BlockEntrance(const CFGBlock *B, const LocationContext *L,
                const ProgramPointTag *tag = 0)
    : ProgramPoint(B, BlockEntranceKind, L, tag) {    
    assert(B && "BlockEntrance requires non-null block");
  }

  const CFGBlock *getBlock() const {
    return reinterpret_cast<const CFGBlock*>(getData1());
  }

  const CFGElement getFirstElement() const {
    const CFGBlock *B = getBlock();
    return B->empty() ? CFGElement() : B->front();
  }
  
  static bool classof(const ProgramPoint* Location) {
    return Location->getKind() == BlockEntranceKind;
  }
};

class BlockExit : public ProgramPoint {
public:
  BlockExit(const CFGBlock *B, const LocationContext *L)
    : ProgramPoint(B, BlockExitKind, L) {}

  const CFGBlock *getBlock() const {
    return reinterpret_cast<const CFGBlock*>(getData1());
  }

  const Stmt *getTerminator() const {
    return getBlock()->getTerminator();
  }

  static bool classof(const ProgramPoint* Location) {
    return Location->getKind() == BlockExitKind;
  }
};

class StmtPoint : public ProgramPoint {
public:
  StmtPoint(const Stmt *S, const void *p2, Kind k, const LocationContext *L,
            const ProgramPointTag *tag)
    : ProgramPoint(S, p2, k, L, tag) {}

  const Stmt *getStmt() const { return (const Stmt*) getData1(); }

  template <typename T>
  const T* getStmtAs() const { return llvm::dyn_cast<T>(getStmt()); }

  static bool classof(const ProgramPoint* Location) {
    unsigned k = Location->getKind();
    return k >= PreStmtKind && k <= MaxPostStmtKind;
  }
};


class PreStmt : public StmtPoint {
public:
  PreStmt(const Stmt *S, const LocationContext *L, const ProgramPointTag *tag,
          const Stmt *SubStmt = 0)
    : StmtPoint(S, SubStmt, PreStmtKind, L, tag) {}

  const Stmt *getSubStmt() const { return (const Stmt*) getData2(); }

  static bool classof(const ProgramPoint* Location) {
    return Location->getKind() == PreStmtKind;
  }
};

class PostStmt : public StmtPoint {
protected:
  PostStmt(const Stmt *S, const void *data, Kind k, const LocationContext *L,
           const ProgramPointTag *tag = 0)
    : StmtPoint(S, data, k, L, tag) {}

public:
  explicit PostStmt(const Stmt *S, Kind k, 
                    const LocationContext *L, const ProgramPointTag *tag = 0)
    : StmtPoint(S, NULL, k, L, tag) {}

  explicit PostStmt(const Stmt *S, const LocationContext *L,
                    const ProgramPointTag *tag = 0)
    : StmtPoint(S, NULL, PostStmtKind, L, tag) {}

  static bool classof(const ProgramPoint* Location) {
    unsigned k = Location->getKind();
    return k >= MinPostStmtKind && k <= MaxPostStmtKind;
  }
};

// PostCondition represents the post program point of a branch condition.
class PostCondition : public PostStmt {
public:
  PostCondition(const Stmt *S, const LocationContext *L,
                const ProgramPointTag *tag = 0)
    : PostStmt(S, PostConditionKind, L, tag) {}

  static bool classof(const ProgramPoint* Location) {
    return Location->getKind() == PostConditionKind;
  }
};

class LocationCheck : public StmtPoint {
protected:
  LocationCheck(const Stmt *S, const LocationContext *L,
                ProgramPoint::Kind K, const ProgramPointTag *tag)
    : StmtPoint(S, NULL, K, L, tag) {}
    
  static bool classof(const ProgramPoint *location) {
    unsigned k = location->getKind();
    return k == PreLoadKind || k == PreStoreKind;
  }
};
  
class PreLoad : public LocationCheck {
public:
  PreLoad(const Stmt *S, const LocationContext *L,
          const ProgramPointTag *tag = 0)
    : LocationCheck(S, L, PreLoadKind, tag) {}
  
  static bool classof(const ProgramPoint *location) {
    return location->getKind() == PreLoadKind;
  }
};

class PreStore : public LocationCheck {
public:
  PreStore(const Stmt *S, const LocationContext *L,
           const ProgramPointTag *tag = 0)
  : LocationCheck(S, L, PreStoreKind, tag) {}
  
  static bool classof(const ProgramPoint *location) {
    return location->getKind() == PreStoreKind;
  }
};

class PostLoad : public PostStmt {
public:
  PostLoad(const Stmt *S, const LocationContext *L,
           const ProgramPointTag *tag = 0)
    : PostStmt(S, PostLoadKind, L, tag) {}

  static bool classof(const ProgramPoint* Location) {
    return Location->getKind() == PostLoadKind;
  }
};

/// \class Represents a program point after a store evaluation.
class PostStore : public PostStmt {
public:
  /// Construct the post store point.
  /// \param Loc can be used to store the information about the location 
  /// used in the form it was uttered in the code.
  PostStore(const Stmt *S, const LocationContext *L, const void *Loc,
            const ProgramPointTag *tag = 0)
    : PostStmt(S, PostStoreKind, L, tag) {
    assert(getData2() == 0);
    setData2(Loc);
  }

  static bool classof(const ProgramPoint* Location) {
    return Location->getKind() == PostStoreKind;
  }
  
  /// \brief Returns the information about the location used in the store,
  /// how it was uttered in the code.
  const void *getLocationValue() const {
    return getData2();
  }

};

class PostLValue : public PostStmt {
public:
  PostLValue(const Stmt *S, const LocationContext *L,
             const ProgramPointTag *tag = 0)
    : PostStmt(S, PostLValueKind, L, tag) {}

  static bool classof(const ProgramPoint* Location) {
    return Location->getKind() == PostLValueKind;
  }
};

class PostPurgeDeadSymbols : public PostStmt {
public:
  PostPurgeDeadSymbols(const Stmt *S, const LocationContext *L,
                       const ProgramPointTag *tag = 0)
    : PostStmt(S, PostPurgeDeadSymbolsKind, L, tag) {}

  static bool classof(const ProgramPoint* Location) {
    return Location->getKind() == PostPurgeDeadSymbolsKind;
  }
};

class BlockEdge : public ProgramPoint {
public:
  BlockEdge(const CFGBlock *B1, const CFGBlock *B2, const LocationContext *L)
    : ProgramPoint(B1, B2, BlockEdgeKind, L) {
    assert(B1 && "BlockEdge: source block must be non-null");
    assert(B2 && "BlockEdge: destination block must be non-null");    
  }

  const CFGBlock *getSrc() const {
    return static_cast<const CFGBlock*>(getData1());
  }

  const CFGBlock *getDst() const {
    return static_cast<const CFGBlock*>(getData2());
  }

  static bool classof(const ProgramPoint* Location) {
    return Location->getKind() == BlockEdgeKind;
  }
};

class PostInitializer : public ProgramPoint {
public:
  PostInitializer(const CXXCtorInitializer *I, 
                  const LocationContext *L)
    : ProgramPoint(I, PostInitializerKind, L) {}

  static bool classof(const ProgramPoint *Location) {
    return Location->getKind() == PostInitializerKind;
  }
};

class CallEnter : public StmtPoint {
public:
  CallEnter(const Stmt *stmt, const StackFrameContext *calleeCtx, 
            const LocationContext *callerCtx)
    : StmtPoint(stmt, calleeCtx, CallEnterKind, callerCtx, 0) {}

  const Stmt *getCallExpr() const {
    return static_cast<const Stmt *>(getData1());
  }

  const StackFrameContext *getCalleeContext() const {
    return static_cast<const StackFrameContext *>(getData2());
  }

  static bool classof(const ProgramPoint *Location) {
    return Location->getKind() == CallEnterKind;
  }
};

class CallExit : public StmtPoint {
public:
  // CallExit uses the callee's location context.
  CallExit(const Stmt *S, const LocationContext *L)
    : StmtPoint(S, 0, CallExitKind, L, 0) {}

  static bool classof(const ProgramPoint *Location) {
    return Location->getKind() == CallExitKind;
  }
};

/// This is a meta program point, which should be skipped by all the diagnostic
/// reasoning etc.
class EpsilonPoint : public ProgramPoint {
public:
  EpsilonPoint(const LocationContext *L, const void *Data1,
               const void *Data2 = 0, const ProgramPointTag *tag = 0)
    : ProgramPoint(Data1, Data2, EpsilonKind, L, tag) {}

  const void *getData() const { return getData1(); }

  static bool classof(const ProgramPoint* Location) {
    return Location->getKind() == EpsilonKind;
  }
};

/// ProgramPoints can be "tagged" as representing points specific to a given
/// analysis entity.  Tags are abstract annotations, with an associated
/// description and potentially other information.
class ProgramPointTag {
public:
  ProgramPointTag(void *tagKind = 0) : TagKind(tagKind) {}
  virtual ~ProgramPointTag();
  virtual StringRef getTagDescription() const = 0;    

protected:
  /// Used to implement 'classof' in subclasses.
  const void *getTagKind() { return TagKind; }
  
private:
  const void *TagKind;
};
  
class SimpleProgramPointTag : public ProgramPointTag {
  std::string desc;
public:
  SimpleProgramPointTag(StringRef description);
  StringRef getTagDescription() const;
};

} // end namespace clang


namespace llvm { // Traits specialization for DenseMap

template <> struct DenseMapInfo<clang::ProgramPoint> {

static inline clang::ProgramPoint getEmptyKey() {
  uintptr_t x =
   reinterpret_cast<uintptr_t>(DenseMapInfo<void*>::getEmptyKey()) & ~0x7;
  return clang::BlockEntrance(reinterpret_cast<clang::CFGBlock*>(x), 0);
}

static inline clang::ProgramPoint getTombstoneKey() {
  uintptr_t x =
   reinterpret_cast<uintptr_t>(DenseMapInfo<void*>::getTombstoneKey()) & ~0x7;
  return clang::BlockEntrance(reinterpret_cast<clang::CFGBlock*>(x), 0);
}

static unsigned getHashValue(const clang::ProgramPoint &Loc) {
  return Loc.getHashValue();
}

static bool isEqual(const clang::ProgramPoint &L,
                    const clang::ProgramPoint &R) {
  return L == R;
}

};
  
template <>
struct isPodLike<clang::ProgramPoint> { static const bool value = true; };

} // end namespace llvm

#endif
