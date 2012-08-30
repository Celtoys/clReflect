//===-- DeclFriend.h - Classes for C++ friend declarations -*- C++ -*------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the section of the AST representing C++ friend
// declarations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLFRIEND_H
#define LLVM_CLANG_AST_DECLFRIEND_H

#include "clang/AST/DeclCXX.h"
#include "llvm/Support/Compiler.h"

namespace clang {

/// FriendDecl - Represents the declaration of a friend entity,
/// which can be a function, a type, or a templated function or type.
//  For example:
///
/// @code
/// template <typename T> class A {
///   friend int foo(T);
///   friend class B;
///   friend T; // only in C++0x
///   template <typename U> friend class C;
///   template <typename U> friend A& operator+=(A&, const U&) { ... }
/// };
/// @endcode
///
/// The semantic context of a friend decl is its declaring class.
class FriendDecl : public Decl {
  virtual void anchor();
public:
  typedef llvm::PointerUnion<NamedDecl*,TypeSourceInfo*> FriendUnion;

private:
  // The declaration that's a friend of this class.
  FriendUnion Friend;

  // A pointer to the next friend in the sequence.
  LazyDeclPtr NextFriend;

  // Location of the 'friend' specifier.
  SourceLocation FriendLoc;

  /// True if this 'friend' declaration is unsupported.  Eventually we
  /// will support every possible friend declaration, but for now we
  /// silently ignore some and set this flag to authorize all access.
  bool UnsupportedFriend;

  friend class CXXRecordDecl::friend_iterator;
  friend class CXXRecordDecl;

  FriendDecl(DeclContext *DC, SourceLocation L, FriendUnion Friend,
             SourceLocation FriendL)
    : Decl(Decl::Friend, DC, L),
      Friend(Friend),
      NextFriend(),
      FriendLoc(FriendL),
      UnsupportedFriend(false) {
  }

  explicit FriendDecl(EmptyShell Empty)
    : Decl(Decl::Friend, Empty), NextFriend() { }

  FriendDecl *getNextFriend() {
    return cast_or_null<FriendDecl>(
                          NextFriend.get(getASTContext().getExternalSource()));
  }
  
public:
  static FriendDecl *Create(ASTContext &C, DeclContext *DC,
                            SourceLocation L, FriendUnion Friend_,
                            SourceLocation FriendL);
  static FriendDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  /// If this friend declaration names an (untemplated but possibly
  /// dependent) type, return the type; otherwise return null.  This
  /// is used for elaborated-type-specifiers and, in C++0x, for
  /// arbitrary friend type declarations.
  TypeSourceInfo *getFriendType() const {
    return Friend.dyn_cast<TypeSourceInfo*>();
  }

  /// If this friend declaration doesn't name a type, return the inner
  /// declaration.
  NamedDecl *getFriendDecl() const {
    return Friend.dyn_cast<NamedDecl*>();
  }

  /// Retrieves the location of the 'friend' keyword.
  SourceLocation getFriendLoc() const {
    return FriendLoc;
  }

  /// Retrieves the source range for the friend declaration.
  SourceRange getSourceRange() const LLVM_READONLY {
    /* FIXME: consider the case of templates wrt start of range. */
    if (NamedDecl *ND = getFriendDecl())
      return SourceRange(getFriendLoc(), ND->getLocEnd());
    else if (TypeSourceInfo *TInfo = getFriendType())
      return SourceRange(getFriendLoc(), TInfo->getTypeLoc().getEndLoc());
    else
      return SourceRange(getFriendLoc(), getLocation());
  }

  /// Determines if this friend kind is unsupported.
  bool isUnsupportedFriend() const {
    return UnsupportedFriend;
  }
  void setUnsupportedFriend(bool Unsupported) {
    UnsupportedFriend = Unsupported;
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classof(const FriendDecl *D) { return true; }
  static bool classofKind(Kind K) { return K == Decl::Friend; }

  friend class ASTDeclReader;
  friend class ASTDeclWriter;
};

/// An iterator over the friend declarations of a class.
class CXXRecordDecl::friend_iterator {
  FriendDecl *Ptr;

  friend class CXXRecordDecl;
  explicit friend_iterator(FriendDecl *Ptr) : Ptr(Ptr) {}
public:
  friend_iterator() {}

  typedef FriendDecl *value_type;
  typedef FriendDecl *reference;
  typedef FriendDecl *pointer;
  typedef int difference_type;
  typedef std::forward_iterator_tag iterator_category;

  reference operator*() const { return Ptr; }

  friend_iterator &operator++() {
    assert(Ptr && "attempt to increment past end of friend list");
    Ptr = Ptr->getNextFriend();
    return *this;
  }

  friend_iterator operator++(int) {
    friend_iterator tmp = *this;
    ++*this;
    return tmp;
  }

  bool operator==(const friend_iterator &Other) const {
    return Ptr == Other.Ptr;
  }

  bool operator!=(const friend_iterator &Other) const {
    return Ptr != Other.Ptr;
  }

  friend_iterator &operator+=(difference_type N) {
    assert(N >= 0 && "cannot rewind a CXXRecordDecl::friend_iterator");
    while (N--)
      ++*this;
    return *this;
  }

  friend_iterator operator+(difference_type N) const {
    friend_iterator tmp = *this;
    tmp += N;
    return tmp;
  }
};

inline CXXRecordDecl::friend_iterator CXXRecordDecl::friend_begin() const {
  return friend_iterator(data().FirstFriend);
}

inline CXXRecordDecl::friend_iterator CXXRecordDecl::friend_end() const {
  return friend_iterator(0);
}

inline void CXXRecordDecl::pushFriendDecl(FriendDecl *FD) {
  assert(FD->NextFriend == 0 && "friend already has next friend?");
  FD->NextFriend = data().FirstFriend;
  data().FirstFriend = FD;
}
  
}

#endif
