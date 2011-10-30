//===--- Attr.h - Classes for representing expressions ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Attr interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ATTR_H
#define LLVM_CLANG_AST_ATTR_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/AttrKinds.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/VersionTuple.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstring>
#include <algorithm>

namespace clang {
  class ASTContext;
  class IdentifierInfo;
  class ObjCInterfaceDecl;
  class Expr;
  class QualType;
  class FunctionDecl;
  class TypeSourceInfo;
}

// Defined in ASTContext.h
void *operator new(size_t Bytes, const clang::ASTContext &C,
                   size_t Alignment = 16) throw ();
// FIXME: Being forced to not have a default argument here due to redeclaration
//        rules on default arguments sucks
void *operator new[](size_t Bytes, const clang::ASTContext &C,
                     size_t Alignment) throw ();

// It is good practice to pair new/delete operators.  Also, MSVC gives many
// warnings if a matching delete overload is not declared, even though the
// throw() spec guarantees it will not be implicitly called.
void operator delete(void *Ptr, const clang::ASTContext &C, size_t)
              throw ();
void operator delete[](void *Ptr, const clang::ASTContext &C, size_t)
              throw ();

namespace clang {

/// Attr - This represents one attribute.
class Attr {
private:
  SourceRange Range;
  unsigned AttrKind : 16;

protected:
  bool Inherited : 1;

  virtual ~Attr();
  
  void* operator new(size_t bytes) throw() {
    llvm_unreachable("Attrs cannot be allocated with regular 'new'.");
  }
  void operator delete(void* data) throw() {
    llvm_unreachable("Attrs cannot be released with regular 'delete'.");
  }

public:
  // Forward so that the regular new and delete do not hide global ones.
  void* operator new(size_t Bytes, ASTContext &C,
                     size_t Alignment = 16) throw() {
    return ::operator new(Bytes, C, Alignment);
  }
  void operator delete(void *Ptr, ASTContext &C,
                       size_t Alignment) throw() {
    return ::operator delete(Ptr, C, Alignment);
  }

protected:
  Attr(attr::Kind AK, SourceRange R)
    : Range(R), AttrKind(AK), Inherited(false) {}

public:

  attr::Kind getKind() const {
    return static_cast<attr::Kind>(AttrKind);
  }

  SourceLocation getLocation() const { return Range.getBegin(); }
  SourceRange getRange() const { return Range; }
  void setRange(SourceRange R) { Range = R; }

  bool isInherited() const { return Inherited; }

  // Clone this attribute.
  virtual Attr* clone(ASTContext &C) const = 0;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *) { return true; }
};

class InheritableAttr : public Attr {
protected:
  InheritableAttr(attr::Kind AK, SourceRange R)
    : Attr(AK, R) {}

public:
  void setInherited(bool I) { Inherited = I; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() <= attr::LAST_INHERITABLE;
  }
  static bool classof(const InheritableAttr *) { return true; }
};

class InheritableParamAttr : public InheritableAttr {
protected:
  InheritableParamAttr(attr::Kind AK, SourceRange R)
    : InheritableAttr(AK, R) {}

public:
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() <= attr::LAST_INHERITABLE_PARAM;
  }
  static bool classof(const InheritableParamAttr *) { return true; }
};

#include "clang/AST/Attrs.inc"

/// AttrVec - A vector of Attr, which is how they are stored on the AST.
typedef SmallVector<Attr*, 2> AttrVec;
typedef SmallVector<const Attr*, 2> ConstAttrVec;

/// DestroyAttrs - Destroy the contents of an AttrVec.
inline void DestroyAttrs (AttrVec& V, ASTContext &C) {
}

/// specific_attr_iterator - Iterates over a subrange of an AttrVec, only
/// providing attributes that are of a specifc type.
template <typename SpecificAttr>
class specific_attr_iterator {
  /// Current - The current, underlying iterator.
  /// In order to ensure we don't dereference an invalid iterator unless
  /// specifically requested, we don't necessarily advance this all the
  /// way. Instead, we advance it when an operation is requested; if the
  /// operation is acting on what should be a past-the-end iterator,
  /// then we offer no guarantees, but this way we do not dererence a
  /// past-the-end iterator when we move to a past-the-end position.
  mutable AttrVec::const_iterator Current;

  void AdvanceToNext() const {
    while (!isa<SpecificAttr>(*Current))
      ++Current;
  }

  void AdvanceToNext(AttrVec::const_iterator I) const {
    while (Current != I && !isa<SpecificAttr>(*Current))
      ++Current;
  }

public:
  typedef SpecificAttr*             value_type;
  typedef SpecificAttr*             reference;
  typedef SpecificAttr*             pointer;
  typedef std::forward_iterator_tag iterator_category;
  typedef std::ptrdiff_t            difference_type;

  specific_attr_iterator() : Current() { }
  explicit specific_attr_iterator(AttrVec::const_iterator i) : Current(i) { }

  reference operator*() const {
    AdvanceToNext();
    return cast<SpecificAttr>(*Current);
  }
  pointer operator->() const {
    AdvanceToNext();
    return cast<SpecificAttr>(*Current);
  }

  specific_attr_iterator& operator++() {
    ++Current;
    return *this;
  }
  specific_attr_iterator operator++(int) {
    specific_attr_iterator Tmp(*this);
    ++(*this);
    return Tmp;
  }

  friend bool operator==(specific_attr_iterator Left,
                         specific_attr_iterator Right) {
    if (Left.Current < Right.Current)
      Left.AdvanceToNext(Right.Current); 
    else
      Right.AdvanceToNext(Left.Current);
    return Left.Current == Right.Current;
  }
  friend bool operator!=(specific_attr_iterator Left,
                         specific_attr_iterator Right) {
    return !(Left == Right);
  }
};

template <typename T>
inline specific_attr_iterator<T> specific_attr_begin(const AttrVec& vec) {
  return specific_attr_iterator<T>(vec.begin());
}
template <typename T>
inline specific_attr_iterator<T> specific_attr_end(const AttrVec& vec) {
  return specific_attr_iterator<T>(vec.end());
}

template <typename T>
inline bool hasSpecificAttr(const AttrVec& vec) {
  return specific_attr_begin<T>(vec) != specific_attr_end<T>(vec);
}
template <typename T>
inline T *getSpecificAttr(const AttrVec& vec) {
  specific_attr_iterator<T> i = specific_attr_begin<T>(vec);
  if (i != specific_attr_end<T>(vec))
    return *i;
  else
    return 0;
}

/// getMaxAlignment - Returns the highest alignment value found among
/// AlignedAttrs in an AttrVec, or 0 if there are none.
inline unsigned getMaxAttrAlignment(const AttrVec& V, ASTContext &Ctx) {
  unsigned Align = 0;
  specific_attr_iterator<AlignedAttr> i(V.begin()), e(V.end());
  for(; i != e; ++i)
    Align = std::max(Align, i->getAlignment(Ctx));
  return Align;
}

}  // end namespace clang

#endif
