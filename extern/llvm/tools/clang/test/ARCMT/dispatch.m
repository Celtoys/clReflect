// RUN: %clang_cc1 -fblocks -fsyntax-only -fobjc-arc -x objective-c %s.result
// RUN: arcmt-test --args -triple x86_64-apple-darwin10 -fblocks -fsyntax-only -x objective-c %s > %t
// RUN: diff %t %s.result

#include "Common.h"

#define dispatch_retain(object) ({ dispatch_object_t _o = (object); _dispatch_object_validate(_o); (void)[_o retain]; })
#define dispatch_release(object) ({ dispatch_object_t _o = (object); _dispatch_object_validate(_o); [_o release]; })
#define xpc_retain(object) ({ xpc_object_t _o = (object); _xpc_object_validate(_o); [_o retain]; })
#define xpc_release(object) ({ xpc_object_t _o = (object); _xpc_object_validate(_o); [_o release]; })

typedef id dispatch_object_t;
typedef id xpc_object_t;

void _dispatch_object_validate(dispatch_object_t object);
void _xpc_object_validate(xpc_object_t object);

dispatch_object_t getme(void);

void func(dispatch_object_t o) {
  dispatch_retain(o);
  dispatch_release(o);
  dispatch_retain(getme());
}

void func2(xpc_object_t o) {
  xpc_retain(o);
  xpc_release(o);
}
