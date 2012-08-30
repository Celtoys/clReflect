//===- Transforms/Instrumentation.h - Instrumentation passes ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines constructor functions for instrumentation passes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_H

namespace llvm {

class ModulePass;
class FunctionPass;

// Insert edge profiling instrumentation
ModulePass *createEdgeProfilerPass();

// Insert optimal edge profiling instrumentation
ModulePass *createOptimalEdgeProfilerPass();

// Insert path profiling instrumentation
ModulePass *createPathProfilerPass();

// Insert GCOV profiling instrumentation
ModulePass *createGCOVProfilerPass(bool EmitNotes = true, bool EmitData = true,
                                   bool Use402Format = false,
                                   bool UseExtraChecksum = false);

// Insert AddressSanitizer (address sanity checking) instrumentation
ModulePass *createAddressSanitizerPass();
// Insert ThreadSanitizer (race detection) instrumentation
FunctionPass *createThreadSanitizerPass();

} // End llvm namespace

#endif
