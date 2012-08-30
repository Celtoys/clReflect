// RUN: %clang_cc1 -fms-extensions -rewrite-objc -fobjc-fragile-abi -x objective-c++ -fblocks -o - %s
// radar 7537770

typedef struct {
        int a;
        int b;
} s;

extern void CFBasicHashApply(int (^block)(s)) {
        int used, cnt;
    for (int idx = 0; 0 < used && idx < cnt; idx++) {
                s bkt;
        if (0 < bkt.a) {
            if (!block(bkt)) {
                return;
            }
            used--;
        }
    }
}

