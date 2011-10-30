// RUN: %clang_cc1 %s -verify -fsyntax-only

// Test that we recover gracefully from conflict markers left in input files.
// PR5238

// diff3 style
<<<<<<< .mine      // expected-error {{version control conflict marker in file}}
int x = 4;
|||||||
int x = 123;
=======
float x = 17;
>>>>>>> .r91107

// normal style.
<<<<<<< .mine     // expected-error {{version control conflict marker in file}}
typedef int y;
=======
typedef struct foo *y;
>>>>>>> .r91107

// Perforce style.
>>>> ORIGINAL conflict-marker.c#6 // expected-error {{version control conflict marker in file}}
int z = 1;
==== THEIRS conflict-marker.c#7
int z = 0;
==== YOURS conflict-marker.c
int z = 2;
<<<<

;
y b;


int foo() {
  y a = x;
  return x + a - z;
}
