// RUN: %clang_cc1 -fsyntax-only -verify -Wno-objc-root-class %s

typedef struct CGColor * __attribute__ ((NSObject)) CGColorRef;
static int count;
static CGColorRef tmp = 0;

typedef struct S1  __attribute__ ((NSObject)) CGColorRef1; // expected-error {{__attribute ((NSObject)) is for pointer types only}}
typedef void *  __attribute__ ((NSObject)) CGColorRef2; // expected-error {{__attribute ((NSObject)) is for pointer types only}}


@interface HandTested {
@public
    CGColorRef x;
}

@property(copy) CGColorRef x;
// rdar: // 7809460
typedef struct CGColor * __attribute__((NSObject)) CGColorRefNoNSObject;
@property (nonatomic, retain) CGColorRefNoNSObject color;
@end

void setProperty(id self, id value)  {
  ((HandTested *)self)->x = value;
}

id getProperty(id self) {
     return (id)((HandTested *)self)->x;
}

@implementation HandTested
@synthesize x=x;
@dynamic color;
@end

int main(int argc, char *argv[]) {
    HandTested *to;
    to.x = tmp;  // setter
    if (tmp != to.x)
      to.x = tmp;
    return 0;
}

// rdar://10453342
@interface I
{
   __attribute__((NSObject)) void * color; // expected-warning {{__attribute ((NSObject)) may be put on a typedef only, attribute is ignored}}
}
  // <rdar://problem/10930507>
@property (nonatomic, retain) __attribute__((NSObject)) void * color; // // no-warning
@end
void test_10453342() {
    char* __attribute__((NSObject)) string2 = 0; // expected-warning {{__attribute ((NSObject)) may be put on a typedef only, attribute is ignored}}
}

