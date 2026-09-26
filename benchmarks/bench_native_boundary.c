/* Live registers across branches exercise native-state import and export. */
#include <stdint.h>
#include <stdio.h>

#include "bench_timing.h"

#define TIMES 1000000
#define RUNS 7

static bench_ns_t bench_six_live(const double values[6]) {
    unsigned count = TIMES;
    bench_ns_t start = bench_now_ns();
    // Six live values match the pressure in NFSMW's box-transform loop.
    __asm__ volatile(
        "fninit\n"
        "fldl 0(%1); fldl 8(%1); fldl 16(%1); fldl 24(%1); fldl 32(%1); fldl 40(%1)\n"
        "1: fnop; jmp 2f\n"
        "2: fnop; subl $1, %0; jnz 1b\n"
        "fninit"
        : "+r"(count)
        : "r"(values)
        : "memory", "cc", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)");
    return bench_now_ns() - start;
}

int main(void) {
    const struct {
        const char* name;
        double values[6];
    } cases[] = {
        {"normal", {1.0, -2.5, 0.125, -1.75, 0x1p-1022, 0x1.fffffffffffffp1023}},
        {"zero", {0.0, -0.0, 0.0, -0.0, 0.0, -0.0}},
        {"mixed", {0.0, 1.25, -2.5, -0.0, 0.5, 2.0}},
        {"special",
         {0x1p-1074, -0x1p-1074, __builtin_inf(), -__builtin_inf(), __builtin_nan(""),
          -__builtin_nan("")}},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        bench_six_live(cases[i].values);
        for (int run = 0; run < RUNS; ++run)
            printf("BENCH native_boundary_%s %lu\n", cases[i].name,
                   (unsigned long)bench_six_live(cases[i].values));
    }
    return 0;
}
