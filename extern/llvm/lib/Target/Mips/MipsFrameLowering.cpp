//===-- MipsFrameLowering.cpp - Mips Frame Information --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mips implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "MipsFrameLowering.h"
#include "MipsAnalyzeImmediate.h"
#include "MipsInstrInfo.h"
#include "MipsMachineFunction.h"
#include "MCTargetDesc/MipsBaseInfo.h"
#include "llvm/Function.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;


//===----------------------------------------------------------------------===//
//
// Stack Frame Processing methods
// +----------------------------+
//
// The stack is allocated decrementing the stack pointer on
// the first instruction of a function prologue. Once decremented,
// all stack references are done thought a positive offset
// from the stack/frame pointer, so the stack is considering
// to grow up! Otherwise terrible hacks would have to be made
// to get this stack ABI compliant :)
//
//  The stack frame required by the ABI (after call):
//  Offset
//
//  0                 ----------
//  4                 Args to pass
//  .                 saved $GP  (used in PIC)
//  .                 Alloca allocations
//  .                 Local Area
//  .                 CPU "Callee Saved" Registers
//  .                 saved FP
//  .                 saved RA
//  .                 FPU "Callee Saved" Registers
//  StackSize         -----------
//
// Offset - offset from sp after stack allocation on function prologue
//
// The sp is the stack pointer subtracted/added from the stack size
// at the Prologue/Epilogue
//
// References to the previous stack (to obtain arguments) are done
// with offsets that exceeds the stack size: (stacksize+(4*(num_arg-1))
//
// Examples:
// - reference to the actual stack frame
//   for any local area var there is smt like : FI >= 0, StackOffset: 4
//     sw REGX, 4(SP)
//
// - reference to previous stack frame
//   suppose there's a load to the 5th arguments : FI < 0, StackOffset: 16.
//   The emitted instruction will be something like:
//     lw REGX, 16+StackSize(SP)
//
// Since the total stack size is unknown on LowerFormalArguments, all
// stack references (ObjectOffset) created to reference the function
// arguments, are negative numbers. This way, on eliminateFrameIndex it's
// possible to detect those references and the offsets are adjusted to
// their real location.
//
//===----------------------------------------------------------------------===//

// hasFP - Return true if the specified function should have a dedicated frame
// pointer register.  This is true if the function has variable sized allocas or
// if frame pointer elimination is disabled.
bool MipsFrameLowering::hasFP(const MachineFunction &MF) const {
  const MachineFrameInfo *MFI = MF.getFrameInfo();
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
      MFI->hasVarSizedObjects() || MFI->isFrameAddressTaken();
}

bool MipsFrameLowering::targetHandlesStackFrameRounding() const {
  return true;
}

// Build an instruction sequence to load an immediate that is too large to fit
// in 16-bit and add the result to Reg.
static void expandLargeImm(unsigned Reg, int64_t Imm, bool IsN64,
                           const MipsInstrInfo &TII, MachineBasicBlock& MBB,
                           MachineBasicBlock::iterator II, DebugLoc DL) {
  unsigned LUi = IsN64 ? Mips::LUi64 : Mips::LUi;
  unsigned ADDu = IsN64 ? Mips::DADDu : Mips::ADDu;
  unsigned ZEROReg = IsN64 ? Mips::ZERO_64 : Mips::ZERO;
  unsigned ATReg = IsN64 ? Mips::AT_64 : Mips::AT;
  MipsAnalyzeImmediate AnalyzeImm;
  const MipsAnalyzeImmediate::InstSeq &Seq =
    AnalyzeImm.Analyze(Imm, IsN64 ? 64 : 32, false /* LastInstrIsADDiu */);
  MipsAnalyzeImmediate::InstSeq::const_iterator Inst = Seq.begin();

  // The first instruction can be a LUi, which is different from other
  // instructions (ADDiu, ORI and SLL) in that it does not have a register
  // operand.
  if (Inst->Opc == LUi)
    BuildMI(MBB, II, DL, TII.get(LUi), ATReg)
      .addImm(SignExtend64<16>(Inst->ImmOpnd));
  else
    BuildMI(MBB, II, DL, TII.get(Inst->Opc), ATReg).addReg(ZEROReg)
      .addImm(SignExtend64<16>(Inst->ImmOpnd));

  // Build the remaining instructions in Seq.
  for (++Inst; Inst != Seq.end(); ++Inst)
    BuildMI(MBB, II, DL, TII.get(Inst->Opc), ATReg).addReg(ATReg)
      .addImm(SignExtend64<16>(Inst->ImmOpnd));

  BuildMI(MBB, II, DL, TII.get(ADDu), Reg).addReg(Reg).addReg(ATReg);
}

void MipsFrameLowering::emitPrologue(MachineFunction &MF) const {
  MachineBasicBlock &MBB   = MF.front();
  MachineFrameInfo *MFI    = MF.getFrameInfo();
  MipsFunctionInfo *MipsFI = MF.getInfo<MipsFunctionInfo>();
  const MipsRegisterInfo *RegInfo =
    static_cast<const MipsRegisterInfo*>(MF.getTarget().getRegisterInfo());
  const MipsInstrInfo &TII =
    *static_cast<const MipsInstrInfo*>(MF.getTarget().getInstrInfo());
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc dl = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  bool isPIC = (MF.getTarget().getRelocationModel() == Reloc::PIC_);
  unsigned SP = STI.isABI_N64() ? Mips::SP_64 : Mips::SP;
  unsigned FP = STI.isABI_N64() ? Mips::FP_64 : Mips::FP;
  unsigned ZERO = STI.isABI_N64() ? Mips::ZERO_64 : Mips::ZERO;
  unsigned ADDu = STI.isABI_N64() ? Mips::DADDu : Mips::ADDu;
  unsigned ADDiu = STI.isABI_N64() ? Mips::DADDiu : Mips::ADDiu;

  // First, compute final stack size.
  unsigned RegSize = STI.isGP32bit() ? 4 : 8;
  unsigned StackAlign = getStackAlignment();
  unsigned LocalVarAreaOffset = MipsFI->needGPSaveRestore() ?
    (MFI->getObjectOffset(MipsFI->getGPFI()) + RegSize) :
    MipsFI->getMaxCallFrameSize();
  uint64_t StackSize =  RoundUpToAlignment(LocalVarAreaOffset, StackAlign) +
     RoundUpToAlignment(MFI->getStackSize(), StackAlign);

   // Update stack size
  MFI->setStackSize(StackSize);

  // Emit instructions that set the global base register if the target ABI is
  // O32.
  if (isPIC && MipsFI->globalBaseRegSet() && STI.isABI_O32() &&
      !MipsFI->globalBaseRegFixed()) {
      // See MipsInstrInfo.td for explanation.
    MachineBasicBlock *NewEntry = MF.CreateMachineBasicBlock();
    MF.insert(&MBB, NewEntry);
    NewEntry->addSuccessor(&MBB);

    // Copy live in registers.
    for (MachineBasicBlock::livein_iterator R = MBB.livein_begin();
         R != MBB.livein_end(); ++R)
      NewEntry->addLiveIn(*R);

    BuildMI(*NewEntry, NewEntry->begin(), dl, TII.get(Mips:: SETGP01),
            Mips::V0);
  }

  // No need to allocate space on the stack.
  if (StackSize == 0 && !MFI->adjustsStack()) return;

  MachineModuleInfo &MMI = MF.getMMI();
  std::vector<MachineMove> &Moves = MMI.getFrameMoves();
  MachineLocation DstML, SrcML;

  // Adjust stack.
  if (isInt<16>(-StackSize)) // addi sp, sp, (-stacksize)
    BuildMI(MBB, MBBI, dl, TII.get(ADDiu), SP).addReg(SP).addImm(-StackSize);
  else { // Expand immediate that doesn't fit in 16-bit.
    MipsFI->setEmitNOAT();
    expandLargeImm(SP, -StackSize, STI.isABI_N64(), TII, MBB, MBBI, dl);
  }

  // emit ".cfi_def_cfa_offset StackSize"
  MCSymbol *AdjustSPLabel = MMI.getContext().CreateTempSymbol();
  BuildMI(MBB, MBBI, dl,
          TII.get(TargetOpcode::PROLOG_LABEL)).addSym(AdjustSPLabel);
  DstML = MachineLocation(MachineLocation::VirtualFP);
  SrcML = MachineLocation(MachineLocation::VirtualFP, -StackSize);
  Moves.push_back(MachineMove(AdjustSPLabel, DstML, SrcML));

  const std::vector<CalleeSavedInfo> &CSI = MFI->getCalleeSavedInfo();

  if (CSI.size()) {
    // Find the instruction past the last instruction that saves a callee-saved
    // register to the stack.
    for (unsigned i = 0; i < CSI.size(); ++i)
      ++MBBI;

    // Iterate over list of callee-saved registers and emit .cfi_offset
    // directives.
    MCSymbol *CSLabel = MMI.getContext().CreateTempSymbol();
    BuildMI(MBB, MBBI, dl,
            TII.get(TargetOpcode::PROLOG_LABEL)).addSym(CSLabel);

    for (std::vector<CalleeSavedInfo>::const_iterator I = CSI.begin(),
           E = CSI.end(); I != E; ++I) {
      int64_t Offset = MFI->getObjectOffset(I->getFrameIdx());
      unsigned Reg = I->getReg();

      // If Reg is a double precision register, emit two cfa_offsets,
      // one for each of the paired single precision registers.
      if (Mips::AFGR64RegisterClass->contains(Reg)) {
        const uint16_t *SubRegs = RegInfo->getSubRegisters(Reg);
        MachineLocation DstML0(MachineLocation::VirtualFP, Offset);
        MachineLocation DstML1(MachineLocation::VirtualFP, Offset + 4);
        MachineLocation SrcML0(*SubRegs);
        MachineLocation SrcML1(*(SubRegs + 1));

        if (!STI.isLittle())
          std::swap(SrcML0, SrcML1);

        Moves.push_back(MachineMove(CSLabel, DstML0, SrcML0));
        Moves.push_back(MachineMove(CSLabel, DstML1, SrcML1));
      }
      else {
        // Reg is either in CPURegs or FGR32.
        DstML = MachineLocation(MachineLocation::VirtualFP, Offset);
        SrcML = MachineLocation(Reg);
        Moves.push_back(MachineMove(CSLabel, DstML, SrcML));
      }
    }
  }

  // if framepointer enabled, set it to point to the stack pointer.
  if (hasFP(MF)) {
    // Insert instruction "move $fp, $sp" at this location.
    BuildMI(MBB, MBBI, dl, TII.get(ADDu), FP).addReg(SP).addReg(ZERO);

    // emit ".cfi_def_cfa_register $fp"
    MCSymbol *SetFPLabel = MMI.getContext().CreateTempSymbol();
    BuildMI(MBB, MBBI, dl,
            TII.get(TargetOpcode::PROLOG_LABEL)).addSym(SetFPLabel);
    DstML = MachineLocation(FP);
    SrcML = MachineLocation(MachineLocation::VirtualFP);
    Moves.push_back(MachineMove(SetFPLabel, DstML, SrcML));
  }

  // Restore GP from the saved stack location
  if (MipsFI->needGPSaveRestore()) {
    unsigned Offset = MFI->getObjectOffset(MipsFI->getGPFI());
    BuildMI(MBB, MBBI, dl, TII.get(Mips::CPRESTORE)).addImm(Offset)
      .addReg(Mips::GP);
  }
}

void MipsFrameLowering::emitEpilogue(MachineFunction &MF,
                                 MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  MachineFrameInfo *MFI            = MF.getFrameInfo();
  const MipsInstrInfo &TII =
    *static_cast<const MipsInstrInfo*>(MF.getTarget().getInstrInfo());
  DebugLoc dl = MBBI->getDebugLoc();
  unsigned SP = STI.isABI_N64() ? Mips::SP_64 : Mips::SP;
  unsigned FP = STI.isABI_N64() ? Mips::FP_64 : Mips::FP;
  unsigned ZERO = STI.isABI_N64() ? Mips::ZERO_64 : Mips::ZERO;
  unsigned ADDu = STI.isABI_N64() ? Mips::DADDu : Mips::ADDu;
  unsigned ADDiu = STI.isABI_N64() ? Mips::DADDiu : Mips::ADDiu;

  // if framepointer enabled, restore the stack pointer.
  if (hasFP(MF)) {
    // Find the first instruction that restores a callee-saved register.
    MachineBasicBlock::iterator I = MBBI;

    for (unsigned i = 0; i < MFI->getCalleeSavedInfo().size(); ++i)
      --I;

    // Insert instruction "move $sp, $fp" at this location.
    BuildMI(MBB, I, dl, TII.get(ADDu), SP).addReg(FP).addReg(ZERO);
  }

  // Get the number of bytes from FrameInfo
  uint64_t StackSize = MFI->getStackSize();

  if (!StackSize)
    return;

  // Adjust stack.
  if (isInt<16>(StackSize)) // addi sp, sp, (-stacksize)
    BuildMI(MBB, MBBI, dl, TII.get(ADDiu), SP).addReg(SP).addImm(StackSize);
  else // Expand immediate that doesn't fit in 16-bit.
    expandLargeImm(SP, StackSize, STI.isABI_N64(), TII, MBB, MBBI, dl);
}

void MipsFrameLowering::
processFunctionBeforeCalleeSavedScan(MachineFunction &MF,
                                     RegScavenger *RS) const {
  MachineRegisterInfo& MRI = MF.getRegInfo();
  unsigned FP = STI.isABI_N64() ? Mips::FP_64 : Mips::FP;

  // FIXME: remove this code if register allocator can correctly mark
  //        $fp and $ra used or unused.

  // Mark $fp and $ra as used or unused.
  if (hasFP(MF))
    MRI.setPhysRegUsed(FP);

  // The register allocator might determine $ra is used after seeing
  // instruction "jr $ra", but we do not want PrologEpilogInserter to insert
  // instructions to save/restore $ra unless there is a function call.
  // To correct this, $ra is explicitly marked unused if there is no
  // function call.
  if (MF.getFrameInfo()->hasCalls())
    MRI.setPhysRegUsed(Mips::RA);
  else {
    MRI.setPhysRegUnused(Mips::RA);
    MRI.setPhysRegUnused(Mips::RA_64);
  }
}
