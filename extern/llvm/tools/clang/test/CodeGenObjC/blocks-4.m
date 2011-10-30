// RUN: %clang_cc1 -triple i386-apple-darwin9 -fobjc-fragile-abi -emit-llvm -fobjc-exceptions -fblocks -o %t %s
// rdar://7590273

void EXIT(id e);

@interface NSBlockOperation {
}
+(id)blockOperationWithBlock:(void (^)(void))block ;
@end

void FUNC() {
        [NSBlockOperation blockOperationWithBlock:^{
            @try {

            }
            @catch (id exception) {
		EXIT(exception);
            }
        }];

}
