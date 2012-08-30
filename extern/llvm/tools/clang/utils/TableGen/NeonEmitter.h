//===- NeonEmitter.h - Generate arm_neon.h for use with clang ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend is responsible for emitting arm_neon.h, which includes
// a declaration and definition of each function specified by the ARM NEON
// compiler interface.  See ARM document DUI0348B.
//
//===----------------------------------------------------------------------===//

#ifndef NEON_EMITTER_H
#define NEON_EMITTER_H

#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"

enum OpKind {
  OpNone,
  OpAdd,
  OpAddl,
  OpAddw,
  OpSub,
  OpSubl,
  OpSubw,
  OpMul,
  OpMla,
  OpMlal,
  OpMls,
  OpMlsl,
  OpMulN,
  OpMlaN,
  OpMlsN,
  OpMlalN,
  OpMlslN,
  OpMulLane,
  OpMullLane,
  OpMlaLane,
  OpMlsLane,
  OpMlalLane,
  OpMlslLane,
  OpQDMullLane,
  OpQDMlalLane,
  OpQDMlslLane,
  OpQDMulhLane,
  OpQRDMulhLane,
  OpEq,
  OpGe,
  OpLe,
  OpGt,
  OpLt,
  OpNeg,
  OpNot,
  OpAnd,
  OpOr,
  OpXor,
  OpAndNot,
  OpOrNot,
  OpCast,
  OpConcat,
  OpDup,
  OpDupLane,
  OpHi,
  OpLo,
  OpSelect,
  OpRev16,
  OpRev32,
  OpRev64,
  OpReinterpret,
  OpAbdl,
  OpAba,
  OpAbal
};

enum ClassKind {
  ClassNone,
  ClassI,           // generic integer instruction, e.g., "i8" suffix
  ClassS,           // signed/unsigned/poly, e.g., "s8", "u8" or "p8" suffix
  ClassW,           // width-specific instruction, e.g., "8" suffix
  ClassB            // bitcast arguments with enum argument to specify type
};

/// NeonTypeFlags - Flags to identify the types for overloaded Neon
/// builtins.  These must be kept in sync with the flags in
/// include/clang/Basic/TargetBuiltins.h.
class NeonTypeFlags {
  enum {
    EltTypeMask = 0xf,
    UnsignedFlag = 0x10,
    QuadFlag = 0x20
  };
  uint32_t Flags;

public:
  enum EltType {
    Int8,
    Int16,
    Int32,
    Int64,
    Poly8,
    Poly16,
    Float16,
    Float32
  };

  NeonTypeFlags(unsigned F) : Flags(F) {}
  NeonTypeFlags(EltType ET, bool IsUnsigned, bool IsQuad) : Flags(ET) {
    if (IsUnsigned)
      Flags |= UnsignedFlag;
    if (IsQuad)
      Flags |= QuadFlag;
  }

  uint32_t getFlags() const { return Flags; }
};

namespace llvm {

  class NeonEmitter : public TableGenBackend {
    RecordKeeper &Records;
    StringMap<OpKind> OpMap;
    DenseMap<Record*, ClassKind> ClassMap;

  public:
    NeonEmitter(RecordKeeper &R) : Records(R) {
      OpMap["OP_NONE"]  = OpNone;
      OpMap["OP_ADD"]   = OpAdd;
      OpMap["OP_ADDL"]  = OpAddl;
      OpMap["OP_ADDW"]  = OpAddw;
      OpMap["OP_SUB"]   = OpSub;
      OpMap["OP_SUBL"]  = OpSubl;
      OpMap["OP_SUBW"]  = OpSubw;
      OpMap["OP_MUL"]   = OpMul;
      OpMap["OP_MLA"]   = OpMla;
      OpMap["OP_MLAL"]  = OpMlal;
      OpMap["OP_MLS"]   = OpMls;
      OpMap["OP_MLSL"]  = OpMlsl;
      OpMap["OP_MUL_N"] = OpMulN;
      OpMap["OP_MLA_N"] = OpMlaN;
      OpMap["OP_MLS_N"] = OpMlsN;
      OpMap["OP_MLAL_N"] = OpMlalN;
      OpMap["OP_MLSL_N"] = OpMlslN;
      OpMap["OP_MUL_LN"]= OpMulLane;
      OpMap["OP_MULL_LN"] = OpMullLane;
      OpMap["OP_MLA_LN"]= OpMlaLane;
      OpMap["OP_MLS_LN"]= OpMlsLane;
      OpMap["OP_MLAL_LN"] = OpMlalLane;
      OpMap["OP_MLSL_LN"] = OpMlslLane;
      OpMap["OP_QDMULL_LN"] = OpQDMullLane;
      OpMap["OP_QDMLAL_LN"] = OpQDMlalLane;
      OpMap["OP_QDMLSL_LN"] = OpQDMlslLane;
      OpMap["OP_QDMULH_LN"] = OpQDMulhLane;
      OpMap["OP_QRDMULH_LN"] = OpQRDMulhLane;
      OpMap["OP_EQ"]    = OpEq;
      OpMap["OP_GE"]    = OpGe;
      OpMap["OP_LE"]    = OpLe;
      OpMap["OP_GT"]    = OpGt;
      OpMap["OP_LT"]    = OpLt;
      OpMap["OP_NEG"]   = OpNeg;
      OpMap["OP_NOT"]   = OpNot;
      OpMap["OP_AND"]   = OpAnd;
      OpMap["OP_OR"]    = OpOr;
      OpMap["OP_XOR"]   = OpXor;
      OpMap["OP_ANDN"]  = OpAndNot;
      OpMap["OP_ORN"]   = OpOrNot;
      OpMap["OP_CAST"]  = OpCast;
      OpMap["OP_CONC"]  = OpConcat;
      OpMap["OP_HI"]    = OpHi;
      OpMap["OP_LO"]    = OpLo;
      OpMap["OP_DUP"]   = OpDup;
      OpMap["OP_DUP_LN"] = OpDupLane;
      OpMap["OP_SEL"]   = OpSelect;
      OpMap["OP_REV16"] = OpRev16;
      OpMap["OP_REV32"] = OpRev32;
      OpMap["OP_REV64"] = OpRev64;
      OpMap["OP_REINT"] = OpReinterpret;
      OpMap["OP_ABDL"]  = OpAbdl;
      OpMap["OP_ABA"]   = OpAba;
      OpMap["OP_ABAL"]  = OpAbal;

      Record *SI = R.getClass("SInst");
      Record *II = R.getClass("IInst");
      Record *WI = R.getClass("WInst");
      ClassMap[SI] = ClassS;
      ClassMap[II] = ClassI;
      ClassMap[WI] = ClassW;
    }

    // run - Emit arm_neon.h.inc
    void run(raw_ostream &o);

    // runHeader - Emit all the __builtin prototypes used in arm_neon.h
    void runHeader(raw_ostream &o);

    // runTests - Emit tests for all the Neon intrinsics.
    void runTests(raw_ostream &o);

  private:
    void emitIntrinsic(raw_ostream &OS, Record *R);
  };

} // End llvm namespace

#endif
