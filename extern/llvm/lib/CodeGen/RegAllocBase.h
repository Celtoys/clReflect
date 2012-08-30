//===-- RegAllocBase.h - basic regalloc interface and driver --*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the RegAllocBase class, which is the skeleton of a basic
// register allocation algorithm and interface for extending it. It provides the
// building blocks on which to construct other experimental allocators and test
// the validity of two principles:
//
// - If virtual and physical register liveness is modeled using intervals, then
// on-the-fly interference checking is cheap. Furthermore, interferences can be
// lazily cached and reused.
//
// - Register allocation complexity, and generated code performance is
// determined by the effectiveness of live range splitting rather than optimal
// coloring.
//
// Following the first principle, interfering checking revolves around the
// LiveIntervalUnion data structure.
//
// To fulfill the second principle, the basic allocator provides a driver for
// incremental splitting. It essentially punts on the problem of register
// coloring, instead driving the assignment of virtual to physical registers by
// the cost of splitting. The basic allocator allows for heuristic reassignment
// of registers, if a more sophisticated allocator chooses to do that.
//
// This framework provides a way to engineer the compile time vs. code
// quality trade-off without relying on a particular theoretical solver.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGALLOCBASE
#define LLVM_CODEGEN_REGALLOCBASE

#include "llvm/ADT/OwningPtr.h"
#include "LiveIntervalUnion.h"
#include "RegisterClassInfo.h"

namespace llvm {

template<typename T> class SmallVectorImpl;
class TargetRegisterInfo;
class VirtRegMap;
class LiveIntervals;
class Spiller;

/// RegAllocBase provides the register allocation driver and interface that can
/// be extended to add interesting heuristics.
///
/// Register allocators must override the selectOrSplit() method to implement
/// live range splitting. They must also override enqueue/dequeue to provide an
/// assignment order.
class RegAllocBase {
  LiveIntervalUnion::Allocator UnionAllocator;

  // Cache tag for PhysReg2LiveUnion entries. Increment whenever virtual
  // registers may have changed.
  unsigned UserTag;

  // Array of LiveIntervalUnions indexed by physical register.
  class LiveUnionArray {
    unsigned NumRegs;
    LiveIntervalUnion *Array;
  public:
    LiveUnionArray(): NumRegs(0), Array(0) {}
    ~LiveUnionArray() { clear(); }

    unsigned numRegs() const { return NumRegs; }

    void init(LiveIntervalUnion::Allocator &, unsigned NRegs);

    void clear();

    LiveIntervalUnion& operator[](unsigned PhysReg) {
      assert(PhysReg <  NumRegs && "physReg out of bounds");
      return Array[PhysReg];
    }
  };

  LiveUnionArray PhysReg2LiveUnion;

  // Current queries, one per physreg. They must be reinitialized each time we
  // query on a new live virtual register.
  OwningArrayPtr<LiveIntervalUnion::Query> Queries;

protected:
  const TargetRegisterInfo *TRI;
  MachineRegisterInfo *MRI;
  VirtRegMap *VRM;
  LiveIntervals *LIS;
  RegisterClassInfo RegClassInfo;

  RegAllocBase(): UserTag(0), TRI(0), MRI(0), VRM(0), LIS(0) {}

  virtual ~RegAllocBase() {}

  // A RegAlloc pass should call this before allocatePhysRegs.
  void init(VirtRegMap &vrm, LiveIntervals &lis);

  // Get an initialized query to check interferences between lvr and preg.  Note
  // that Query::init must be called at least once for each physical register
  // before querying a new live virtual register. This ties Queries and
  // PhysReg2LiveUnion together.
  LiveIntervalUnion::Query &query(LiveInterval &VirtReg, unsigned PhysReg) {
    Queries[PhysReg].init(UserTag, &VirtReg, &PhysReg2LiveUnion[PhysReg]);
    return Queries[PhysReg];
  }

  // Get direct access to the underlying LiveIntervalUnion for PhysReg.
  LiveIntervalUnion &getLiveUnion(unsigned PhysReg) {
    return PhysReg2LiveUnion[PhysReg];
  }

  // Invalidate all cached information about virtual registers - live ranges may
  // have changed.
  void invalidateVirtRegs() { ++UserTag; }

  // The top-level driver. The output is a VirtRegMap that us updated with
  // physical register assignments.
  void allocatePhysRegs();

  // Get a temporary reference to a Spiller instance.
  virtual Spiller &spiller() = 0;

  /// enqueue - Add VirtReg to the priority queue of unassigned registers.
  virtual void enqueue(LiveInterval *LI) = 0;

  /// dequeue - Return the next unassigned register, or NULL.
  virtual LiveInterval *dequeue() = 0;

  // A RegAlloc pass should override this to provide the allocation heuristics.
  // Each call must guarantee forward progess by returning an available PhysReg
  // or new set of split live virtual registers. It is up to the splitter to
  // converge quickly toward fully spilled live ranges.
  virtual unsigned selectOrSplit(LiveInterval &VirtReg,
                                 SmallVectorImpl<LiveInterval*> &splitLVRs) = 0;

  // A RegAlloc pass should call this when PassManager releases its memory.
  virtual void releaseMemory();

  // Helper for checking interference between a live virtual register and a
  // physical register, including all its register aliases. If an interference
  // exists, return the interfering register, which may be preg or an alias.
  unsigned checkPhysRegInterference(LiveInterval& VirtReg, unsigned PhysReg);

  /// assign - Assign VirtReg to PhysReg.
  /// This should not be called from selectOrSplit for the current register.
  void assign(LiveInterval &VirtReg, unsigned PhysReg);

  /// unassign - Undo a previous assignment of VirtReg to PhysReg.
  /// This can be invoked from selectOrSplit, but be careful to guarantee that
  /// allocation is making progress.
  void unassign(LiveInterval &VirtReg, unsigned PhysReg);

  /// addMBBLiveIns - Add physreg liveins to basic blocks.
  void addMBBLiveIns(MachineFunction *);

#ifndef NDEBUG
  // Verify each LiveIntervalUnion.
  void verify();
#endif

  // Use this group name for NamedRegionTimer.
  static const char *TimerGroupName;

public:
  /// VerifyEnabled - True when -verify-regalloc is given.
  static bool VerifyEnabled;

private:
  void seedLiveRegs();
};

} // end namespace llvm

#endif // !defined(LLVM_CODEGEN_REGALLOCBASE)
