//===--- TargetBuiltins.h - Target specific builtin IDs -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_TARGET_BUILTINS_H
#define LLVM_CLANG_BASIC_TARGET_BUILTINS_H

#include "clang/Basic/Builtins.h"
#undef PPC

namespace clang {

  /// ARM builtins
  namespace ARM {
    enum {
        LastTIBuiltin = clang::Builtin::FirstTSBuiltin-1,
#define BUILTIN(ID, TYPE, ATTRS) BI##ID,
#include "clang/Basic/BuiltinsARM.def"
        LastTSBuiltin
    };
  }

  /// PPC builtins
  namespace PPC {
    enum {
        LastTIBuiltin = clang::Builtin::FirstTSBuiltin-1,
#define BUILTIN(ID, TYPE, ATTRS) BI##ID,
#include "clang/Basic/BuiltinsPPC.def"
        LastTSBuiltin
    };
  }

  /// PTX builtins
  namespace PTX {
    enum {
        LastTIBuiltin = clang::Builtin::FirstTSBuiltin-1,
#define BUILTIN(ID, TYPE, ATTRS) BI##ID,
#include "clang/Basic/BuiltinsPTX.def"
        LastTSBuiltin
    };
  }


  /// X86 builtins
  namespace X86 {
    enum {
        LastTIBuiltin = clang::Builtin::FirstTSBuiltin-1,
#define BUILTIN(ID, TYPE, ATTRS) BI##ID,
#include "clang/Basic/BuiltinsX86.def"
        LastTSBuiltin
    };
  }

} // end namespace clang.

#endif
