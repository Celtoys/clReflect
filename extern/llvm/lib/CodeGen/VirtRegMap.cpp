//===-- llvm/CodeGen/VirtRegMap.cpp - Virtual Register Map ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the VirtRegMap class.
//
// It also contains implementations of the Spiller interface, which, given a
// virtual register map and a machine function, eliminates all virtual
// references by replacing them with physical register references - adding spill
// code as necessary.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "regalloc"
#include "VirtRegMap.h"
#include "llvm/Function.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include <algorithm>
using namespace llvm;

STATISTIC(NumSpillSlots, "Number of spill slots allocated");
STATISTIC(NumIdCopies,   "Number of identity moves eliminated after rewriting");

//===----------------------------------------------------------------------===//
//  VirtRegMap implementation
//===----------------------------------------------------------------------===//

char VirtRegMap::ID = 0;

INITIALIZE_PASS(VirtRegMap, "virtregmap", "Virtual Register Map", false, false)

bool VirtRegMap::runOnMachineFunction(MachineFunction &mf) {
  MRI = &mf.getRegInfo();
  TII = mf.getTarget().getInstrInfo();
  TRI = mf.getTarget().getRegisterInfo();
  MF = &mf;

  Virt2PhysMap.clear();
  Virt2StackSlotMap.clear();
  Virt2SplitMap.clear();

  grow();
  return false;
}

void VirtRegMap::grow() {
  unsigned NumRegs = MF->getRegInfo().getNumVirtRegs();
  Virt2PhysMap.resize(NumRegs);
  Virt2StackSlotMap.resize(NumRegs);
  Virt2SplitMap.resize(NumRegs);
}

unsigned VirtRegMap::createSpillSlot(const TargetRegisterClass *RC) {
  int SS = MF->getFrameInfo()->CreateSpillStackObject(RC->getSize(),
                                                      RC->getAlignment());
  ++NumSpillSlots;
  return SS;
}

unsigned VirtRegMap::getRegAllocPref(unsigned virtReg) {
  std::pair<unsigned, unsigned> Hint = MRI->getRegAllocationHint(virtReg);
  unsigned physReg = Hint.second;
  if (TargetRegisterInfo::isVirtualRegister(physReg) && hasPhys(physReg))
    physReg = getPhys(physReg);
  if (Hint.first == 0)
    return (TargetRegisterInfo::isPhysicalRegister(physReg))
      ? physReg : 0;
  return TRI->ResolveRegAllocHint(Hint.first, physReg, *MF);
}

int VirtRegMap::assignVirt2StackSlot(unsigned virtReg) {
  assert(TargetRegisterInfo::isVirtualRegister(virtReg));
  assert(Virt2StackSlotMap[virtReg] == NO_STACK_SLOT &&
         "attempt to assign stack slot to already spilled register");
  const TargetRegisterClass* RC = MF->getRegInfo().getRegClass(virtReg);
  return Virt2StackSlotMap[virtReg] = createSpillSlot(RC);
}

void VirtRegMap::assignVirt2StackSlot(unsigned virtReg, int SS) {
  assert(TargetRegisterInfo::isVirtualRegister(virtReg));
  assert(Virt2StackSlotMap[virtReg] == NO_STACK_SLOT &&
         "attempt to assign stack slot to already spilled register");
  assert((SS >= 0 ||
          (SS >= MF->getFrameInfo()->getObjectIndexBegin())) &&
         "illegal fixed frame index");
  Virt2StackSlotMap[virtReg] = SS;
}

void VirtRegMap::rewrite(SlotIndexes *Indexes) {
  DEBUG(dbgs() << "********** REWRITE VIRTUAL REGISTERS **********\n"
               << "********** Function: "
               << MF->getFunction()->getName() << '\n');
  DEBUG(dump());
  SmallVector<unsigned, 8> SuperDeads;
  SmallVector<unsigned, 8> SuperDefs;
  SmallVector<unsigned, 8> SuperKills;
#ifndef NDEBUG
  BitVector Reserved = TRI->getReservedRegs(*MF);
#endif

  for (MachineFunction::iterator MBBI = MF->begin(), MBBE = MF->end();
       MBBI != MBBE; ++MBBI) {
    DEBUG(MBBI->print(dbgs(), Indexes));
    for (MachineBasicBlock::instr_iterator
           MII = MBBI->instr_begin(), MIE = MBBI->instr_end(); MII != MIE;) {
      MachineInstr *MI = MII;
      ++MII;

      for (MachineInstr::mop_iterator MOI = MI->operands_begin(),
           MOE = MI->operands_end(); MOI != MOE; ++MOI) {
        MachineOperand &MO = *MOI;

        // Make sure MRI knows about registers clobbered by regmasks.
        if (MO.isRegMask())
          MRI->addPhysRegsUsedFromRegMask(MO.getRegMask());

        if (!MO.isReg() || !TargetRegisterInfo::isVirtualRegister(MO.getReg()))
          continue;
        unsigned VirtReg = MO.getReg();
        unsigned PhysReg = getPhys(VirtReg);
        assert(PhysReg != NO_PHYS_REG && "Instruction uses unmapped VirtReg");
        assert(!Reserved.test(PhysReg) && "Reserved register assignment");

        // Preserve semantics of sub-register operands.
        if (MO.getSubReg()) {
          // A virtual register kill refers to the whole register, so we may
          // have to add <imp-use,kill> operands for the super-register.  A
          // partial redef always kills and redefines the super-register.
          if (MO.readsReg() && (MO.isDef() || MO.isKill()))
            SuperKills.push_back(PhysReg);

          if (MO.isDef()) {
            // The <def,undef> flag only makes sense for sub-register defs, and
            // we are substituting a full physreg.  An <imp-use,kill> operand
            // from the SuperKills list will represent the partial read of the
            // super-register.
            MO.setIsUndef(false);

            // Also add implicit defs for the super-register.
            if (MO.isDead())
              SuperDeads.push_back(PhysReg);
            else
              SuperDefs.push_back(PhysReg);
          }

          // PhysReg operands cannot have subregister indexes.
          PhysReg = TRI->getSubReg(PhysReg, MO.getSubReg());
          assert(PhysReg && "Invalid SubReg for physical register");
          MO.setSubReg(0);
        }
        // Rewrite. Note we could have used MachineOperand::substPhysReg(), but
        // we need the inlining here.
        MO.setReg(PhysReg);
      }

      // Add any missing super-register kills after rewriting the whole
      // instruction.
      while (!SuperKills.empty())
        MI->addRegisterKilled(SuperKills.pop_back_val(), TRI, true);

      while (!SuperDeads.empty())
        MI->addRegisterDead(SuperDeads.pop_back_val(), TRI, true);

      while (!SuperDefs.empty())
        MI->addRegisterDefined(SuperDefs.pop_back_val(), TRI);

      DEBUG(dbgs() << "> " << *MI);

      // Finally, remove any identity copies.
      if (MI->isIdentityCopy()) {
        ++NumIdCopies;
        if (MI->getNumOperands() == 2) {
          DEBUG(dbgs() << "Deleting identity copy.\n");
          if (Indexes)
            Indexes->removeMachineInstrFromMaps(MI);
          // It's safe to erase MI because MII has already been incremented.
          MI->eraseFromParent();
        } else {
          // Transform identity copy to a KILL to deal with subregisters.
          MI->setDesc(TII->get(TargetOpcode::KILL));
          DEBUG(dbgs() << "Identity copy: " << *MI);
        }
      }
    }
  }

  // Tell MRI about physical registers in use.
  for (unsigned Reg = 1, RegE = TRI->getNumRegs(); Reg != RegE; ++Reg)
    if (!MRI->reg_nodbg_empty(Reg))
      MRI->setPhysRegUsed(Reg);
}

void VirtRegMap::print(raw_ostream &OS, const Module* M) const {
  const TargetRegisterInfo* TRI = MF->getTarget().getRegisterInfo();
  const MachineRegisterInfo &MRI = MF->getRegInfo();

  OS << "********** REGISTER MAP **********\n";
  for (unsigned i = 0, e = MRI.getNumVirtRegs(); i != e; ++i) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(i);
    if (Virt2PhysMap[Reg] != (unsigned)VirtRegMap::NO_PHYS_REG) {
      OS << '[' << PrintReg(Reg, TRI) << " -> "
         << PrintReg(Virt2PhysMap[Reg], TRI) << "] "
         << MRI.getRegClass(Reg)->getName() << "\n";
    }
  }

  for (unsigned i = 0, e = MRI.getNumVirtRegs(); i != e; ++i) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(i);
    if (Virt2StackSlotMap[Reg] != VirtRegMap::NO_STACK_SLOT) {
      OS << '[' << PrintReg(Reg, TRI) << " -> fi#" << Virt2StackSlotMap[Reg]
         << "] " << MRI.getRegClass(Reg)->getName() << "\n";
    }
  }
  OS << '\n';
}

void VirtRegMap::dump() const {
  print(dbgs());
}
