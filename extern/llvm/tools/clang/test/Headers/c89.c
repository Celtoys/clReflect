// RUN: %clang -ccc-host-triple i386-apple-darwin10 -fsyntax-only -Xclang -verify -std=c89 %s

// FIXME: Disable inclusion of mm_malloc.h, our current implementation is broken
// on win32 since we don't generally know how to find errno.h.

#define __MM_MALLOC_H

// PR6658
#include <xmmintrin.h>

