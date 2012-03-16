
//
// ===============================================================================
// clReflect, Platform.h - Platform-independent definitions required by the
// runtime C++ API.
// -------------------------------------------------------------------------------
// Copyright (c) 2011 Don Williamson
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// ===============================================================================
//


#pragma once

// cross platform type definitions
#ifdef _MSC_VER

typedef unsigned int size_type;

typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;

#else

typedef unsigned long size_type;

typedef long long int64_t;
typedef unsigned long long uint64_t;

#endif // _MSC_VER


#ifdef __GNUC__

// The offsetof macro of g++ version does not work with pointer to members.
//
// First g++ would expand
// offset(type, field)
// macro into:
// ((size_t)(&((type *)0)-> field))
//
// Note that with g++ whose version is later than 3.5, offsetof would actually
// expand to __builtin_offsetof, the behaviour remains the same
//
// So if we feed in data like following(example taken from line 86
// in DatabaseMetadata.h):
//
// offsetof(CONTAINER_TYPE, *member)
// It would expand to:
// ((size_t)(&((CONTAINER_TYPE *)0)-> *member))
//
// First, with default macro expansion rule, g++ would add a space between
// "->" and "*member", causing "->*" operator to become two operators, thus
// breaking our code
// Second, even if we eliminate the space some how, according to c++ operator
// precedence, & is first evaluated on ((CONTAINER_TYPE *)0), then the result
// is evaluated on ->*member, which is the wrong order.
//
// Considering all these cases, we provide a custom offsetof macro here which
// is compatible with pointer to member given our requirements.

#define POINTER_OFFSETOF(type, field) ((size_t)(&(((type *)0)->##field)))

#else

// For MSVC, we can just use official offsetof macro
#define POINTER_OFFSETOF(type, field) offsetof(type, field)

#endif  /* __GNUC__ */
