//===---- SemaAccess.cpp - C++ Access Control -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides Sema routines for C++ access control semantics.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/DelayedDiagnostic.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DependentDiagnostic.h"
#include "clang/AST/ExprCXX.h"

using namespace clang;
using namespace sema;

/// A copy of Sema's enum without AR_delayed.
enum AccessResult {
  AR_accessible,
  AR_inaccessible,
  AR_dependent
};

/// SetMemberAccessSpecifier - Set the access specifier of a member.
/// Returns true on error (when the previous member decl access specifier
/// is different from the new member decl access specifier).
bool Sema::SetMemberAccessSpecifier(NamedDecl *MemberDecl,
                                    NamedDecl *PrevMemberDecl,
                                    AccessSpecifier LexicalAS) {
  if (!PrevMemberDecl) {
    // Use the lexical access specifier.
    MemberDecl->setAccess(LexicalAS);
    return false;
  }

  // C++ [class.access.spec]p3: When a member is redeclared its access
  // specifier must be same as its initial declaration.
  if (LexicalAS != AS_none && LexicalAS != PrevMemberDecl->getAccess()) {
    Diag(MemberDecl->getLocation(),
         diag::err_class_redeclared_with_different_access)
      << MemberDecl << LexicalAS;
    Diag(PrevMemberDecl->getLocation(), diag::note_previous_access_declaration)
      << PrevMemberDecl << PrevMemberDecl->getAccess();

    MemberDecl->setAccess(LexicalAS);
    return true;
  }

  MemberDecl->setAccess(PrevMemberDecl->getAccess());
  return false;
}

static CXXRecordDecl *FindDeclaringClass(NamedDecl *D) {
  DeclContext *DC = D->getDeclContext();

  // This can only happen at top: enum decls only "publish" their
  // immediate members.
  if (isa<EnumDecl>(DC))
    DC = cast<EnumDecl>(DC)->getDeclContext();

  CXXRecordDecl *DeclaringClass = cast<CXXRecordDecl>(DC);
  while (DeclaringClass->isAnonymousStructOrUnion())
    DeclaringClass = cast<CXXRecordDecl>(DeclaringClass->getDeclContext());
  return DeclaringClass;
}

namespace {
struct EffectiveContext {
  EffectiveContext() : Inner(0), Dependent(false) {}

  explicit EffectiveContext(DeclContext *DC)
    : Inner(DC),
      Dependent(DC->isDependentContext()) {

    // C++ [class.access.nest]p1:
    //   A nested class is a member and as such has the same access
    //   rights as any other member.
    // C++ [class.access]p2:
    //   A member of a class can also access all the names to which
    //   the class has access.  A local class of a member function
    //   may access the same names that the member function itself
    //   may access.
    // This almost implies that the privileges of nesting are transitive.
    // Technically it says nothing about the local classes of non-member
    // functions (which can gain privileges through friendship), but we
    // take that as an oversight.
    while (true) {
      if (isa<CXXRecordDecl>(DC)) {
        CXXRecordDecl *Record = cast<CXXRecordDecl>(DC)->getCanonicalDecl();
        Records.push_back(Record);
        DC = Record->getDeclContext();
      } else if (isa<FunctionDecl>(DC)) {
        FunctionDecl *Function = cast<FunctionDecl>(DC)->getCanonicalDecl();
        Functions.push_back(Function);
        
        if (Function->getFriendObjectKind())
          DC = Function->getLexicalDeclContext();
        else
          DC = Function->getDeclContext();
      } else if (DC->isFileContext()) {
        break;
      } else {
        DC = DC->getParent();
      }
    }
  }

  bool isDependent() const { return Dependent; }

  bool includesClass(const CXXRecordDecl *R) const {
    R = R->getCanonicalDecl();
    return std::find(Records.begin(), Records.end(), R)
             != Records.end();
  }

  /// Retrieves the innermost "useful" context.  Can be null if we're
  /// doing access-control without privileges.
  DeclContext *getInnerContext() const {
    return Inner;
  }

  typedef SmallVectorImpl<CXXRecordDecl*>::const_iterator record_iterator;

  DeclContext *Inner;
  SmallVector<FunctionDecl*, 4> Functions;
  SmallVector<CXXRecordDecl*, 4> Records;
  bool Dependent;
};

/// Like sema::AccessedEntity, but kindly lets us scribble all over
/// it.
struct AccessTarget : public AccessedEntity {
  AccessTarget(const AccessedEntity &Entity)
    : AccessedEntity(Entity) {
    initialize();
  }
    
  AccessTarget(ASTContext &Context, 
               MemberNonce _,
               CXXRecordDecl *NamingClass,
               DeclAccessPair FoundDecl,
               QualType BaseObjectType)
    : AccessedEntity(Context, Member, NamingClass, FoundDecl, BaseObjectType) {
    initialize();
  }

  AccessTarget(ASTContext &Context, 
               BaseNonce _,
               CXXRecordDecl *BaseClass,
               CXXRecordDecl *DerivedClass,
               AccessSpecifier Access)
    : AccessedEntity(Context, Base, BaseClass, DerivedClass, Access) {
    initialize();
  }

  bool hasInstanceContext() const {
    return HasInstanceContext;
  }

  class SavedInstanceContext {
  public:
    ~SavedInstanceContext() {
      Target.HasInstanceContext = Has;
    }

  private:
    friend struct AccessTarget;
    explicit SavedInstanceContext(AccessTarget &Target)
      : Target(Target), Has(Target.HasInstanceContext) {}
    AccessTarget &Target;
    bool Has;
  };

  SavedInstanceContext saveInstanceContext() {
    return SavedInstanceContext(*this);
  }

  void suppressInstanceContext() {
    HasInstanceContext = false;
  }

  const CXXRecordDecl *resolveInstanceContext(Sema &S) const {
    assert(HasInstanceContext);
    if (CalculatedInstanceContext)
      return InstanceContext;

    CalculatedInstanceContext = true;
    DeclContext *IC = S.computeDeclContext(getBaseObjectType());
    InstanceContext = (IC ? cast<CXXRecordDecl>(IC)->getCanonicalDecl() : 0);
    return InstanceContext;
  }

  const CXXRecordDecl *getDeclaringClass() const {
    return DeclaringClass;
  }

private:
  void initialize() {
    HasInstanceContext = (isMemberAccess() &&
                          !getBaseObjectType().isNull() &&
                          getTargetDecl()->isCXXInstanceMember());
    CalculatedInstanceContext = false;
    InstanceContext = 0;

    if (isMemberAccess())
      DeclaringClass = FindDeclaringClass(getTargetDecl());
    else
      DeclaringClass = getBaseClass();
    DeclaringClass = DeclaringClass->getCanonicalDecl();
  }

  bool HasInstanceContext : 1;
  mutable bool CalculatedInstanceContext : 1;
  mutable const CXXRecordDecl *InstanceContext;
  const CXXRecordDecl *DeclaringClass;
};

}

/// Checks whether one class might instantiate to the other.
static bool MightInstantiateTo(const CXXRecordDecl *From,
                               const CXXRecordDecl *To) {
  // Declaration names are always preserved by instantiation.
  if (From->getDeclName() != To->getDeclName())
    return false;

  const DeclContext *FromDC = From->getDeclContext()->getPrimaryContext();
  const DeclContext *ToDC = To->getDeclContext()->getPrimaryContext();
  if (FromDC == ToDC) return true;
  if (FromDC->isFileContext() || ToDC->isFileContext()) return false;

  // Be conservative.
  return true;
}

/// Checks whether one class is derived from another, inclusively.
/// Properly indicates when it couldn't be determined due to
/// dependence.
///
/// This should probably be donated to AST or at least Sema.
static AccessResult IsDerivedFromInclusive(const CXXRecordDecl *Derived,
                                           const CXXRecordDecl *Target) {
  assert(Derived->getCanonicalDecl() == Derived);
  assert(Target->getCanonicalDecl() == Target);

  if (Derived == Target) return AR_accessible;

  bool CheckDependent = Derived->isDependentContext();
  if (CheckDependent && MightInstantiateTo(Derived, Target))
    return AR_dependent;

  AccessResult OnFailure = AR_inaccessible;
  SmallVector<const CXXRecordDecl*, 8> Queue; // actually a stack

  while (true) {
    for (CXXRecordDecl::base_class_const_iterator
           I = Derived->bases_begin(), E = Derived->bases_end(); I != E; ++I) {

      const CXXRecordDecl *RD;

      QualType T = I->getType();
      if (const RecordType *RT = T->getAs<RecordType>()) {
        RD = cast<CXXRecordDecl>(RT->getDecl());
      } else if (const InjectedClassNameType *IT
                   = T->getAs<InjectedClassNameType>()) {
        RD = IT->getDecl();
      } else {
        assert(T->isDependentType() && "non-dependent base wasn't a record?");
        OnFailure = AR_dependent;
        continue;
      }

      RD = RD->getCanonicalDecl();
      if (RD == Target) return AR_accessible;
      if (CheckDependent && MightInstantiateTo(RD, Target))
        OnFailure = AR_dependent;

      Queue.push_back(RD);
    }

    if (Queue.empty()) break;

    Derived = Queue.back();
    Queue.pop_back();
  }

  return OnFailure;
}


static bool MightInstantiateTo(Sema &S, DeclContext *Context,
                               DeclContext *Friend) {
  if (Friend == Context)
    return true;

  assert(!Friend->isDependentContext() &&
         "can't handle friends with dependent contexts here");

  if (!Context->isDependentContext())
    return false;

  if (Friend->isFileContext())
    return false;

  // TODO: this is very conservative
  return true;
}

// Asks whether the type in 'context' can ever instantiate to the type
// in 'friend'.
static bool MightInstantiateTo(Sema &S, CanQualType Context, CanQualType Friend) {
  if (Friend == Context)
    return true;

  if (!Friend->isDependentType() && !Context->isDependentType())
    return false;

  // TODO: this is very conservative.
  return true;
}

static bool MightInstantiateTo(Sema &S,
                               FunctionDecl *Context,
                               FunctionDecl *Friend) {
  if (Context->getDeclName() != Friend->getDeclName())
    return false;

  if (!MightInstantiateTo(S,
                          Context->getDeclContext(),
                          Friend->getDeclContext()))
    return false;

  CanQual<FunctionProtoType> FriendTy
    = S.Context.getCanonicalType(Friend->getType())
         ->getAs<FunctionProtoType>();
  CanQual<FunctionProtoType> ContextTy
    = S.Context.getCanonicalType(Context->getType())
         ->getAs<FunctionProtoType>();

  // There isn't any way that I know of to add qualifiers
  // during instantiation.
  if (FriendTy.getQualifiers() != ContextTy.getQualifiers())
    return false;

  if (FriendTy->getNumArgs() != ContextTy->getNumArgs())
    return false;

  if (!MightInstantiateTo(S,
                          ContextTy->getResultType(),
                          FriendTy->getResultType()))
    return false;

  for (unsigned I = 0, E = FriendTy->getNumArgs(); I != E; ++I)
    if (!MightInstantiateTo(S,
                            ContextTy->getArgType(I),
                            FriendTy->getArgType(I)))
      return false;

  return true;
}

static bool MightInstantiateTo(Sema &S,
                               FunctionTemplateDecl *Context,
                               FunctionTemplateDecl *Friend) {
  return MightInstantiateTo(S,
                            Context->getTemplatedDecl(),
                            Friend->getTemplatedDecl());
}

static AccessResult MatchesFriend(Sema &S,
                                  const EffectiveContext &EC,
                                  const CXXRecordDecl *Friend) {
  if (EC.includesClass(Friend))
    return AR_accessible;

  if (EC.isDependent()) {
    CanQualType FriendTy
      = S.Context.getCanonicalType(S.Context.getTypeDeclType(Friend));

    for (EffectiveContext::record_iterator
           I = EC.Records.begin(), E = EC.Records.end(); I != E; ++I) {
      CanQualType ContextTy
        = S.Context.getCanonicalType(S.Context.getTypeDeclType(*I));
      if (MightInstantiateTo(S, ContextTy, FriendTy))
        return AR_dependent;
    }
  }

  return AR_inaccessible;
}

static AccessResult MatchesFriend(Sema &S,
                                  const EffectiveContext &EC,
                                  CanQualType Friend) {
  if (const RecordType *RT = Friend->getAs<RecordType>())
    return MatchesFriend(S, EC, cast<CXXRecordDecl>(RT->getDecl()));

  // TODO: we can do better than this
  if (Friend->isDependentType())
    return AR_dependent;

  return AR_inaccessible;
}

/// Determines whether the given friend class template matches
/// anything in the effective context.
static AccessResult MatchesFriend(Sema &S,
                                  const EffectiveContext &EC,
                                  ClassTemplateDecl *Friend) {
  AccessResult OnFailure = AR_inaccessible;

  // Check whether the friend is the template of a class in the
  // context chain.
  for (SmallVectorImpl<CXXRecordDecl*>::const_iterator
         I = EC.Records.begin(), E = EC.Records.end(); I != E; ++I) {
    CXXRecordDecl *Record = *I;

    // Figure out whether the current class has a template:
    ClassTemplateDecl *CTD;

    // A specialization of the template...
    if (isa<ClassTemplateSpecializationDecl>(Record)) {
      CTD = cast<ClassTemplateSpecializationDecl>(Record)
        ->getSpecializedTemplate();

    // ... or the template pattern itself.
    } else {
      CTD = Record->getDescribedClassTemplate();
      if (!CTD) continue;
    }

    // It's a match.
    if (Friend == CTD->getCanonicalDecl())
      return AR_accessible;

    // If the context isn't dependent, it can't be a dependent match.
    if (!EC.isDependent())
      continue;

    // If the template names don't match, it can't be a dependent
    // match.
    if (CTD->getDeclName() != Friend->getDeclName())
      continue;

    // If the class's context can't instantiate to the friend's
    // context, it can't be a dependent match.
    if (!MightInstantiateTo(S, CTD->getDeclContext(),
                            Friend->getDeclContext()))
      continue;

    // Otherwise, it's a dependent match.
    OnFailure = AR_dependent;
  }

  return OnFailure;
}

/// Determines whether the given friend function matches anything in
/// the effective context.
static AccessResult MatchesFriend(Sema &S,
                                  const EffectiveContext &EC,
                                  FunctionDecl *Friend) {
  AccessResult OnFailure = AR_inaccessible;

  for (SmallVectorImpl<FunctionDecl*>::const_iterator
         I = EC.Functions.begin(), E = EC.Functions.end(); I != E; ++I) {
    if (Friend == *I)
      return AR_accessible;

    if (EC.isDependent() && MightInstantiateTo(S, *I, Friend))
      OnFailure = AR_dependent;
  }

  return OnFailure;
}

/// Determines whether the given friend function template matches
/// anything in the effective context.
static AccessResult MatchesFriend(Sema &S,
                                  const EffectiveContext &EC,
                                  FunctionTemplateDecl *Friend) {
  if (EC.Functions.empty()) return AR_inaccessible;

  AccessResult OnFailure = AR_inaccessible;

  for (SmallVectorImpl<FunctionDecl*>::const_iterator
         I = EC.Functions.begin(), E = EC.Functions.end(); I != E; ++I) {

    FunctionTemplateDecl *FTD = (*I)->getPrimaryTemplate();
    if (!FTD)
      FTD = (*I)->getDescribedFunctionTemplate();
    if (!FTD)
      continue;

    FTD = FTD->getCanonicalDecl();

    if (Friend == FTD)
      return AR_accessible;

    if (EC.isDependent() && MightInstantiateTo(S, FTD, Friend))
      OnFailure = AR_dependent;
  }

  return OnFailure;
}

/// Determines whether the given friend declaration matches anything
/// in the effective context.
static AccessResult MatchesFriend(Sema &S,
                                  const EffectiveContext &EC,
                                  FriendDecl *FriendD) {
  // Whitelist accesses if there's an invalid or unsupported friend
  // declaration.
  if (FriendD->isInvalidDecl() || FriendD->isUnsupportedFriend())
    return AR_accessible;

  if (TypeSourceInfo *T = FriendD->getFriendType())
    return MatchesFriend(S, EC, T->getType()->getCanonicalTypeUnqualified());

  NamedDecl *Friend
    = cast<NamedDecl>(FriendD->getFriendDecl()->getCanonicalDecl());

  // FIXME: declarations with dependent or templated scope.

  if (isa<ClassTemplateDecl>(Friend))
    return MatchesFriend(S, EC, cast<ClassTemplateDecl>(Friend));

  if (isa<FunctionTemplateDecl>(Friend))
    return MatchesFriend(S, EC, cast<FunctionTemplateDecl>(Friend));

  if (isa<CXXRecordDecl>(Friend))
    return MatchesFriend(S, EC, cast<CXXRecordDecl>(Friend));

  assert(isa<FunctionDecl>(Friend) && "unknown friend decl kind");
  return MatchesFriend(S, EC, cast<FunctionDecl>(Friend));
}

static AccessResult GetFriendKind(Sema &S,
                                  const EffectiveContext &EC,
                                  const CXXRecordDecl *Class) {
  AccessResult OnFailure = AR_inaccessible;

  // Okay, check friends.
  for (CXXRecordDecl::friend_iterator I = Class->friend_begin(),
         E = Class->friend_end(); I != E; ++I) {
    FriendDecl *Friend = *I;

    switch (MatchesFriend(S, EC, Friend)) {
    case AR_accessible:
      return AR_accessible;

    case AR_inaccessible:
      continue;

    case AR_dependent:
      OnFailure = AR_dependent;
      break;
    }
  }

  // That's it, give up.
  return OnFailure;
}

namespace {

/// A helper class for checking for a friend which will grant access
/// to a protected instance member.
struct ProtectedFriendContext {
  Sema &S;
  const EffectiveContext &EC;
  const CXXRecordDecl *NamingClass;
  bool CheckDependent;
  bool EverDependent;

  /// The path down to the current base class.
  SmallVector<const CXXRecordDecl*, 20> CurPath;

  ProtectedFriendContext(Sema &S, const EffectiveContext &EC,
                         const CXXRecordDecl *InstanceContext,
                         const CXXRecordDecl *NamingClass)
    : S(S), EC(EC), NamingClass(NamingClass),
      CheckDependent(InstanceContext->isDependentContext() ||
                     NamingClass->isDependentContext()),
      EverDependent(false) {}

  /// Check classes in the current path for friendship, starting at
  /// the given index.
  bool checkFriendshipAlongPath(unsigned I) {
    assert(I < CurPath.size());
    for (unsigned E = CurPath.size(); I != E; ++I) {
      switch (GetFriendKind(S, EC, CurPath[I])) {
      case AR_accessible:   return true;
      case AR_inaccessible: continue;
      case AR_dependent:    EverDependent = true; continue;
      }
    }
    return false;
  }

  /// Perform a search starting at the given class.
  ///
  /// PrivateDepth is the index of the last (least derived) class
  /// along the current path such that a notional public member of
  /// the final class in the path would have access in that class.
  bool findFriendship(const CXXRecordDecl *Cur, unsigned PrivateDepth) {
    // If we ever reach the naming class, check the current path for
    // friendship.  We can also stop recursing because we obviously
    // won't find the naming class there again.
    if (Cur == NamingClass)
      return checkFriendshipAlongPath(PrivateDepth);

    if (CheckDependent && MightInstantiateTo(Cur, NamingClass))
      EverDependent = true;

    // Recurse into the base classes.
    for (CXXRecordDecl::base_class_const_iterator
           I = Cur->bases_begin(), E = Cur->bases_end(); I != E; ++I) {

      // If this is private inheritance, then a public member of the
      // base will not have any access in classes derived from Cur.
      unsigned BasePrivateDepth = PrivateDepth;
      if (I->getAccessSpecifier() == AS_private)
        BasePrivateDepth = CurPath.size() - 1;

      const CXXRecordDecl *RD;

      QualType T = I->getType();
      if (const RecordType *RT = T->getAs<RecordType>()) {
        RD = cast<CXXRecordDecl>(RT->getDecl());
      } else if (const InjectedClassNameType *IT
                   = T->getAs<InjectedClassNameType>()) {
        RD = IT->getDecl();
      } else {
        assert(T->isDependentType() && "non-dependent base wasn't a record?");
        EverDependent = true;
        continue;
      }

      // Recurse.  We don't need to clean up if this returns true.
      CurPath.push_back(RD);
      if (findFriendship(RD->getCanonicalDecl(), BasePrivateDepth))
        return true;
      CurPath.pop_back();
    }

    return false;
  }

  bool findFriendship(const CXXRecordDecl *Cur) {
    assert(CurPath.empty());
    CurPath.push_back(Cur);
    return findFriendship(Cur, 0);
  }
};
}

/// Search for a class P that EC is a friend of, under the constraint
///   InstanceContext <= P <= NamingClass
/// and with the additional restriction that a protected member of
/// NamingClass would have some natural access in P.
///
/// That second condition isn't actually quite right: the condition in
/// the standard is whether the target would have some natural access
/// in P.  The difference is that the target might be more accessible
/// along some path not passing through NamingClass.  Allowing that
/// introduces two problems:
///   - It breaks encapsulation because you can suddenly access a
///     forbidden base class's members by subclassing it elsewhere.
///   - It makes access substantially harder to compute because it
///     breaks the hill-climbing algorithm: knowing that the target is
///     accessible in some base class would no longer let you change
///     the question solely to whether the base class is accessible,
///     because the original target might have been more accessible
///     because of crazy subclassing.
/// So we don't implement that.
static AccessResult GetProtectedFriendKind(Sema &S, const EffectiveContext &EC,
                                           const CXXRecordDecl *InstanceContext,
                                           const CXXRecordDecl *NamingClass) {
  assert(InstanceContext->getCanonicalDecl() == InstanceContext);
  assert(NamingClass->getCanonicalDecl() == NamingClass);

  ProtectedFriendContext PRC(S, EC, InstanceContext, NamingClass);
  if (PRC.findFriendship(InstanceContext)) return AR_accessible;
  if (PRC.EverDependent) return AR_dependent;
  return AR_inaccessible;
}

static AccessResult HasAccess(Sema &S,
                              const EffectiveContext &EC,
                              const CXXRecordDecl *NamingClass,
                              AccessSpecifier Access,
                              const AccessTarget &Target) {
  assert(NamingClass->getCanonicalDecl() == NamingClass &&
         "declaration should be canonicalized before being passed here");

  if (Access == AS_public) return AR_accessible;
  assert(Access == AS_private || Access == AS_protected);

  AccessResult OnFailure = AR_inaccessible;

  for (EffectiveContext::record_iterator
         I = EC.Records.begin(), E = EC.Records.end(); I != E; ++I) {
    // All the declarations in EC have been canonicalized, so pointer
    // equality from this point on will work fine.
    const CXXRecordDecl *ECRecord = *I;

    // [B2] and [M2]
    if (Access == AS_private) {
      if (ECRecord == NamingClass)
        return AR_accessible;

      if (EC.isDependent() && MightInstantiateTo(ECRecord, NamingClass))
        OnFailure = AR_dependent;

    // [B3] and [M3]
    } else {
      assert(Access == AS_protected);
      switch (IsDerivedFromInclusive(ECRecord, NamingClass)) {
      case AR_accessible: break;
      case AR_inaccessible: continue;
      case AR_dependent: OnFailure = AR_dependent; continue;
      }

      if (!Target.hasInstanceContext())
        return AR_accessible;

      const CXXRecordDecl *InstanceContext = Target.resolveInstanceContext(S);
      if (!InstanceContext) {
        OnFailure = AR_dependent;
        continue;
      }

      // C++ [class.protected]p1:
      //   An additional access check beyond those described earlier in
      //   [class.access] is applied when a non-static data member or
      //   non-static member function is a protected member of its naming
      //   class.  As described earlier, access to a protected member is
      //   granted because the reference occurs in a friend or member of
      //   some class C.  If the access is to form a pointer to member,
      //   the nested-name-specifier shall name C or a class derived from
      //   C. All other accesses involve a (possibly implicit) object
      //   expression. In this case, the class of the object expression
      //   shall be C or a class derived from C.
      //
      // We interpret this as a restriction on [M3].  Most of the
      // conditions are encoded by not having any instance context.
      switch (IsDerivedFromInclusive(InstanceContext, ECRecord)) {
      case AR_accessible: return AR_accessible;
      case AR_inaccessible: continue;
      case AR_dependent: OnFailure = AR_dependent; continue;
      }
    }
  }

  // [M3] and [B3] say that, if the target is protected in N, we grant
  // access if the access occurs in a friend or member of some class P
  // that's a subclass of N and where the target has some natural
  // access in P.  The 'member' aspect is easy to handle because P
  // would necessarily be one of the effective-context records, and we
  // address that above.  The 'friend' aspect is completely ridiculous
  // to implement because there are no restrictions at all on P
  // *unless* the [class.protected] restriction applies.  If it does,
  // however, we should ignore whether the naming class is a friend,
  // and instead rely on whether any potential P is a friend.
  if (Access == AS_protected && Target.hasInstanceContext()) {
    const CXXRecordDecl *InstanceContext = Target.resolveInstanceContext(S);
    if (!InstanceContext) return AR_dependent;
    switch (GetProtectedFriendKind(S, EC, InstanceContext, NamingClass)) {
    case AR_accessible: return AR_accessible;
    case AR_inaccessible: return OnFailure;
    case AR_dependent: return AR_dependent;
    }
    llvm_unreachable("impossible friendship kind");
  }

  switch (GetFriendKind(S, EC, NamingClass)) {
  case AR_accessible: return AR_accessible;
  case AR_inaccessible: return OnFailure;
  case AR_dependent: return AR_dependent;
  }

  // Silence bogus warnings
  llvm_unreachable("impossible friendship kind");
  return OnFailure;
}

/// Finds the best path from the naming class to the declaring class,
/// taking friend declarations into account.
///
/// C++0x [class.access.base]p5:
///   A member m is accessible at the point R when named in class N if
///   [M1] m as a member of N is public, or
///   [M2] m as a member of N is private, and R occurs in a member or
///        friend of class N, or
///   [M3] m as a member of N is protected, and R occurs in a member or
///        friend of class N, or in a member or friend of a class P
///        derived from N, where m as a member of P is public, private,
///        or protected, or
///   [M4] there exists a base class B of N that is accessible at R, and
///        m is accessible at R when named in class B.
///
/// C++0x [class.access.base]p4:
///   A base class B of N is accessible at R, if
///   [B1] an invented public member of B would be a public member of N, or
///   [B2] R occurs in a member or friend of class N, and an invented public
///        member of B would be a private or protected member of N, or
///   [B3] R occurs in a member or friend of a class P derived from N, and an
///        invented public member of B would be a private or protected member
///        of P, or
///   [B4] there exists a class S such that B is a base class of S accessible
///        at R and S is a base class of N accessible at R.
///
/// Along a single inheritance path we can restate both of these
/// iteratively:
///
/// First, we note that M1-4 are equivalent to B1-4 if the member is
/// treated as a notional base of its declaring class with inheritance
/// access equivalent to the member's access.  Therefore we need only
/// ask whether a class B is accessible from a class N in context R.
///
/// Let B_1 .. B_n be the inheritance path in question (i.e. where
/// B_1 = N, B_n = B, and for all i, B_{i+1} is a direct base class of
/// B_i).  For i in 1..n, we will calculate ACAB(i), the access to the
/// closest accessible base in the path:
///   Access(a, b) = (* access on the base specifier from a to b *)
///   Merge(a, forbidden) = forbidden
///   Merge(a, private) = forbidden
///   Merge(a, b) = min(a,b)
///   Accessible(c, forbidden) = false
///   Accessible(c, private) = (R is c) || IsFriend(c, R)
///   Accessible(c, protected) = (R derived from c) || IsFriend(c, R)
///   Accessible(c, public) = true
///   ACAB(n) = public
///   ACAB(i) =
///     let AccessToBase = Merge(Access(B_i, B_{i+1}), ACAB(i+1)) in
///     if Accessible(B_i, AccessToBase) then public else AccessToBase
///
/// B is an accessible base of N at R iff ACAB(1) = public.
///
/// \param FinalAccess the access of the "final step", or AS_public if
///   there is no final step.
/// \return null if friendship is dependent
static CXXBasePath *FindBestPath(Sema &S,
                                 const EffectiveContext &EC,
                                 AccessTarget &Target,
                                 AccessSpecifier FinalAccess,
                                 CXXBasePaths &Paths) {
  // Derive the paths to the desired base.
  const CXXRecordDecl *Derived = Target.getNamingClass();
  const CXXRecordDecl *Base = Target.getDeclaringClass();

  // FIXME: fail correctly when there are dependent paths.
  bool isDerived = Derived->isDerivedFrom(const_cast<CXXRecordDecl*>(Base),
                                          Paths);
  assert(isDerived && "derived class not actually derived from base");
  (void) isDerived;

  CXXBasePath *BestPath = 0;

  assert(FinalAccess != AS_none && "forbidden access after declaring class");

  bool AnyDependent = false;

  // Derive the friend-modified access along each path.
  for (CXXBasePaths::paths_iterator PI = Paths.begin(), PE = Paths.end();
         PI != PE; ++PI) {
    AccessTarget::SavedInstanceContext _ = Target.saveInstanceContext();

    // Walk through the path backwards.
    AccessSpecifier PathAccess = FinalAccess;
    CXXBasePath::iterator I = PI->end(), E = PI->begin();
    while (I != E) {
      --I;

      assert(PathAccess != AS_none);

      // If the declaration is a private member of a base class, there
      // is no level of friendship in derived classes that can make it
      // accessible.
      if (PathAccess == AS_private) {
        PathAccess = AS_none;
        break;
      }

      const CXXRecordDecl *NC = I->Class->getCanonicalDecl();

      AccessSpecifier BaseAccess = I->Base->getAccessSpecifier();
      PathAccess = std::max(PathAccess, BaseAccess);

      switch (HasAccess(S, EC, NC, PathAccess, Target)) {
      case AR_inaccessible: break;
      case AR_accessible:
        PathAccess = AS_public;

        // Future tests are not against members and so do not have
        // instance context.
        Target.suppressInstanceContext();
        break;
      case AR_dependent:
        AnyDependent = true;
        goto Next;
      }
    }

    // Note that we modify the path's Access field to the
    // friend-modified access.
    if (BestPath == 0 || PathAccess < BestPath->Access) {
      BestPath = &*PI;
      BestPath->Access = PathAccess;

      // Short-circuit if we found a public path.
      if (BestPath->Access == AS_public)
        return BestPath;
    }

  Next: ;
  }

  assert((!BestPath || BestPath->Access != AS_public) &&
         "fell out of loop with public path");

  // We didn't find a public path, but at least one path was subject
  // to dependent friendship, so delay the check.
  if (AnyDependent)
    return 0;

  return BestPath;
}

/// Given that an entity has protected natural access, check whether
/// access might be denied because of the protected member access
/// restriction.
///
/// \return true if a note was emitted
static bool TryDiagnoseProtectedAccess(Sema &S, const EffectiveContext &EC,
                                       AccessTarget &Target) {
  // Only applies to instance accesses.
  if (!Target.hasInstanceContext())
    return false;
  assert(Target.isMemberAccess());
  NamedDecl *D = Target.getTargetDecl();

  const CXXRecordDecl *DeclaringClass = Target.getDeclaringClass();
  DeclaringClass = DeclaringClass->getCanonicalDecl();

  for (EffectiveContext::record_iterator
         I = EC.Records.begin(), E = EC.Records.end(); I != E; ++I) {
    const CXXRecordDecl *ECRecord = *I;
    switch (IsDerivedFromInclusive(ECRecord, DeclaringClass)) {
    case AR_accessible: break;
    case AR_inaccessible: continue;
    case AR_dependent: continue;
    }

    // The effective context is a subclass of the declaring class.
    // If that class isn't a superclass of the instance context,
    // then the [class.protected] restriction applies.

    // To get this exactly right, this might need to be checked more
    // holistically;  it's not necessarily the case that gaining
    // access here would grant us access overall.

    const CXXRecordDecl *InstanceContext = Target.resolveInstanceContext(S);
    assert(InstanceContext && "diagnosing dependent access");

    switch (IsDerivedFromInclusive(InstanceContext, ECRecord)) {
    case AR_accessible: continue;
    case AR_dependent: continue;
    case AR_inaccessible:
      S.Diag(D->getLocation(), diag::note_access_protected_restricted)
        << (InstanceContext != Target.getNamingClass()->getCanonicalDecl())
        << S.Context.getTypeDeclType(InstanceContext)
        << S.Context.getTypeDeclType(ECRecord);
      return true;
    }
  }

  return false;
}

/// Diagnose the path which caused the given declaration or base class
/// to become inaccessible.
static void DiagnoseAccessPath(Sema &S,
                               const EffectiveContext &EC,
                               AccessTarget &Entity) {
  AccessSpecifier Access = Entity.getAccess();
  const CXXRecordDecl *NamingClass = Entity.getNamingClass();
  NamingClass = NamingClass->getCanonicalDecl();

  NamedDecl *D = (Entity.isMemberAccess() ? Entity.getTargetDecl() : 0);
  const CXXRecordDecl *DeclaringClass = Entity.getDeclaringClass();

  // Easy case: the decl's natural access determined its path access.
  // We have to check against AS_private here in case Access is AS_none,
  // indicating a non-public member of a private base class.
  if (D && (Access == D->getAccess() || D->getAccess() == AS_private)) {
    switch (HasAccess(S, EC, DeclaringClass, D->getAccess(), Entity)) {
    case AR_inaccessible: {
      if (Access == AS_protected &&
          TryDiagnoseProtectedAccess(S, EC, Entity))
        return;

      // Find an original declaration.
      while (D->isOutOfLine()) {
        NamedDecl *PrevDecl = 0;
        if (VarDecl *VD = dyn_cast<VarDecl>(D))
          PrevDecl = VD->getPreviousDeclaration();
        else if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D))
          PrevDecl = FD->getPreviousDeclaration();
        else if (TypedefNameDecl *TND = dyn_cast<TypedefNameDecl>(D))
          PrevDecl = TND->getPreviousDeclaration();
        else if (TagDecl *TD = dyn_cast<TagDecl>(D)) {
          if (isa<RecordDecl>(D) && cast<RecordDecl>(D)->isInjectedClassName())
            break;
          PrevDecl = TD->getPreviousDeclaration();
        }
        if (!PrevDecl) break;
        D = PrevDecl;
      }

      CXXRecordDecl *DeclaringClass = FindDeclaringClass(D);
      Decl *ImmediateChild;
      if (D->getDeclContext() == DeclaringClass)
        ImmediateChild = D;
      else {
        DeclContext *DC = D->getDeclContext();
        while (DC->getParent() != DeclaringClass)
          DC = DC->getParent();
        ImmediateChild = cast<Decl>(DC);
      }
      
      // Check whether there's an AccessSpecDecl preceding this in the
      // chain of the DeclContext.
      bool Implicit = true;
      for (CXXRecordDecl::decl_iterator
             I = DeclaringClass->decls_begin(), E = DeclaringClass->decls_end();
           I != E; ++I) {
        if (*I == ImmediateChild) break;
        if (isa<AccessSpecDecl>(*I)) {
          Implicit = false;
          break;
        }
      }

      S.Diag(D->getLocation(), diag::note_access_natural)
        << (unsigned) (Access == AS_protected)
        << Implicit;
      return;
    }

    case AR_accessible: break;

    case AR_dependent:
      llvm_unreachable("can't diagnose dependent access failures");
      return;
    }
  }

  CXXBasePaths Paths;
  CXXBasePath &Path = *FindBestPath(S, EC, Entity, AS_public, Paths);

  CXXBasePath::iterator I = Path.end(), E = Path.begin();
  while (I != E) {
    --I;

    const CXXBaseSpecifier *BS = I->Base;
    AccessSpecifier BaseAccess = BS->getAccessSpecifier();

    // If this is public inheritance, or the derived class is a friend,
    // skip this step.
    if (BaseAccess == AS_public)
      continue;

    switch (GetFriendKind(S, EC, I->Class)) {
    case AR_accessible: continue;
    case AR_inaccessible: break;
    case AR_dependent:
      llvm_unreachable("can't diagnose dependent access failures");
    }

    // Check whether this base specifier is the tighest point
    // constraining access.  We have to check against AS_private for
    // the same reasons as above.
    if (BaseAccess == AS_private || BaseAccess >= Access) {

      // We're constrained by inheritance, but we want to say
      // "declared private here" if we're diagnosing a hierarchy
      // conversion and this is the final step.
      unsigned diagnostic;
      if (D) diagnostic = diag::note_access_constrained_by_path;
      else if (I + 1 == Path.end()) diagnostic = diag::note_access_natural;
      else diagnostic = diag::note_access_constrained_by_path;

      S.Diag(BS->getSourceRange().getBegin(), diagnostic)
        << BS->getSourceRange()
        << (BaseAccess == AS_protected)
        << (BS->getAccessSpecifierAsWritten() == AS_none);
      
      if (D)
        S.Diag(D->getLocation(), diag::note_field_decl);
      
      return;
    }
  }

  llvm_unreachable("access not apparently constrained by path");
}

static void DiagnoseBadAccess(Sema &S, SourceLocation Loc,
                              const EffectiveContext &EC,
                              AccessTarget &Entity) {
  const CXXRecordDecl *NamingClass = Entity.getNamingClass();
  const CXXRecordDecl *DeclaringClass = Entity.getDeclaringClass();
  NamedDecl *D = (Entity.isMemberAccess() ? Entity.getTargetDecl() : 0);

  S.Diag(Loc, Entity.getDiag())
    << (Entity.getAccess() == AS_protected)
    << (D ? D->getDeclName() : DeclarationName())
    << S.Context.getTypeDeclType(NamingClass)
    << S.Context.getTypeDeclType(DeclaringClass);
  DiagnoseAccessPath(S, EC, Entity);
}

/// MSVC has a bug where if during an using declaration name lookup, 
/// the declaration found is unaccessible (private) and that declaration 
/// was bring into scope via another using declaration whose target
/// declaration is accessible (public) then no error is generated.
/// Example:
///   class A {
///   public:
///     int f();
///   };
///   class B : public A {
///   private:
///     using A::f;
///   };
///   class C : public B {
///   private:
///     using B::f;
///   };
///
/// Here, B::f is private so this should fail in Standard C++, but 
/// because B::f refers to A::f which is public MSVC accepts it.
static bool IsMicrosoftUsingDeclarationAccessBug(Sema& S, 
                                                 SourceLocation AccessLoc,
                                                 AccessTarget &Entity) {
  if (UsingShadowDecl *Shadow =
                         dyn_cast<UsingShadowDecl>(Entity.getTargetDecl())) {
    const NamedDecl *OrigDecl = Entity.getTargetDecl()->getUnderlyingDecl();
    if (Entity.getTargetDecl()->getAccess() == AS_private && 
        (OrigDecl->getAccess() == AS_public ||
         OrigDecl->getAccess() == AS_protected)) {
      S.Diag(AccessLoc, diag::war_ms_using_declaration_inaccessible) 
        << Shadow->getUsingDecl()->getQualifiedNameAsString()
        << OrigDecl->getQualifiedNameAsString();
      return true;
    }
  }
  return false;
}

/// Determines whether the accessed entity is accessible.  Public members
/// have been weeded out by this point.
static AccessResult IsAccessible(Sema &S,
                                 const EffectiveContext &EC,
                                 AccessTarget &Entity) {
  // Determine the actual naming class.
  CXXRecordDecl *NamingClass = Entity.getNamingClass();
  while (NamingClass->isAnonymousStructOrUnion())
    NamingClass = cast<CXXRecordDecl>(NamingClass->getParent());
  NamingClass = NamingClass->getCanonicalDecl();

  AccessSpecifier UnprivilegedAccess = Entity.getAccess();
  assert(UnprivilegedAccess != AS_public && "public access not weeded out");

  // Before we try to recalculate access paths, try to white-list
  // accesses which just trade in on the final step, i.e. accesses
  // which don't require [M4] or [B4]. These are by far the most
  // common forms of privileged access.
  if (UnprivilegedAccess != AS_none) {
    switch (HasAccess(S, EC, NamingClass, UnprivilegedAccess, Entity)) {
    case AR_dependent:
      // This is actually an interesting policy decision.  We don't
      // *have* to delay immediately here: we can do the full access
      // calculation in the hope that friendship on some intermediate
      // class will make the declaration accessible non-dependently.
      // But that's not cheap, and odds are very good (note: assertion
      // made without data) that the friend declaration will determine
      // access.
      return AR_dependent;

    case AR_accessible: return AR_accessible;
    case AR_inaccessible: break;
    }
  }

  AccessTarget::SavedInstanceContext _ = Entity.saveInstanceContext();

  // We lower member accesses to base accesses by pretending that the
  // member is a base class of its declaring class.
  AccessSpecifier FinalAccess;

  if (Entity.isMemberAccess()) {
    // Determine if the declaration is accessible from EC when named
    // in its declaring class.
    NamedDecl *Target = Entity.getTargetDecl();
    const CXXRecordDecl *DeclaringClass = Entity.getDeclaringClass();

    FinalAccess = Target->getAccess();
    switch (HasAccess(S, EC, DeclaringClass, FinalAccess, Entity)) {
    case AR_accessible:
      FinalAccess = AS_public;
      break;
    case AR_inaccessible: break;
    case AR_dependent: return AR_dependent; // see above
    }

    if (DeclaringClass == NamingClass)
      return (FinalAccess == AS_public ? AR_accessible : AR_inaccessible);

    Entity.suppressInstanceContext();
  } else {
    FinalAccess = AS_public;
  }

  assert(Entity.getDeclaringClass() != NamingClass);

  // Append the declaration's access if applicable.
  CXXBasePaths Paths;
  CXXBasePath *Path = FindBestPath(S, EC, Entity, FinalAccess, Paths);
  if (!Path)
    return AR_dependent;

  assert(Path->Access <= UnprivilegedAccess &&
         "access along best path worse than direct?");
  if (Path->Access == AS_public)
    return AR_accessible;
  return AR_inaccessible;
}

static void DelayDependentAccess(Sema &S,
                                 const EffectiveContext &EC,
                                 SourceLocation Loc,
                                 const AccessTarget &Entity) {
  assert(EC.isDependent() && "delaying non-dependent access");
  DeclContext *DC = EC.getInnerContext();
  assert(DC->isDependentContext() && "delaying non-dependent access");
  DependentDiagnostic::Create(S.Context, DC, DependentDiagnostic::Access,
                              Loc,
                              Entity.isMemberAccess(),
                              Entity.getAccess(),
                              Entity.getTargetDecl(),
                              Entity.getNamingClass(),
                              Entity.getBaseObjectType(),
                              Entity.getDiag());
}

/// Checks access to an entity from the given effective context.
static AccessResult CheckEffectiveAccess(Sema &S,
                                         const EffectiveContext &EC,
                                         SourceLocation Loc,
                                         AccessTarget &Entity) {
  assert(Entity.getAccess() != AS_public && "called for public access!");

  if (S.getLangOptions().MicrosoftMode &&
      IsMicrosoftUsingDeclarationAccessBug(S, Loc, Entity))
    return AR_accessible;

  switch (IsAccessible(S, EC, Entity)) {
  case AR_dependent:
    DelayDependentAccess(S, EC, Loc, Entity);
    return AR_dependent;

  case AR_inaccessible:
    if (!Entity.isQuiet())
      DiagnoseBadAccess(S, Loc, EC, Entity);
    return AR_inaccessible;

  case AR_accessible:
    return AR_accessible;
  }

  // silence unnecessary warning
  llvm_unreachable("invalid access result");
  return AR_accessible;
}

static Sema::AccessResult CheckAccess(Sema &S, SourceLocation Loc,
                                      AccessTarget &Entity) {
  // If the access path is public, it's accessible everywhere.
  if (Entity.getAccess() == AS_public)
    return Sema::AR_accessible;

  if (S.SuppressAccessChecking)
    return Sema::AR_accessible;

  // If we're currently parsing a declaration, we may need to delay
  // access control checking, because our effective context might be
  // different based on what the declaration comes out as.
  //
  // For example, we might be parsing a declaration with a scope
  // specifier, like this:
  //   A::private_type A::foo() { ... }
  //
  // Or we might be parsing something that will turn out to be a friend:
  //   void foo(A::private_type);
  //   void B::foo(A::private_type);
  if (S.DelayedDiagnostics.shouldDelayDiagnostics()) {
    S.DelayedDiagnostics.add(DelayedDiagnostic::makeAccess(Loc, Entity));
    return Sema::AR_delayed;
  }

  EffectiveContext EC(S.CurContext);
  switch (CheckEffectiveAccess(S, EC, Loc, Entity)) {
  case AR_accessible: return Sema::AR_accessible;
  case AR_inaccessible: return Sema::AR_inaccessible;
  case AR_dependent: return Sema::AR_dependent;
  }
  llvm_unreachable("falling off end");
  return Sema::AR_accessible;
}

void Sema::HandleDelayedAccessCheck(DelayedDiagnostic &DD, Decl *decl) {
  // Access control for names used in the declarations of functions
  // and function templates should normally be evaluated in the context
  // of the declaration, just in case it's a friend of something.
  // However, this does not apply to local extern declarations.

  DeclContext *DC = decl->getDeclContext();
  if (FunctionDecl *fn = dyn_cast<FunctionDecl>(decl)) {
    if (!DC->isFunctionOrMethod()) DC = fn;
  } else if (FunctionTemplateDecl *fnt = dyn_cast<FunctionTemplateDecl>(decl)) {
    // Never a local declaration.
    DC = fnt->getTemplatedDecl();
  }

  EffectiveContext EC(DC);

  AccessTarget Target(DD.getAccessData());

  if (CheckEffectiveAccess(*this, EC, DD.Loc, Target) == ::AR_inaccessible)
    DD.Triggered = true;
}

void Sema::HandleDependentAccessCheck(const DependentDiagnostic &DD,
                        const MultiLevelTemplateArgumentList &TemplateArgs) {
  SourceLocation Loc = DD.getAccessLoc();
  AccessSpecifier Access = DD.getAccess();

  Decl *NamingD = FindInstantiatedDecl(Loc, DD.getAccessNamingClass(),
                                       TemplateArgs);
  if (!NamingD) return;
  Decl *TargetD = FindInstantiatedDecl(Loc, DD.getAccessTarget(),
                                       TemplateArgs);
  if (!TargetD) return;

  if (DD.isAccessToMember()) {
    CXXRecordDecl *NamingClass = cast<CXXRecordDecl>(NamingD);
    NamedDecl *TargetDecl = cast<NamedDecl>(TargetD);
    QualType BaseObjectType = DD.getAccessBaseObjectType();
    if (!BaseObjectType.isNull()) {
      BaseObjectType = SubstType(BaseObjectType, TemplateArgs, Loc,
                                 DeclarationName());
      if (BaseObjectType.isNull()) return;
    }

    AccessTarget Entity(Context,
                        AccessTarget::Member,
                        NamingClass,
                        DeclAccessPair::make(TargetDecl, Access),
                        BaseObjectType);
    Entity.setDiag(DD.getDiagnostic());
    CheckAccess(*this, Loc, Entity);
  } else {
    AccessTarget Entity(Context,
                        AccessTarget::Base,
                        cast<CXXRecordDecl>(TargetD),
                        cast<CXXRecordDecl>(NamingD),
                        Access);
    Entity.setDiag(DD.getDiagnostic());
    CheckAccess(*this, Loc, Entity);
  }
}

Sema::AccessResult Sema::CheckUnresolvedLookupAccess(UnresolvedLookupExpr *E,
                                                     DeclAccessPair Found) {
  if (!getLangOptions().AccessControl ||
      !E->getNamingClass() ||
      Found.getAccess() == AS_public)
    return AR_accessible;

  AccessTarget Entity(Context, AccessTarget::Member, E->getNamingClass(), 
                      Found, QualType());
  Entity.setDiag(diag::err_access) << E->getSourceRange();

  return CheckAccess(*this, E->getNameLoc(), Entity);
}

/// Perform access-control checking on a previously-unresolved member
/// access which has now been resolved to a member.
Sema::AccessResult Sema::CheckUnresolvedMemberAccess(UnresolvedMemberExpr *E,
                                                     DeclAccessPair Found) {
  if (!getLangOptions().AccessControl ||
      Found.getAccess() == AS_public)
    return AR_accessible;

  QualType BaseType = E->getBaseType();
  if (E->isArrow())
    BaseType = BaseType->getAs<PointerType>()->getPointeeType();

  AccessTarget Entity(Context, AccessTarget::Member, E->getNamingClass(),
                      Found, BaseType);
  Entity.setDiag(diag::err_access) << E->getSourceRange();

  return CheckAccess(*this, E->getMemberLoc(), Entity);
}

Sema::AccessResult Sema::CheckDestructorAccess(SourceLocation Loc,
                                               CXXDestructorDecl *Dtor,
                                               const PartialDiagnostic &PDiag) {
  if (!getLangOptions().AccessControl)
    return AR_accessible;

  // There's never a path involved when checking implicit destructor access.
  AccessSpecifier Access = Dtor->getAccess();
  if (Access == AS_public)
    return AR_accessible;

  CXXRecordDecl *NamingClass = Dtor->getParent();
  AccessTarget Entity(Context, AccessTarget::Member, NamingClass,
                      DeclAccessPair::make(Dtor, Access),
                      QualType());
  Entity.setDiag(PDiag); // TODO: avoid copy

  return CheckAccess(*this, Loc, Entity);
}

/// Checks access to a constructor.
Sema::AccessResult Sema::CheckConstructorAccess(SourceLocation UseLoc,
                                                CXXConstructorDecl *Constructor,
                                                const InitializedEntity &Entity,
                                                AccessSpecifier Access,
                                                bool IsCopyBindingRefToTemp) {
  if (!getLangOptions().AccessControl ||
      Access == AS_public)
    return AR_accessible;

  CXXRecordDecl *NamingClass = Constructor->getParent();
  AccessTarget AccessEntity(Context, AccessTarget::Member, NamingClass,
                            DeclAccessPair::make(Constructor, Access),
                            QualType());
  PartialDiagnostic PD(PDiag());
  switch (Entity.getKind()) {
  default:
    PD = PDiag(IsCopyBindingRefToTemp
                 ? diag::ext_rvalue_to_reference_access_ctor
                 : diag::err_access_ctor);

    break;

  case InitializedEntity::EK_Base:
    PD = PDiag(diag::err_access_base_ctor);
    PD << Entity.isInheritedVirtualBase()
       << Entity.getBaseSpecifier()->getType() << getSpecialMember(Constructor);
    break;

  case InitializedEntity::EK_Member: {
    const FieldDecl *Field = cast<FieldDecl>(Entity.getDecl());
    PD = PDiag(diag::err_access_field_ctor);
    PD << Field->getType() << getSpecialMember(Constructor);
    break;
  }

  }

  return CheckConstructorAccess(UseLoc, Constructor, Access, PD);
}

/// Checks access to a constructor.
Sema::AccessResult Sema::CheckConstructorAccess(SourceLocation UseLoc,
                                                CXXConstructorDecl *Constructor,
                                                AccessSpecifier Access,
                                                PartialDiagnostic PD) {
  if (!getLangOptions().AccessControl ||
      Access == AS_public)
    return AR_accessible;

  CXXRecordDecl *NamingClass = Constructor->getParent();
  AccessTarget AccessEntity(Context, AccessTarget::Member, NamingClass,
                            DeclAccessPair::make(Constructor, Access),
                            QualType());
  AccessEntity.setDiag(PD);

  return CheckAccess(*this, UseLoc, AccessEntity);
}

/// Checks direct (i.e. non-inherited) access to an arbitrary class
/// member.
Sema::AccessResult Sema::CheckDirectMemberAccess(SourceLocation UseLoc,
                                                 NamedDecl *Target,
                                           const PartialDiagnostic &Diag) {
  AccessSpecifier Access = Target->getAccess();
  if (!getLangOptions().AccessControl ||
      Access == AS_public)
    return AR_accessible;

  CXXRecordDecl *NamingClass = cast<CXXRecordDecl>(Target->getDeclContext());
  AccessTarget Entity(Context, AccessTarget::Member, NamingClass,
                      DeclAccessPair::make(Target, Access),
                      QualType());
  Entity.setDiag(Diag);
  return CheckAccess(*this, UseLoc, Entity);
}
                                           

/// Checks access to an overloaded operator new or delete.
Sema::AccessResult Sema::CheckAllocationAccess(SourceLocation OpLoc,
                                               SourceRange PlacementRange,
                                               CXXRecordDecl *NamingClass,
                                               DeclAccessPair Found,
                                               bool Diagnose) {
  if (!getLangOptions().AccessControl ||
      !NamingClass ||
      Found.getAccess() == AS_public)
    return AR_accessible;

  AccessTarget Entity(Context, AccessTarget::Member, NamingClass, Found,
                      QualType());
  if (Diagnose)
    Entity.setDiag(diag::err_access)
      << PlacementRange;

  return CheckAccess(*this, OpLoc, Entity);
}

/// Checks access to an overloaded member operator, including
/// conversion operators.
Sema::AccessResult Sema::CheckMemberOperatorAccess(SourceLocation OpLoc,
                                                   Expr *ObjectExpr,
                                                   Expr *ArgExpr,
                                                   DeclAccessPair Found) {
  if (!getLangOptions().AccessControl ||
      Found.getAccess() == AS_public)
    return AR_accessible;

  const RecordType *RT = ObjectExpr->getType()->castAs<RecordType>();
  CXXRecordDecl *NamingClass = cast<CXXRecordDecl>(RT->getDecl());

  AccessTarget Entity(Context, AccessTarget::Member, NamingClass, Found,
                      ObjectExpr->getType());
  Entity.setDiag(diag::err_access)
    << ObjectExpr->getSourceRange()
    << (ArgExpr ? ArgExpr->getSourceRange() : SourceRange());

  return CheckAccess(*this, OpLoc, Entity);
}

Sema::AccessResult Sema::CheckAddressOfMemberAccess(Expr *OvlExpr,
                                                    DeclAccessPair Found) {
  if (!getLangOptions().AccessControl ||
      Found.getAccess() == AS_none ||
      Found.getAccess() == AS_public)
    return AR_accessible;

  OverloadExpr *Ovl = OverloadExpr::find(OvlExpr).Expression;
  CXXRecordDecl *NamingClass = Ovl->getNamingClass();

  AccessTarget Entity(Context, AccessTarget::Member, NamingClass, Found,
                      Context.getTypeDeclType(NamingClass));
  Entity.setDiag(diag::err_access)
    << Ovl->getSourceRange();

  return CheckAccess(*this, Ovl->getNameLoc(), Entity);
}

/// Checks access for a hierarchy conversion.
///
/// \param IsBaseToDerived whether this is a base-to-derived conversion (true)
///     or a derived-to-base conversion (false)
/// \param ForceCheck true if this check should be performed even if access
///     control is disabled;  some things rely on this for semantics
/// \param ForceUnprivileged true if this check should proceed as if the
///     context had no special privileges
/// \param ADK controls the kind of diagnostics that are used
Sema::AccessResult Sema::CheckBaseClassAccess(SourceLocation AccessLoc,
                                              QualType Base,
                                              QualType Derived,
                                              const CXXBasePath &Path,
                                              unsigned DiagID,
                                              bool ForceCheck,
                                              bool ForceUnprivileged) {
  if (!ForceCheck && !getLangOptions().AccessControl)
    return AR_accessible;

  if (Path.Access == AS_public)
    return AR_accessible;

  CXXRecordDecl *BaseD, *DerivedD;
  BaseD = cast<CXXRecordDecl>(Base->getAs<RecordType>()->getDecl());
  DerivedD = cast<CXXRecordDecl>(Derived->getAs<RecordType>()->getDecl());

  AccessTarget Entity(Context, AccessTarget::Base, BaseD, DerivedD, 
                      Path.Access);
  if (DiagID)
    Entity.setDiag(DiagID) << Derived << Base;

  if (ForceUnprivileged) {
    switch (CheckEffectiveAccess(*this, EffectiveContext(),
                                 AccessLoc, Entity)) {
    case ::AR_accessible: return Sema::AR_accessible;
    case ::AR_inaccessible: return Sema::AR_inaccessible;
    case ::AR_dependent: return Sema::AR_dependent;
    }
    llvm_unreachable("unexpected result from CheckEffectiveAccess");
  }
  return CheckAccess(*this, AccessLoc, Entity);
}

/// Checks access to all the declarations in the given result set.
void Sema::CheckLookupAccess(const LookupResult &R) {
  assert(getLangOptions().AccessControl
         && "performing access check without access control");
  assert(R.getNamingClass() && "performing access check without naming class");

  for (LookupResult::iterator I = R.begin(), E = R.end(); I != E; ++I) {
    if (I.getAccess() != AS_public) {
      AccessTarget Entity(Context, AccessedEntity::Member,
                          R.getNamingClass(), I.getPair(),
                          R.getBaseObjectType());
      Entity.setDiag(diag::err_access);
      CheckAccess(*this, R.getNameLoc(), Entity);
    }
  }
}

/// Checks access to Decl from the given class. The check will take access
/// specifiers into account, but no member access expressions and such.
///
/// \param Decl the declaration to check if it can be accessed
/// \param Class the class/context from which to start the search
/// \return true if the Decl is accessible from the Class, false otherwise.
bool Sema::IsSimplyAccessible(NamedDecl *Decl, CXXRecordDecl *Class) {
  if (!Class || !Decl->isCXXClassMember())
    return true;

  QualType qType = Class->getTypeForDecl()->getCanonicalTypeInternal();
  AccessTarget Entity(Context, AccessedEntity::Member, Class,
                      DeclAccessPair::make(Decl, Decl->getAccess()),
                      qType);
  if (Entity.getAccess() == AS_public)
    return true;

  EffectiveContext EC(CurContext);
  return ::IsAccessible(*this, EC, Entity) != ::AR_inaccessible;
}

void Sema::ActOnStartSuppressingAccessChecks() {
  assert(!SuppressAccessChecking &&
         "Tried to start access check suppression when already started.");
  SuppressAccessChecking = true;
}

void Sema::ActOnStopSuppressingAccessChecks() {
  assert(SuppressAccessChecking &&
         "Tried to stop access check suprression when already stopped.");
  SuppressAccessChecking = false;
}
