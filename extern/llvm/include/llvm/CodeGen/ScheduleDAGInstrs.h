//==- ScheduleDAGInstrs.h - MachineInstr Scheduling --------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the ScheduleDAGInstrs class, which implements
// scheduling for a MachineInstr-based dependency graph.
//
//===----------------------------------------------------------------------===//

#ifndef SCHEDULEDAGINSTRS_H
#define SCHEDULEDAGINSTRS_H

#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SparseSet.h"
#include <map>

namespace llvm {
  class MachineLoopInfo;
  class MachineDominatorTree;
  class LiveIntervals;

  /// LoopDependencies - This class analyzes loop-oriented register
  /// dependencies, which are used to guide scheduling decisions.
  /// For example, loop induction variable increments should be
  /// scheduled as soon as possible after the variable's last use.
  ///
  class LoopDependencies {
    const MachineLoopInfo &MLI;
    const MachineDominatorTree &MDT;

  public:
    typedef std::map<unsigned, std::pair<const MachineOperand *, unsigned> >
      LoopDeps;
    LoopDeps Deps;

    LoopDependencies(const MachineLoopInfo &mli,
                     const MachineDominatorTree &mdt) :
      MLI(mli), MDT(mdt) {}

    /// VisitLoop - Clear out any previous state and analyze the given loop.
    ///
    void VisitLoop(const MachineLoop *Loop) {
      assert(Deps.empty() && "stale loop dependencies");

      MachineBasicBlock *Header = Loop->getHeader();
      SmallSet<unsigned, 8> LoopLiveIns;
      for (MachineBasicBlock::livein_iterator LI = Header->livein_begin(),
           LE = Header->livein_end(); LI != LE; ++LI)
        LoopLiveIns.insert(*LI);

      const MachineDomTreeNode *Node = MDT.getNode(Header);
      const MachineBasicBlock *MBB = Node->getBlock();
      assert(Loop->contains(MBB) &&
             "Loop does not contain header!");
      VisitRegion(Node, MBB, Loop, LoopLiveIns);
    }

  private:
    void VisitRegion(const MachineDomTreeNode *Node,
                     const MachineBasicBlock *MBB,
                     const MachineLoop *Loop,
                     const SmallSet<unsigned, 8> &LoopLiveIns) {
      unsigned Count = 0;
      for (MachineBasicBlock::const_iterator I = MBB->begin(), E = MBB->end();
           I != E; ++I) {
        const MachineInstr *MI = I;
        if (MI->isDebugValue())
          continue;
        for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
          const MachineOperand &MO = MI->getOperand(i);
          if (!MO.isReg() || !MO.isUse())
            continue;
          unsigned MOReg = MO.getReg();
          if (LoopLiveIns.count(MOReg))
            Deps.insert(std::make_pair(MOReg, std::make_pair(&MO, Count)));
        }
        ++Count; // Not every iteration due to dbg_value above.
      }

      const std::vector<MachineDomTreeNode*> &Children = Node->getChildren();
      for (std::vector<MachineDomTreeNode*>::const_iterator I =
           Children.begin(), E = Children.end(); I != E; ++I) {
        const MachineDomTreeNode *ChildNode = *I;
        MachineBasicBlock *ChildBlock = ChildNode->getBlock();
        if (Loop->contains(ChildBlock))
          VisitRegion(ChildNode, ChildBlock, Loop, LoopLiveIns);
      }
    }
  };

  /// An individual mapping from virtual register number to SUnit.
  struct VReg2SUnit {
    unsigned VirtReg;
    SUnit *SU;

    VReg2SUnit(unsigned reg, SUnit *su): VirtReg(reg), SU(su) {}

    unsigned getSparseSetKey() const {
      return TargetRegisterInfo::virtReg2Index(VirtReg);
    }
  };

  /// Combine a SparseSet with a 1x1 vector to track physical registers.
  /// The SparseSet allows iterating over the (few) live registers for quickly
  /// comparing against a regmask or clearing the set.
  ///
  /// Storage for the map is allocated once for the pass. The map can be
  /// cleared between scheduling regions without freeing unused entries.
  class Reg2SUnitsMap {
    SparseSet<unsigned> PhysRegSet;
    std::vector<std::vector<SUnit*> > SUnits;
  public:
    typedef SparseSet<unsigned>::const_iterator const_iterator;

    // Allow iteration over register numbers (keys) in the map. If needed, we
    // can provide an iterator over SUnits (values) as well.
    const_iterator reg_begin() const { return PhysRegSet.begin(); }
    const_iterator reg_end() const { return PhysRegSet.end(); }

    /// Initialize the map with the number of registers.
    /// If the map is already large enough, no allocation occurs.
    /// For simplicity we expect the map to be empty().
    void setRegLimit(unsigned Limit);

    /// Returns true if the map is empty.
    bool empty() const { return PhysRegSet.empty(); }

    /// Clear the map without deallocating storage.
    void clear();

    bool contains(unsigned Reg) const { return PhysRegSet.count(Reg); }

    /// If this register is mapped, return its existing SUnits vector.
    /// Otherwise map the register and return an empty SUnits vector.
    std::vector<SUnit *> &operator[](unsigned Reg) {
      bool New = PhysRegSet.insert(Reg).second;
      assert((!New || SUnits[Reg].empty()) && "stale SUnits vector");
      (void)New;
      return SUnits[Reg];
    }

    /// Erase an existing element without freeing memory.
    void erase(unsigned Reg) {
      PhysRegSet.erase(Reg);
      SUnits[Reg].clear();
    }
  };

  /// Use SparseSet as a SparseMap by relying on the fact that it never
  /// compares ValueT's, only unsigned keys. This allows the set to be cleared
  /// between scheduling regions in constant time as long as ValueT does not
  /// require a destructor.
  typedef SparseSet<VReg2SUnit> VReg2SUnitMap;

  /// ScheduleDAGInstrs - A ScheduleDAG subclass for scheduling lists of
  /// MachineInstrs.
  class ScheduleDAGInstrs : public ScheduleDAG {
  protected:
    const MachineLoopInfo &MLI;
    const MachineDominatorTree &MDT;
    const MachineFrameInfo *MFI;
    const InstrItineraryData *InstrItins;

    /// Live Intervals provides reaching defs in preRA scheduling.
    LiveIntervals *LIS;

    /// isPostRA flag indicates vregs cannot be present.
    bool IsPostRA;

    /// UnitLatencies (misnamed) flag avoids computing def-use latencies, using
    /// the def-side latency only.
    bool UnitLatencies;

    /// The standard DAG builder does not normally include terminators as DAG
    /// nodes because it does not create the necessary dependencies to prevent
    /// reordering. A specialized scheduler can overide
    /// TargetInstrInfo::isSchedulingBoundary then enable this flag to indicate
    /// it has taken responsibility for scheduling the terminator correctly.
    bool CanHandleTerminators;

    /// State specific to the current scheduling region.
    /// ------------------------------------------------

    /// The block in which to insert instructions
    MachineBasicBlock *BB;

    /// The beginning of the range to be scheduled.
    MachineBasicBlock::iterator RegionBegin;

    /// The end of the range to be scheduled.
    MachineBasicBlock::iterator RegionEnd;

    /// The index in BB of RegionEnd.
    unsigned EndIndex;

    /// After calling BuildSchedGraph, each machine instruction in the current
    /// scheduling region is mapped to an SUnit.
    DenseMap<MachineInstr*, SUnit*> MISUnitMap;

    /// State internal to DAG building.
    /// -------------------------------

    /// Defs, Uses - Remember where defs and uses of each register are as we
    /// iterate upward through the instructions. This is allocated here instead
    /// of inside BuildSchedGraph to avoid the need for it to be initialized and
    /// destructed for each block.
    Reg2SUnitsMap Defs;
    Reg2SUnitsMap Uses;

    /// Track the last instructon in this region defining each virtual register.
    VReg2SUnitMap VRegDefs;

    /// PendingLoads - Remember where unknown loads are after the most recent
    /// unknown store, as we iterate. As with Defs and Uses, this is here
    /// to minimize construction/destruction.
    std::vector<SUnit *> PendingLoads;

    /// LoopRegs - Track which registers are used for loop-carried dependencies.
    ///
    LoopDependencies LoopRegs;

    /// DbgValues - Remember instruction that preceeds DBG_VALUE.
    /// These are generated by buildSchedGraph but persist so they can be
    /// referenced when emitting the final schedule.
    typedef std::vector<std::pair<MachineInstr *, MachineInstr *> >
      DbgValueVector;
    DbgValueVector DbgValues;
    MachineInstr *FirstDbgValue;

  public:
    explicit ScheduleDAGInstrs(MachineFunction &mf,
                               const MachineLoopInfo &mli,
                               const MachineDominatorTree &mdt,
                               bool IsPostRAFlag,
                               LiveIntervals *LIS = 0);

    virtual ~ScheduleDAGInstrs() {}

    /// begin - Return an iterator to the top of the current scheduling region.
    MachineBasicBlock::iterator begin() const { return RegionBegin; }

    /// end - Return an iterator to the bottom of the current scheduling region.
    MachineBasicBlock::iterator end() const { return RegionEnd; }

    /// newSUnit - Creates a new SUnit and return a ptr to it.
    SUnit *newSUnit(MachineInstr *MI);

    /// getSUnit - Return an existing SUnit for this MI, or NULL.
    SUnit *getSUnit(MachineInstr *MI) const;

    /// startBlock - Prepare to perform scheduling in the given block.
    virtual void startBlock(MachineBasicBlock *BB);

    /// finishBlock - Clean up after scheduling in the given block.
    virtual void finishBlock();

    /// Initialize the scheduler state for the next scheduling region.
    virtual void enterRegion(MachineBasicBlock *bb,
                             MachineBasicBlock::iterator begin,
                             MachineBasicBlock::iterator end,
                             unsigned endcount);

    /// Notify that the scheduler has finished scheduling the current region.
    virtual void exitRegion();

    /// buildSchedGraph - Build SUnits from the MachineBasicBlock that we are
    /// input.
    void buildSchedGraph(AliasAnalysis *AA);

    /// addSchedBarrierDeps - Add dependencies from instructions in the current
    /// list of instructions being scheduled to scheduling barrier. We want to
    /// make sure instructions which define registers that are either used by
    /// the terminator or are live-out are properly scheduled. This is
    /// especially important when the definition latency of the return value(s)
    /// are too high to be hidden by the branch or when the liveout registers
    /// used by instructions in the fallthrough block.
    void addSchedBarrierDeps();

    /// computeLatency - Compute node latency.
    ///
    virtual void computeLatency(SUnit *SU);

    /// computeOperandLatency - Override dependence edge latency using
    /// operand use/def information
    ///
    virtual void computeOperandLatency(SUnit *Def, SUnit *Use,
                                       SDep& dep) const;

    /// schedule - Order nodes according to selected style, filling
    /// in the Sequence member.
    ///
    /// Typically, a scheduling algorithm will implement schedule() without
    /// overriding enterRegion() or exitRegion().
    virtual void schedule() = 0;

    /// finalizeSchedule - Allow targets to perform final scheduling actions at
    /// the level of the whole MachineFunction. By default does nothing.
    virtual void finalizeSchedule() {}

    virtual void dumpNode(const SUnit *SU) const;

    /// Return a label for a DAG node that points to an instruction.
    virtual std::string getGraphNodeLabel(const SUnit *SU) const;

    /// Return a label for the region of code covered by the DAG.
    virtual std::string getDAGName() const;

  protected:
    void initSUnits();
    void addPhysRegDataDeps(SUnit *SU, const MachineOperand &MO);
    void addPhysRegDeps(SUnit *SU, unsigned OperIdx);
    void addVRegDefDeps(SUnit *SU, unsigned OperIdx);
    void addVRegUseDeps(SUnit *SU, unsigned OperIdx);

    VReg2SUnitMap::iterator findVRegDef(unsigned VirtReg) {
      return VRegDefs.find(TargetRegisterInfo::virtReg2Index(VirtReg));
    }
  };

  /// newSUnit - Creates a new SUnit and return a ptr to it.
  inline SUnit *ScheduleDAGInstrs::newSUnit(MachineInstr *MI) {
#ifndef NDEBUG
    const SUnit *Addr = SUnits.empty() ? 0 : &SUnits[0];
#endif
    SUnits.push_back(SUnit(MI, (unsigned)SUnits.size()));
    assert((Addr == 0 || Addr == &SUnits[0]) &&
           "SUnits std::vector reallocated on the fly!");
    SUnits.back().OrigNode = &SUnits.back();
    return &SUnits.back();
  }

  /// getSUnit - Return an existing SUnit for this MI, or NULL.
  inline SUnit *ScheduleDAGInstrs::getSUnit(MachineInstr *MI) const {
    DenseMap<MachineInstr*, SUnit*>::const_iterator I = MISUnitMap.find(MI);
    if (I == MISUnitMap.end())
      return 0;
    return I->second;
  }
} // namespace llvm

#endif
