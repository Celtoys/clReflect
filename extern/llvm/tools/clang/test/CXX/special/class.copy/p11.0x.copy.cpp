// RUN: %clang_cc1 -std=c++11 -fsyntax-only -verify %s

struct NonTrivial {
  NonTrivial(const NonTrivial&);
};

// A defaulted copy constructor for a class X is defined as deleted if X has:

// -- a variant member with a non-trivial corresponding constructor
union DeletedNTVariant {
  NonTrivial NT; // expected-note{{copy constructor of union 'DeletedNTVariant' is implicitly deleted because field 'NT' has a non-trivial copy constructor}}
  DeletedNTVariant();
};
DeletedNTVariant DVa;
DeletedNTVariant DVb(DVa); // expected-error{{call to implicitly-deleted copy constructor}}

struct DeletedNTVariant2 {
  union {
    NonTrivial NT; // expected-note{{copy constructor of union 'DeletedNTVariant2' is implicitly deleted because field 'NT' has a non-trivial copy constructor}}
  };
  DeletedNTVariant2();
};
DeletedNTVariant2 DV2a;
DeletedNTVariant2 DV2b(DV2a); // expected-error{{call to implicitly-deleted copy constructor}}

// -- a non-static data member of class type M (or array thereof) that cannot be
//    copied because overload resolution results in an ambiguity or a function
//    that is deleted or inaccessible
struct NoAccess {
  NoAccess() = default;
private:
  NoAccess(const NoAccess&);

  friend struct HasAccess;
};

struct HasNoAccess {
  NoAccess NA; // expected-note{{copy constructor of 'HasNoAccess' is implicitly deleted because field 'NA' has an inaccessible copy constructor}}
};
HasNoAccess HNAa;
HasNoAccess HNAb(HNAa); // expected-error{{call to implicitly-deleted copy constructor}}

struct HasAccess {
  NoAccess NA;
};

HasAccess HAa;
HasAccess HAb(HAa);

struct NonConst {
  NonConst(NonConst&);
};
struct Ambiguity {
  Ambiguity(const Ambiguity&);
  Ambiguity(volatile Ambiguity&);
};

struct IsAmbiguous {
  NonConst NC;
  Ambiguity A; // expected-note 2{{copy constructor of 'IsAmbiguous' is implicitly deleted because field 'A' has multiple copy constructors}}
  IsAmbiguous();
};
IsAmbiguous IAa;
IsAmbiguous IAb(IAa); // expected-error{{call to implicitly-deleted copy constructor}}

struct Deleted {
  IsAmbiguous IA; // expected-note{{copy constructor of 'Deleted' is implicitly deleted because field 'IA' has a deleted copy constructor}}
};
Deleted Da;
Deleted Db(Da); // expected-error{{call to implicitly-deleted copy constructor}}

// -- a direct or virtual base class B that cannot be copied because overload
//    resolution results in an ambiguity or a function that is deleted or
//    inaccessible
struct AmbiguousCopyBase : Ambiguity { // expected-note 2{{copy constructor of 'AmbiguousCopyBase' is implicitly deleted because base class 'Ambiguity' has multiple copy constructors}}
  NonConst NC;
};
extern AmbiguousCopyBase ACBa;
AmbiguousCopyBase ACBb(ACBa); // expected-error {{deleted copy constructor}}

struct DeletedCopyBase : AmbiguousCopyBase {}; // expected-note {{copy constructor of 'DeletedCopyBase' is implicitly deleted because base class 'AmbiguousCopyBase' has a deleted copy constructor}}
extern DeletedCopyBase DCBa;
DeletedCopyBase DCBb(DCBa); // expected-error {{deleted copy constructor}}

struct InaccessibleCopyBase : NoAccess {}; // expected-note {{copy constructor of 'InaccessibleCopyBase' is implicitly deleted because base class 'NoAccess' has an inaccessible copy constructor}}
extern InaccessibleCopyBase ICBa;
InaccessibleCopyBase ICBb(ICBa); // expected-error {{deleted copy constructor}}

// -- any direct or virtual base class or non-static data member of a type with
//    a destructor that is deleted or inaccessible
struct NoAccessDtor {
private:
  ~NoAccessDtor();
  friend struct HasAccessDtor;
};

struct HasNoAccessDtor {
  NoAccessDtor NAD; // expected-note{{copy constructor of 'HasNoAccessDtor' is implicitly deleted because field 'NAD' has an inaccessible destructor}}
  HasNoAccessDtor();
  ~HasNoAccessDtor();
};
HasNoAccessDtor HNADa;
HasNoAccessDtor HNADb(HNADa); // expected-error{{call to implicitly-deleted copy constructor}}

struct HasAccessDtor {
  NoAccessDtor NAD;
};
HasAccessDtor HADa;
HasAccessDtor HADb(HADa);

struct HasNoAccessDtorBase : NoAccessDtor { // expected-note{{copy constructor of 'HasNoAccessDtorBase' is implicitly deleted because base class 'NoAccessDtor' has an inaccessible destructor}}
};
extern HasNoAccessDtorBase HNADBa;
HasNoAccessDtorBase HNADBb(HNADBa); // expected-error{{implicitly-deleted copy constructor}}

// -- a non-static data member of rvalue reference type
struct RValue {
  int && ri = 1; // expected-note{{copy constructor of 'RValue' is implicitly deleted because field 'ri' is of rvalue reference type 'int &&'}}
};
RValue RVa;
RValue RVb(RVa); // expected-error{{call to implicitly-deleted copy constructor}}
