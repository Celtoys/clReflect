//===---- ScheduleDAGInstrs.cpp - MachineInstr Rescheduling ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements the ScheduleDAGInstrs class, which implements re-scheduling
// of MachineInstrs.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "sched-instrs"
#include "llvm/Operator.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/LiveIntervalAnalysis.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Target/TargetSubtargetInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/SmallSet.h"
using namespace llvm;

ScheduleDAGInstrs::ScheduleDAGInstrs(MachineFunction &mf,
                                     const MachineLoopInfo &mli,
                                     const MachineDominatorTree &mdt,
                                     bool IsPostRAFlag,
                                     LiveIntervals *lis)
  : ScheduleDAG(mf), MLI(mli), MDT(mdt), MFI(mf.getFrameInfo()),
    InstrItins(mf.getTarget().getInstrItineraryData()), LIS(lis),
    IsPostRA(IsPostRAFlag), UnitLatencies(false), CanHandleTerminators(false),
    LoopRegs(MLI, MDT), FirstDbgValue(0) {
  assert((IsPostRA || LIS) && "PreRA scheduling requires LiveIntervals");
  DbgValues.clear();
  assert(!(IsPostRA && MRI.getNumVirtRegs()) &&
         "Virtual registers must be removed prior to PostRA scheduling");
}

/// getUnderlyingObjectFromInt - This is the function that does the work of
/// looking through basic ptrtoint+arithmetic+inttoptr sequences.
static const Value *getUnderlyingObjectFromInt(const Value *V) {
  do {
    if (const Operator *U = dyn_cast<Operator>(V)) {
      // If we find a ptrtoint, we can transfer control back to the
      // regular getUnderlyingObjectFromInt.
      if (U->getOpcode() == Instruction::PtrToInt)
        return U->getOperand(0);
      // If we find an add of a constant or a multiplied value, it's
      // likely that the other operand will lead us to the base
      // object. We don't have to worry about the case where the
      // object address is somehow being computed by the multiply,
      // because our callers only care when the result is an
      // identifibale object.
      if (U->getOpcode() != Instruction::Add ||
          (!isa<ConstantInt>(U->getOperand(1)) &&
           Operator::getOpcode(U->getOperand(1)) != Instruction::Mul))
        return V;
      V = U->getOperand(0);
    } else {
      return V;
    }
    assert(V->getType()->isIntegerTy() && "Unexpected operand type!");
  } while (1);
}

/// getUnderlyingObject - This is a wrapper around GetUnderlyingObject
/// and adds support for basic ptrtoint+arithmetic+inttoptr sequences.
static const Value *getUnderlyingObject(const Value *V) {
  // First just call Value::getUnderlyingObject to let it do what it does.
  do {
    V = GetUnderlyingObject(V);
    // If it found an inttoptr, use special code to continue climing.
    if (Operator::getOpcode(V) != Instruction::IntToPtr)
      break;
    const Value *O = getUnderlyingObjectFromInt(cast<User>(V)->getOperand(0));
    // If that succeeded in finding a pointer, continue the search.
    if (!O->getType()->isPointerTy())
      break;
    V = O;
  } while (1);
  return V;
}

/// getUnderlyingObjectForInstr - If this machine instr has memory reference
/// information and it can be tracked to a normal reference to a known
/// object, return the Value for that object. Otherwise return null.
static const Value *getUnderlyingObjectForInstr(const MachineInstr *MI,
                                                const MachineFrameInfo *MFI,
                                                bool &MayAlias) {
  MayAlias = true;
  if (!MI->hasOneMemOperand() ||
      !(*MI->memoperands_begin())->getValue() ||
      (*MI->memoperands_begin())->isVolatile())
    return 0;

  const Value *V = (*MI->memoperands_begin())->getValue();
  if (!V)
    return 0;

  V = getUnderlyingObject(V);
  if (const PseudoSourceValue *PSV = dyn_cast<PseudoSourceValue>(V)) {
    // For now, ignore PseudoSourceValues which may alias LLVM IR values
    // because the code that uses this function has no way to cope with
    // such aliases.
    if (PSV->isAliased(MFI))
      return 0;

    MayAlias = PSV->mayAlias(MFI);
    return V;
  }

  if (isIdentifiedObject(V))
    return V;

  return 0;
}

void ScheduleDAGInstrs::startBlock(MachineBasicBlock *BB) {
  LoopRegs.Deps.clear();
  if (MachineLoop *ML = MLI.getLoopFor(BB))
    if (BB == ML->getLoopLatch())
      LoopRegs.VisitLoop(ML);
}

void ScheduleDAGInstrs::finishBlock() {
  // Nothing to do.
}

/// Initialize the map with the number of registers.
void Reg2SUnitsMap::setRegLimit(unsigned Limit) {
  PhysRegSet.setUniverse(Limit);
  SUnits.resize(Limit);
}

/// Clear the map without deallocating storage.
void Reg2SUnitsMap::clear() {
  for (const_iterator I = reg_begin(), E = reg_end(); I != E; ++I) {
    SUnits[*I].clear();
  }
  PhysRegSet.clear();
}

/// Initialize the DAG and common scheduler state for the current scheduling
/// region. This does not actually create the DAG, only clears it. The
/// scheduling driver may call BuildSchedGraph multiple times per scheduling
/// region.
void ScheduleDAGInstrs::enterRegion(MachineBasicBlock *bb,
                                    MachineBasicBlock::iterator begin,
                                    MachineBasicBlock::iterator end,
                                    unsigned endcount) {
  BB = bb;
  RegionBegin = begin;
  RegionEnd = end;
  EndIndex = endcount;
  MISUnitMap.clear();

  // Check to see if the scheduler cares about latencies.
  UnitLatencies = forceUnitLatencies();

  ScheduleDAG::clearDAG();
}

/// Close the current scheduling region. Don't clear any state in case the
/// driver wants to refer to the previous scheduling region.
void ScheduleDAGInstrs::exitRegion() {
  // Nothing to do.
}

/// addSchedBarrierDeps - Add dependencies from instructions in the current
/// list of instructions being scheduled to scheduling barrier by adding
/// the exit SU to the register defs and use list. This is because we want to
/// make sure instructions which define registers that are either used by
/// the terminator or are live-out are properly scheduled. This is
/// especially important when the definition latency of the return value(s)
/// are too high to be hidden by the branch or when the liveout registers
/// used by instructions in the fallthrough block.
void ScheduleDAGInstrs::addSchedBarrierDeps() {
  MachineInstr *ExitMI = RegionEnd != BB->end() ? &*RegionEnd : 0;
  ExitSU.setInstr(ExitMI);
  bool AllDepKnown = ExitMI &&
    (ExitMI->isCall() || ExitMI->isBarrier());
  if (ExitMI && AllDepKnown) {
    // If it's a call or a barrier, add dependencies on the defs and uses of
    // instruction.
    for (unsigned i = 0, e = ExitMI->getNumOperands(); i != e; ++i) {
      const MachineOperand &MO = ExitMI->getOperand(i);
      if (!MO.isReg() || MO.isDef()) continue;
      unsigned Reg = MO.getReg();
      if (Reg == 0) continue;

      if (TRI->isPhysicalRegister(Reg))
        Uses[Reg].push_back(&ExitSU);
      else {
        assert(!IsPostRA && "Virtual register encountered after regalloc.");
        addVRegUseDeps(&ExitSU, i);
      }
    }
  } else {
    // For others, e.g. fallthrough, conditional branch, assume the exit
    // uses all the registers that are livein to the successor blocks.
    assert(Uses.empty() && "Uses in set before adding deps?");
    for (MachineBasicBlock::succ_iterator SI = BB->succ_begin(),
           SE = BB->succ_end(); SI != SE; ++SI)
      for (MachineBasicBlock::livein_iterator I = (*SI)->livein_begin(),
             E = (*SI)->livein_end(); I != E; ++I) {
        unsigned Reg = *I;
        if (!Uses.contains(Reg))
          Uses[Reg].push_back(&ExitSU);
      }
  }
}

/// MO is an operand of SU's instruction that defines a physical register. Add
/// data dependencies from SU to any uses of the physical register.
void ScheduleDAGInstrs::addPhysRegDataDeps(SUnit *SU,
                                           const MachineOperand &MO) {
  assert(MO.isDef() && "expect physreg def");

  // Ask the target if address-backscheduling is desirable, and if so how much.
  const TargetSubtargetInfo &ST = TM.getSubtarget<TargetSubtargetInfo>();
  unsigned SpecialAddressLatency = ST.getSpecialAddressLatency();
  unsigned DataLatency = SU->Latency;

  for (const uint16_t *Alias = TRI->getOverlaps(MO.getReg()); *Alias; ++Alias) {
    if (!Uses.contains(*Alias))
      continue;
    std::vector<SUnit*> &UseList = Uses[*Alias];
    for (unsigned i = 0, e = UseList.size(); i != e; ++i) {
      SUnit *UseSU = UseList[i];
      if (UseSU == SU)
        continue;
      unsigned LDataLatency = DataLatency;
      // Optionally add in a special extra latency for nodes that
      // feed addresses.
      // TODO: Perhaps we should get rid of
      // SpecialAddressLatency and just move this into
      // adjustSchedDependency for the targets that care about it.
      if (SpecialAddressLatency != 0 && !UnitLatencies &&
          UseSU != &ExitSU) {
        MachineInstr *UseMI = UseSU->getInstr();
        const MCInstrDesc &UseMCID = UseMI->getDesc();
        int RegUseIndex = UseMI->findRegisterUseOperandIdx(*Alias);
        assert(RegUseIndex >= 0 && "UseMI doesn't use register!");
        if (RegUseIndex >= 0 &&
            (UseMI->mayLoad() || UseMI->mayStore()) &&
            (unsigned)RegUseIndex < UseMCID.getNumOperands() &&
            UseMCID.OpInfo[RegUseIndex].isLookupPtrRegClass())
          LDataLatency += SpecialAddressLatency;
      }
      // Adjust the dependence latency using operand def/use
      // information (if any), and then allow the target to
      // perform its own adjustments.
      const SDep& dep = SDep(SU, SDep::Data, LDataLatency, *Alias);
      if (!UnitLatencies) {
        computeOperandLatency(SU, UseSU, const_cast<SDep &>(dep));
        ST.adjustSchedDependency(SU, UseSU, const_cast<SDep &>(dep));
      }
      UseSU->addPred(dep);
    }
  }
}

/// addPhysRegDeps - Add register dependencies (data, anti, and output) from
/// this SUnit to following instructions in the same scheduling region that
/// depend the physical register referenced at OperIdx.
void ScheduleDAGInstrs::addPhysRegDeps(SUnit *SU, unsigned OperIdx) {
  const MachineInstr *MI = SU->getInstr();
  const MachineOperand &MO = MI->getOperand(OperIdx);

  // Optionally add output and anti dependencies. For anti
  // dependencies we use a latency of 0 because for a multi-issue
  // target we want to allow the defining instruction to issue
  // in the same cycle as the using instruction.
  // TODO: Using a latency of 1 here for output dependencies assumes
  //       there's no cost for reusing registers.
  SDep::Kind Kind = MO.isUse() ? SDep::Anti : SDep::Output;
  for (const uint16_t *Alias = TRI->getOverlaps(MO.getReg()); *Alias; ++Alias) {
    if (!Defs.contains(*Alias))
      continue;
    std::vector<SUnit *> &DefList = Defs[*Alias];
    for (unsigned i = 0, e = DefList.size(); i != e; ++i) {
      SUnit *DefSU = DefList[i];
      if (DefSU == &ExitSU)
        continue;
      if (DefSU != SU &&
          (Kind != SDep::Output || !MO.isDead() ||
           !DefSU->getInstr()->registerDefIsDead(*Alias))) {
        if (Kind == SDep::Anti)
          DefSU->addPred(SDep(SU, Kind, 0, /*Reg=*/*Alias));
        else {
          unsigned AOLat = TII->getOutputLatency(InstrItins, MI, OperIdx,
                                                 DefSU->getInstr());
          DefSU->addPred(SDep(SU, Kind, AOLat, /*Reg=*/*Alias));
        }
      }
    }
  }

  if (!MO.isDef()) {
    // Either insert a new Reg2SUnits entry with an empty SUnits list, or
    // retrieve the existing SUnits list for this register's uses.
    // Push this SUnit on the use list.
    Uses[MO.getReg()].push_back(SU);
  }
  else {
    addPhysRegDataDeps(SU, MO);

    // Either insert a new Reg2SUnits entry with an empty SUnits list, or
    // retrieve the existing SUnits list for this register's defs.
    std::vector<SUnit *> &DefList = Defs[MO.getReg()];

    // If a def is going to wrap back around to the top of the loop,
    // backschedule it.
    if (!UnitLatencies && DefList.empty()) {
      LoopDependencies::LoopDeps::iterator I = LoopRegs.Deps.find(MO.getReg());
      if (I != LoopRegs.Deps.end()) {
        const MachineOperand *UseMO = I->second.first;
        unsigned Count = I->second.second;
        const MachineInstr *UseMI = UseMO->getParent();
        unsigned UseMOIdx = UseMO - &UseMI->getOperand(0);
        const MCInstrDesc &UseMCID = UseMI->getDesc();
        const TargetSubtargetInfo &ST =
          TM.getSubtarget<TargetSubtargetInfo>();
        unsigned SpecialAddressLatency = ST.getSpecialAddressLatency();
        // TODO: If we knew the total depth of the region here, we could
        // handle the case where the whole loop is inside the region but
        // is large enough that the isScheduleHigh trick isn't needed.
        if (UseMOIdx < UseMCID.getNumOperands()) {
          // Currently, we only support scheduling regions consisting of
          // single basic blocks. Check to see if the instruction is in
          // the same region by checking to see if it has the same parent.
          if (UseMI->getParent() != MI->getParent()) {
            unsigned Latency = SU->Latency;
            if (UseMCID.OpInfo[UseMOIdx].isLookupPtrRegClass())
              Latency += SpecialAddressLatency;
            // This is a wild guess as to the portion of the latency which
            // will be overlapped by work done outside the current
            // scheduling region.
            Latency -= std::min(Latency, Count);
            // Add the artificial edge.
            ExitSU.addPred(SDep(SU, SDep::Order, Latency,
                                /*Reg=*/0, /*isNormalMemory=*/false,
                                /*isMustAlias=*/false,
                                /*isArtificial=*/true));
          } else if (SpecialAddressLatency > 0 &&
                     UseMCID.OpInfo[UseMOIdx].isLookupPtrRegClass()) {
            // The entire loop body is within the current scheduling region
            // and the latency of this operation is assumed to be greater
            // than the latency of the loop.
            // TODO: Recursively mark data-edge predecessors as
            //       isScheduleHigh too.
            SU->isScheduleHigh = true;
          }
        }
        LoopRegs.Deps.erase(I);
      }
    }

    // clear this register's use list
    if (Uses.contains(MO.getReg()))
      Uses[MO.getReg()].clear();

    if (!MO.isDead())
      DefList.clear();

    // Calls will not be reordered because of chain dependencies (see
    // below). Since call operands are dead, calls may continue to be added
    // to the DefList making dependence checking quadratic in the size of
    // the block. Instead, we leave only one call at the back of the
    // DefList.
    if (SU->isCall) {
      while (!DefList.empty() && DefList.back()->isCall)
        DefList.pop_back();
    }
    // Defs are pushed in the order they are visited and never reordered.
    DefList.push_back(SU);
  }
}

/// addVRegDefDeps - Add register output and data dependencies from this SUnit
/// to instructions that occur later in the same scheduling region if they read
/// from or write to the virtual register defined at OperIdx.
///
/// TODO: Hoist loop induction variable increments. This has to be
/// reevaluated. Generally, IV scheduling should be done before coalescing.
void ScheduleDAGInstrs::addVRegDefDeps(SUnit *SU, unsigned OperIdx) {
  const MachineInstr *MI = SU->getInstr();
  unsigned Reg = MI->getOperand(OperIdx).getReg();

  // SSA defs do not have output/anti dependencies.
  // The current operand is a def, so we have at least one.
  if (llvm::next(MRI.def_begin(Reg)) == MRI.def_end())
    return;

  // Add output dependence to the next nearest def of this vreg.
  //
  // Unless this definition is dead, the output dependence should be
  // transitively redundant with antidependencies from this definition's
  // uses. We're conservative for now until we have a way to guarantee the uses
  // are not eliminated sometime during scheduling. The output dependence edge
  // is also useful if output latency exceeds def-use latency.
  VReg2SUnitMap::iterator DefI = findVRegDef(Reg);
  if (DefI == VRegDefs.end())
    VRegDefs.insert(VReg2SUnit(Reg, SU));
  else {
    SUnit *DefSU = DefI->SU;
    if (DefSU != SU && DefSU != &ExitSU) {
      unsigned OutLatency = TII->getOutputLatency(InstrItins, MI, OperIdx,
                                                  DefSU->getInstr());
      DefSU->addPred(SDep(SU, SDep::Output, OutLatency, Reg));
    }
    DefI->SU = SU;
  }
}

/// addVRegUseDeps - Add a register data dependency if the instruction that
/// defines the virtual register used at OperIdx is mapped to an SUnit. Add a
/// register antidependency from this SUnit to instructions that occur later in
/// the same scheduling region if they write the virtual register.
///
/// TODO: Handle ExitSU "uses" properly.
void ScheduleDAGInstrs::addVRegUseDeps(SUnit *SU, unsigned OperIdx) {
  MachineInstr *MI = SU->getInstr();
  unsigned Reg = MI->getOperand(OperIdx).getReg();

  // Lookup this operand's reaching definition.
  assert(LIS && "vreg dependencies requires LiveIntervals");
  SlotIndex UseIdx = LIS->getInstructionIndex(MI).getRegSlot();
  LiveInterval *LI = &LIS->getInterval(Reg);
  VNInfo *VNI = LI->getVNInfoBefore(UseIdx);
  // VNI will be valid because MachineOperand::readsReg() is checked by caller.
  MachineInstr *Def = LIS->getInstructionFromIndex(VNI->def);
  // Phis and other noninstructions (after coalescing) have a NULL Def.
  if (Def) {
    SUnit *DefSU = getSUnit(Def);
    if (DefSU) {
      // The reaching Def lives within this scheduling region.
      // Create a data dependence.
      //
      // TODO: Handle "special" address latencies cleanly.
      const SDep &dep = SDep(DefSU, SDep::Data, DefSU->Latency, Reg);
      if (!UnitLatencies) {
        // Adjust the dependence latency using operand def/use information, then
        // allow the target to perform its own adjustments.
        computeOperandLatency(DefSU, SU, const_cast<SDep &>(dep));
        const TargetSubtargetInfo &ST = TM.getSubtarget<TargetSubtargetInfo>();
        ST.adjustSchedDependency(DefSU, SU, const_cast<SDep &>(dep));
      }
      SU->addPred(dep);
    }
  }

  // Add antidependence to the following def of the vreg it uses.
  VReg2SUnitMap::iterator DefI = findVRegDef(Reg);
  if (DefI != VRegDefs.end() && DefI->SU != SU)
    DefI->SU->addPred(SDep(SU, SDep::Anti, 0, Reg));
}

/// Create an SUnit for each real instruction, numbered in top-down toplological
/// order. The instruction order A < B, implies that no edge exists from B to A.
///
/// Map each real instruction to its SUnit.
///
/// After initSUnits, the SUnits vector cannot be resized and the scheduler may
/// hang onto SUnit pointers. We may relax this in the future by using SUnit IDs
/// instead of pointers.
///
/// MachineScheduler relies on initSUnits numbering the nodes by their order in
/// the original instruction list.
void ScheduleDAGInstrs::initSUnits() {
  // We'll be allocating one SUnit for each real instruction in the region,
  // which is contained within a basic block.
  SUnits.reserve(BB->size());

  for (MachineBasicBlock::iterator I = RegionBegin; I != RegionEnd; ++I) {
    MachineInstr *MI = I;
    if (MI->isDebugValue())
      continue;

    SUnit *SU = newSUnit(MI);
    MISUnitMap[MI] = SU;

    SU->isCall = MI->isCall();
    SU->isCommutable = MI->isCommutable();

    // Assign the Latency field of SU using target-provided information.
    if (UnitLatencies)
      SU->Latency = 1;
    else
      computeLatency(SU);
  }
}

void ScheduleDAGInstrs::buildSchedGraph(AliasAnalysis *AA) {
  // Create an SUnit for each real instruction.
  initSUnits();

  // We build scheduling units by walking a block's instruction list from bottom
  // to top.

  // Remember where a generic side-effecting instruction is as we procede.
  SUnit *BarrierChain = 0, *AliasChain = 0;

  // Memory references to specific known memory locations are tracked
  // so that they can be given more precise dependencies. We track
  // separately the known memory locations that may alias and those
  // that are known not to alias
  std::map<const Value *, SUnit *> AliasMemDefs, NonAliasMemDefs;
  std::map<const Value *, std::vector<SUnit *> > AliasMemUses, NonAliasMemUses;

  // Remove any stale debug info; sometimes BuildSchedGraph is called again
  // without emitting the info from the previous call.
  DbgValues.clear();
  FirstDbgValue = NULL;

  assert(Defs.empty() && Uses.empty() &&
         "Only BuildGraph should update Defs/Uses");
  Defs.setRegLimit(TRI->getNumRegs());
  Uses.setRegLimit(TRI->getNumRegs());

  assert(VRegDefs.empty() && "Only BuildSchedGraph may access VRegDefs");
  // FIXME: Allow SparseSet to reserve space for the creation of virtual
  // registers during scheduling. Don't artificially inflate the Universe
  // because we want to assert that vregs are not created during DAG building.
  VRegDefs.setUniverse(MRI.getNumVirtRegs());

  // Model data dependencies between instructions being scheduled and the
  // ExitSU.
  addSchedBarrierDeps();

  // Walk the list of instructions, from bottom moving up.
  MachineInstr *PrevMI = NULL;
  for (MachineBasicBlock::iterator MII = RegionEnd, MIE = RegionBegin;
       MII != MIE; --MII) {
    MachineInstr *MI = prior(MII);
    if (MI && PrevMI) {
      DbgValues.push_back(std::make_pair(PrevMI, MI));
      PrevMI = NULL;
    }

    if (MI->isDebugValue()) {
      PrevMI = MI;
      continue;
    }

    assert((!MI->isTerminator() || CanHandleTerminators) && !MI->isLabel() &&
           "Cannot schedule terminators or labels!");

    SUnit *SU = MISUnitMap[MI];
    assert(SU && "No SUnit mapped to this MI");

    // Add register-based dependencies (data, anti, and output).
    for (unsigned j = 0, n = MI->getNumOperands(); j != n; ++j) {
      const MachineOperand &MO = MI->getOperand(j);
      if (!MO.isReg()) continue;
      unsigned Reg = MO.getReg();
      if (Reg == 0) continue;

      if (TRI->isPhysicalRegister(Reg))
        addPhysRegDeps(SU, j);
      else {
        assert(!IsPostRA && "Virtual register encountered!");
        if (MO.isDef())
          addVRegDefDeps(SU, j);
        else if (MO.readsReg()) // ignore undef operands
          addVRegUseDeps(SU, j);
      }
    }

    // Add chain dependencies.
    // Chain dependencies used to enforce memory order should have
    // latency of 0 (except for true dependency of Store followed by
    // aliased Load... we estimate that with a single cycle of latency
    // assuming the hardware will bypass)
    // Note that isStoreToStackSlot and isLoadFromStackSLot are not usable
    // after stack slots are lowered to actual addresses.
    // TODO: Use an AliasAnalysis and do real alias-analysis queries, and
    // produce more precise dependence information.
#define STORE_LOAD_LATENCY 1
    unsigned TrueMemOrderLatency = 0;
    if (MI->isCall() || MI->hasUnmodeledSideEffects() ||
        (MI->hasVolatileMemoryRef() &&
         (!MI->mayLoad() || !MI->isInvariantLoad(AA)))) {
      // Be conservative with these and add dependencies on all memory
      // references, even those that are known to not alias.
      for (std::map<const Value *, SUnit *>::iterator I =
             NonAliasMemDefs.begin(), E = NonAliasMemDefs.end(); I != E; ++I) {
        I->second->addPred(SDep(SU, SDep::Order, /*Latency=*/0));
      }
      for (std::map<const Value *, std::vector<SUnit *> >::iterator I =
             NonAliasMemUses.begin(), E = NonAliasMemUses.end(); I != E; ++I) {
        for (unsigned i = 0, e = I->second.size(); i != e; ++i)
          I->second[i]->addPred(SDep(SU, SDep::Order, TrueMemOrderLatency));
      }
      NonAliasMemDefs.clear();
      NonAliasMemUses.clear();
      // Add SU to the barrier chain.
      if (BarrierChain)
        BarrierChain->addPred(SDep(SU, SDep::Order, /*Latency=*/0));
      BarrierChain = SU;

      // fall-through
    new_alias_chain:
      // Chain all possibly aliasing memory references though SU.
      if (AliasChain)
        AliasChain->addPred(SDep(SU, SDep::Order, /*Latency=*/0));
      AliasChain = SU;
      for (unsigned k = 0, m = PendingLoads.size(); k != m; ++k)
        PendingLoads[k]->addPred(SDep(SU, SDep::Order, TrueMemOrderLatency));
      for (std::map<const Value *, SUnit *>::iterator I = AliasMemDefs.begin(),
           E = AliasMemDefs.end(); I != E; ++I) {
        I->second->addPred(SDep(SU, SDep::Order, /*Latency=*/0));
      }
      for (std::map<const Value *, std::vector<SUnit *> >::iterator I =
           AliasMemUses.begin(), E = AliasMemUses.end(); I != E; ++I) {
        for (unsigned i = 0, e = I->second.size(); i != e; ++i)
          I->second[i]->addPred(SDep(SU, SDep::Order, TrueMemOrderLatency));
      }
      PendingLoads.clear();
      AliasMemDefs.clear();
      AliasMemUses.clear();
    } else if (MI->mayStore()) {
      bool MayAlias = true;
      TrueMemOrderLatency = STORE_LOAD_LATENCY;
      if (const Value *V = getUnderlyingObjectForInstr(MI, MFI, MayAlias)) {
        // A store to a specific PseudoSourceValue. Add precise dependencies.
        // Record the def in MemDefs, first adding a dep if there is
        // an existing def.
        std::map<const Value *, SUnit *>::iterator I =
          ((MayAlias) ? AliasMemDefs.find(V) : NonAliasMemDefs.find(V));
        std::map<const Value *, SUnit *>::iterator IE =
          ((MayAlias) ? AliasMemDefs.end() : NonAliasMemDefs.end());
        if (I != IE) {
          I->second->addPred(SDep(SU, SDep::Order, /*Latency=*/0, /*Reg=*/0,
                                  /*isNormalMemory=*/true));
          I->second = SU;
        } else {
          if (MayAlias)
            AliasMemDefs[V] = SU;
          else
            NonAliasMemDefs[V] = SU;
        }
        // Handle the uses in MemUses, if there are any.
        std::map<const Value *, std::vector<SUnit *> >::iterator J =
          ((MayAlias) ? AliasMemUses.find(V) : NonAliasMemUses.find(V));
        std::map<const Value *, std::vector<SUnit *> >::iterator JE =
          ((MayAlias) ? AliasMemUses.end() : NonAliasMemUses.end());
        if (J != JE) {
          for (unsigned i = 0, e = J->second.size(); i != e; ++i)
            J->second[i]->addPred(SDep(SU, SDep::Order, TrueMemOrderLatency,
                                       /*Reg=*/0, /*isNormalMemory=*/true));
          J->second.clear();
        }
        if (MayAlias) {
          // Add dependencies from all the PendingLoads, i.e. loads
          // with no underlying object.
          for (unsigned k = 0, m = PendingLoads.size(); k != m; ++k)
            PendingLoads[k]->addPred(SDep(SU, SDep::Order, TrueMemOrderLatency));
          // Add dependence on alias chain, if needed.
          if (AliasChain)
            AliasChain->addPred(SDep(SU, SDep::Order, /*Latency=*/0));
        }
        // Add dependence on barrier chain, if needed.
        if (BarrierChain)
          BarrierChain->addPred(SDep(SU, SDep::Order, /*Latency=*/0));
      } else {
        // Treat all other stores conservatively.
        goto new_alias_chain;
      }

      if (!ExitSU.isPred(SU))
        // Push store's up a bit to avoid them getting in between cmp
        // and branches.
        ExitSU.addPred(SDep(SU, SDep::Order, 0,
                            /*Reg=*/0, /*isNormalMemory=*/false,
                            /*isMustAlias=*/false,
                            /*isArtificial=*/true));
    } else if (MI->mayLoad()) {
      bool MayAlias = true;
      TrueMemOrderLatency = 0;
      if (MI->isInvariantLoad(AA)) {
        // Invariant load, no chain dependencies needed!
      } else {
        if (const Value *V =
            getUnderlyingObjectForInstr(MI, MFI, MayAlias)) {
          // A load from a specific PseudoSourceValue. Add precise dependencies.
          std::map<const Value *, SUnit *>::iterator I =
            ((MayAlias) ? AliasMemDefs.find(V) : NonAliasMemDefs.find(V));
          std::map<const Value *, SUnit *>::iterator IE =
            ((MayAlias) ? AliasMemDefs.end() : NonAliasMemDefs.end());
          if (I != IE)
            I->second->addPred(SDep(SU, SDep::Order, /*Latency=*/0, /*Reg=*/0,
                                    /*isNormalMemory=*/true));
          if (MayAlias)
            AliasMemUses[V].push_back(SU);
          else
            NonAliasMemUses[V].push_back(SU);
        } else {
          // A load with no underlying object. Depend on all
          // potentially aliasing stores.
          for (std::map<const Value *, SUnit *>::iterator I =
                 AliasMemDefs.begin(), E = AliasMemDefs.end(); I != E; ++I)
            I->second->addPred(SDep(SU, SDep::Order, /*Latency=*/0));

          PendingLoads.push_back(SU);
          MayAlias = true;
        }

        // Add dependencies on alias and barrier chains, if needed.
        if (MayAlias && AliasChain)
          AliasChain->addPred(SDep(SU, SDep::Order, /*Latency=*/0));
        if (BarrierChain)
          BarrierChain->addPred(SDep(SU, SDep::Order, /*Latency=*/0));
      }
    }
  }
  if (PrevMI)
    FirstDbgValue = PrevMI;

  Defs.clear();
  Uses.clear();
  VRegDefs.clear();
  PendingLoads.clear();
}

void ScheduleDAGInstrs::computeLatency(SUnit *SU) {
  // Compute the latency for the node.
  if (!InstrItins || InstrItins->isEmpty()) {
    SU->Latency = 1;

    // Simplistic target-independent heuristic: assume that loads take
    // extra time.
    if (SU->getInstr()->mayLoad())
      SU->Latency += 2;
  } else {
    SU->Latency = TII->getInstrLatency(InstrItins, SU->getInstr());
  }
}

void ScheduleDAGInstrs::computeOperandLatency(SUnit *Def, SUnit *Use,
                                              SDep& dep) const {
  if (!InstrItins || InstrItins->isEmpty())
    return;

  // For a data dependency with a known register...
  if ((dep.getKind() != SDep::Data) || (dep.getReg() == 0))
    return;

  const unsigned Reg = dep.getReg();

  // ... find the definition of the register in the defining
  // instruction
  MachineInstr *DefMI = Def->getInstr();
  int DefIdx = DefMI->findRegisterDefOperandIdx(Reg);
  if (DefIdx != -1) {
    const MachineOperand &MO = DefMI->getOperand(DefIdx);
    if (MO.isReg() && MO.isImplicit() &&
        DefIdx >= (int)DefMI->getDesc().getNumOperands()) {
      // This is an implicit def, getOperandLatency() won't return the correct
      // latency. e.g.
      //   %D6<def>, %D7<def> = VLD1q16 %R2<kill>, 0, ..., %Q3<imp-def>
      //   %Q1<def> = VMULv8i16 %Q1<kill>, %Q3<kill>, ...
      // What we want is to compute latency between def of %D6/%D7 and use of
      // %Q3 instead.
      unsigned Op2 = DefMI->findRegisterDefOperandIdx(Reg, false, true, TRI);
      if (DefMI->getOperand(Op2).isReg())
        DefIdx = Op2;
    }
    MachineInstr *UseMI = Use->getInstr();
    // For all uses of the register, calculate the maxmimum latency
    int Latency = -1;
    if (UseMI) {
      for (unsigned i = 0, e = UseMI->getNumOperands(); i != e; ++i) {
        const MachineOperand &MO = UseMI->getOperand(i);
        if (!MO.isReg() || !MO.isUse())
          continue;
        unsigned MOReg = MO.getReg();
        if (MOReg != Reg)
          continue;

        int UseCycle = TII->getOperandLatency(InstrItins, DefMI, DefIdx,
                                              UseMI, i);
        Latency = std::max(Latency, UseCycle);
      }
    } else {
      // UseMI is null, then it must be a scheduling barrier.
      if (!InstrItins || InstrItins->isEmpty())
        return;
      unsigned DefClass = DefMI->getDesc().getSchedClass();
      Latency = InstrItins->getOperandCycle(DefClass, DefIdx);
    }

    // If we found a latency, then replace the existing dependence latency.
    if (Latency >= 0)
      dep.setLatency(Latency);
  }
}

void ScheduleDAGInstrs::dumpNode(const SUnit *SU) const {
  SU->getInstr()->dump();
}

std::string ScheduleDAGInstrs::getGraphNodeLabel(const SUnit *SU) const {
  std::string s;
  raw_string_ostream oss(s);
  if (SU == &EntrySU)
    oss << "<entry>";
  else if (SU == &ExitSU)
    oss << "<exit>";
  else
    SU->getInstr()->print(oss);
  return oss.str();
}

/// Return the basic block label. It is not necessarilly unique because a block
/// contains multiple scheduling regions. But it is fine for visualization.
std::string ScheduleDAGInstrs::getDAGName() const {
  return "dag." + BB->getFullName();
}
