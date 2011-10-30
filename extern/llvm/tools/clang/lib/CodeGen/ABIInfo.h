//===----- ABIInfo.h - ABI information access & encapsulation ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CODEGEN_ABIINFO_H
#define CLANG_CODEGEN_ABIINFO_H

#include "clang/AST/Type.h"
#include "llvm/Type.h"

namespace llvm {
  class Value;
  class LLVMContext;
  class TargetData;
}

namespace clang {
  class ASTContext;

  namespace CodeGen {
    class CGFunctionInfo;
    class CodeGenFunction;
    class CodeGenTypes;
  }

  // FIXME: All of this stuff should be part of the target interface
  // somehow. It is currently here because it is not clear how to factor
  // the targets to support this, since the Targets currently live in a
  // layer below types n'stuff.

  /// ABIArgInfo - Helper class to encapsulate information about how a
  /// specific C type should be passed to or returned from a function.
  class ABIArgInfo {
  public:
    enum Kind {
      /// Direct - Pass the argument directly using the normal converted LLVM
      /// type, or by coercing to another specified type stored in
      /// 'CoerceToType').  If an offset is specified (in UIntData), then the
      /// argument passed is offset by some number of bytes in the memory
      /// representation.
      Direct,

      /// Extend - Valid only for integer argument types. Same as 'direct'
      /// but also emit a zero/sign extension attribute.
      Extend,

      /// Indirect - Pass the argument indirectly via a hidden pointer
      /// with the specified alignment (0 indicates default alignment).
      Indirect,

      /// Ignore - Ignore the argument (treat as void). Useful for void and
      /// empty structs.
      Ignore,

      /// Expand - Only valid for aggregate argument types. The structure should
      /// be expanded into consecutive arguments for its constituent fields.
      /// Currently expand is only allowed on structures whose fields
      /// are all scalar types or are themselves expandable types.
      Expand,

      KindFirst=Direct, KindLast=Expand
    };

  private:
    Kind TheKind;
    llvm::Type *TypeData;
    unsigned UIntData;
    bool BoolData0;
    bool BoolData1;

    ABIArgInfo(Kind K, llvm::Type *TD=0,
               unsigned UI=0, bool B0 = false, bool B1 = false)
      : TheKind(K), TypeData(TD), UIntData(UI), BoolData0(B0), BoolData1(B1) {}

  public:
    ABIArgInfo() : TheKind(Direct), TypeData(0), UIntData(0) {}

    static ABIArgInfo getDirect(llvm::Type *T = 0, unsigned Offset = 0) {
      return ABIArgInfo(Direct, T, Offset);
    }
    static ABIArgInfo getExtend(llvm::Type *T = 0) {
      return ABIArgInfo(Extend, T, 0);
    }
    static ABIArgInfo getIgnore() {
      return ABIArgInfo(Ignore);
    }
    static ABIArgInfo getIndirect(unsigned Alignment, bool ByVal = true
                                  , bool Realign = false) {
      return ABIArgInfo(Indirect, 0, Alignment, ByVal, Realign);
    }
    static ABIArgInfo getExpand() {
      return ABIArgInfo(Expand);
    }

    Kind getKind() const { return TheKind; }
    bool isDirect() const { return TheKind == Direct; }
    bool isExtend() const { return TheKind == Extend; }
    bool isIgnore() const { return TheKind == Ignore; }
    bool isIndirect() const { return TheKind == Indirect; }
    bool isExpand() const { return TheKind == Expand; }

    bool canHaveCoerceToType() const {
      return TheKind == Direct || TheKind == Extend;
    }

    // Direct/Extend accessors
    unsigned getDirectOffset() const {
      assert((isDirect() || isExtend()) && "Not a direct or extend kind");
      return UIntData;
    }
    llvm::Type *getCoerceToType() const {
      assert(canHaveCoerceToType() && "Invalid kind!");
      return TypeData;
    }

    void setCoerceToType(llvm::Type *T) {
      assert(canHaveCoerceToType() && "Invalid kind!");
      TypeData = T;
    }

    // Indirect accessors
    unsigned getIndirectAlign() const {
      assert(TheKind == Indirect && "Invalid kind!");
      return UIntData;
    }

    bool getIndirectByVal() const {
      assert(TheKind == Indirect && "Invalid kind!");
      return BoolData0;
    }

    bool getIndirectRealign() const {
      assert(TheKind == Indirect && "Invalid kind!");
      return BoolData1;
    }

    void dump() const;
  };

  /// ABIInfo - Target specific hooks for defining how a type should be
  /// passed or returned from functions.
  class ABIInfo {
  public:
    CodeGen::CodeGenTypes &CGT;

    ABIInfo(CodeGen::CodeGenTypes &cgt) : CGT(cgt) {}
    virtual ~ABIInfo();

    ASTContext &getContext() const;
    llvm::LLVMContext &getVMContext() const;
    const llvm::TargetData &getTargetData() const;

    virtual void computeInfo(CodeGen::CGFunctionInfo &FI) const = 0;

    /// EmitVAArg - Emit the target dependent code to load a value of
    /// \arg Ty from the va_list pointed to by \arg VAListAddr.

    // FIXME: This is a gaping layering violation if we wanted to drop
    // the ABI information any lower than CodeGen. Of course, for
    // VAArg handling it has to be at this level; there is no way to
    // abstract this out.
    virtual llvm::Value *EmitVAArg(llvm::Value *VAListAddr, QualType Ty,
                                   CodeGen::CodeGenFunction &CGF) const = 0;
  };
}  // end namespace clang

#endif
