//===-- RuntimeDyld.h - Run-time dynamic linker for MC-JIT ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Interface for the runtime dynamic linker facilities of the MC-JIT.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_RUNTIME_DYLD_H
#define LLVM_RUNTIME_DYLD_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Memory.h"

namespace llvm {

class RuntimeDyldImpl;
class MemoryBuffer;

// RuntimeDyld clients often want to handle the memory management of
// what gets placed where. For JIT clients, this is an abstraction layer
// over the JITMemoryManager, which references objects by their source
// representations in LLVM IR.
// FIXME: As the RuntimeDyld fills out, additional routines will be needed
//        for the varying types of objects to be allocated.
class RTDyldMemoryManager {
  RTDyldMemoryManager(const RTDyldMemoryManager&);  // DO NOT IMPLEMENT
  void operator=(const RTDyldMemoryManager&);       // DO NOT IMPLEMENT
public:
  RTDyldMemoryManager() {}
  virtual ~RTDyldMemoryManager();

  /// allocateCodeSection - Allocate a memory block of (at least) the given
  /// size suitable for executable code.
  virtual uint8_t *allocateCodeSection(uintptr_t Size, unsigned Alignment,
                                       unsigned SectionID) = 0;

  /// allocateDataSection - Allocate a memory block of (at least) the given
  /// size suitable for data.
  virtual uint8_t *allocateDataSection(uintptr_t Size, unsigned Alignment,
                                       unsigned SectionID) = 0;

  virtual void *getPointerToNamedFunction(const std::string &Name,
                                          bool AbortOnFailure = true) = 0;
};

class RuntimeDyld {
  RuntimeDyld(const RuntimeDyld &);     // DO NOT IMPLEMENT
  void operator=(const RuntimeDyld &);  // DO NOT IMPLEMENT

  // RuntimeDyldImpl is the actual class. RuntimeDyld is just the public
  // interface.
  RuntimeDyldImpl *Dyld;
  RTDyldMemoryManager *MM;
protected:
  // Change the address associated with a section when resolving relocations.
  // Any relocations already associated with the symbol will be re-resolved.
  void reassignSectionAddress(unsigned SectionID, uint64_t Addr);
public:
  RuntimeDyld(RTDyldMemoryManager*);
  ~RuntimeDyld();

  bool loadObject(MemoryBuffer *InputBuffer);
  // Get the address of our local copy of the symbol. This may or may not
  // be the address used for relocation (clients can copy the data around
  // and resolve relocatons based on where they put it).
  void *getSymbolAddress(StringRef Name);
  // Resolve the relocations for all symbols we currently know about.
  void resolveRelocations();

  /// mapSectionAddress - map a section to its target address space value.
  /// Map the address of a JIT section as returned from the memory manager
  /// to the address in the target process as the running code will see it.
  /// This is the address which will be used for relocation resolution.
  void mapSectionAddress(void *LocalAddress, uint64_t TargetAddress);

  StringRef getErrorString();
};

} // end namespace llvm

#endif
