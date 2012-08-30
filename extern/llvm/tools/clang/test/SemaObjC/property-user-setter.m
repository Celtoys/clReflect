// RUN: %clang_cc1 -fsyntax-only -verify -Wno-objc-root-class %s

@interface I0 
@property(readonly) int x;
@property(readonly) int y;
@property(readonly) int z;
-(void) setY: (int) y0;
@end

@interface I0 (Cat0)
-(void) setX: (int) a0;
@end

@implementation I0
@dynamic x;
@dynamic y;
@dynamic z;
-(void) setY: (int) y0{}

-(void) im0 {
  self.x = 0;
  self.y = 2;
  self.z = 2; // expected-error {{assignment to readonly property}}
}
@end

// Test when property is 'readonly' but it has a setter in
// its implementation only.
@interface I1  {
}
@property(readonly) int identifier;
@end


@implementation I1
@dynamic identifier;
- (void)setIdentifier:(int)ident {}

- (id)initWithIdentifier:(int)Arg {
    self.identifier = 0;
}

@end


// Also in a category implementation
@interface I1(CAT)  
@property(readonly) int rprop;
@end


@implementation I1(CAT)
@dynamic rprop;
- (void)setRprop:(int)ident {}

- (id)initWithIdentifier:(int)Arg {
    self.rprop = 0;
}

@end

static int g_val;

@interface Root 
+ alloc;
- init;
@end

@interface Subclass : Root
{
    int setterOnly;
}
- (void) setSetterOnly:(int)value;
@end

@implementation Subclass
- (void) setSetterOnly:(int)value {
    setterOnly = value;
    g_val = setterOnly;
}
@end

@interface C {}
// - (int)Foo;
- (void)setFoo:(int)value;
@end

void g(int); // expected-note {{passing argument to parameter here}}

void f(C *c) {
    c.Foo = 17; // OK 
    g(c.Foo); // expected-error {{expected getter method not found on object of type 'C *'}}
}


void abort(void);
int main (void) {
    Subclass *x = [[Subclass alloc] init];

    x.setterOnly = 4;   // OK
    if (g_val != 4)
      abort ();
    return 0;
}
