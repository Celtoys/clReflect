//===--- CodeGenTBAA.h - TBAA information for LLVM CodeGen ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the code that manages TBAA information and defines the TBAA policy
// for the optimizer to use.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CODEGEN_CODEGENTBAA_H
#define CLANG_CODEGEN_CODEGENTBAA_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/MDBuilder.h"

namespace llvm {
  class LLVMContext;
  class MDNode;
}

namespace clang {
  class ASTContext;
  class LangOptions;
  class MangleContext;
  class QualType;
  class Type;

namespace CodeGen {
  class CGRecordLayout;

/// CodeGenTBAA - This class organizes the cross-module state that is used
/// while lowering AST types to LLVM types.
class CodeGenTBAA {
  ASTContext &Context;
  llvm::LLVMContext& VMContext;
  const LangOptions &Features;
  MangleContext &MContext;

  // MDHelper - Helper for creating metadata.
  llvm::MDBuilder MDHelper;

  /// MetadataCache - This maps clang::Types to llvm::MDNodes describing them.
  llvm::DenseMap<const Type *, llvm::MDNode *> MetadataCache;

  llvm::MDNode *Root;
  llvm::MDNode *Char;

  /// getRoot - This is the mdnode for the root of the metadata type graph
  /// for this translation unit.
  llvm::MDNode *getRoot();

  /// getChar - This is the mdnode for "char", which is special, and any types
  /// considered to be equivalent to it.
  llvm::MDNode *getChar();

public:
  CodeGenTBAA(ASTContext &Ctx, llvm::LLVMContext &VMContext,
              const LangOptions &Features,
              MangleContext &MContext);
  ~CodeGenTBAA();

  /// getTBAAInfo - Get the TBAA MDNode to be used for a dereference
  /// of the given type.
  llvm::MDNode *getTBAAInfo(QualType QTy);

  /// getTBAAInfoForVTablePtr - Get the TBAA MDNode to be used for a
  /// dereference of a vtable pointer.
  llvm::MDNode *getTBAAInfoForVTablePtr();
};

}  // end namespace CodeGen
}  // end namespace clang

#endif
