//===-- MipsMachineFunctionInfo.cpp - Private data used for Mips ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MipsMachineFunction.h"
#include "MipsInstrInfo.h"
#include "MipsSubtarget.h"
#include "MCTargetDesc/MipsBaseInfo.h"
#include "llvm/Function.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool>
FixGlobalBaseReg("mips-fix-global-base-reg", cl::Hidden, cl::init(true),
                 cl::desc("Always use $gp as the global base register."));

bool MipsFunctionInfo::globalBaseRegFixed() const {
  return FixGlobalBaseReg;
}

bool MipsFunctionInfo::globalBaseRegSet() const {
  return GlobalBaseReg;
}

unsigned MipsFunctionInfo::getGlobalBaseReg() {
  // Return if it has already been initialized.
  if (GlobalBaseReg)
    return GlobalBaseReg;

  const MipsSubtarget &ST = MF.getTarget().getSubtarget<MipsSubtarget>();

  if (FixGlobalBaseReg) // $gp is the global base register.
    return GlobalBaseReg = ST.isABI_N64() ? Mips::GP_64 : Mips::GP;

  const TargetRegisterClass *RC;
  RC = ST.isABI_N64() ?
    Mips::CPU64RegsRegisterClass : Mips::CPURegsRegisterClass;

  return GlobalBaseReg = MF.getRegInfo().createVirtualRegister(RC);
}

void MipsFunctionInfo::anchor() { }
