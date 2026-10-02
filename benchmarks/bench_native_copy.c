/*
 * bench_native_copy.c -- native arm64 reference for bench_fld_gap_fstp.c: the
 * same float copies as plain loads and stores, no translation at all.  This is
 * the ceiling for what any x87 translation of that pattern can reach.
 * Built for the host (arm64), run directly.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bench_timing.h"

#define N 2000000L
#define R 9

static float src[16] = {1.5f, 2.5f, 3.5f, 4.5f};
static float dst[64] __attribute__((aligned(64)));

static __attribute__((noinline)) void p_fc2(float* d, const float* s, long n) {
    for (long i = 0; i < n; i++)
        __asm__ volatile(
            "str s7, [%0, #0x20]\n\t"
            "ldr s0, [%1]\n\t"
            "str s1, [%0, #4]\n\t"
            "str s0, [%0, #0x30]\n"
            :
            : "r"(d), "r"(s)
            : "memory", "v0");
}

static __attribute__((noinline)) void p_chain(float* d, const float* s, long n) {
    (void)s;
    d[12] = src[0];
    for (long i = 0; i < n; i++)
        __asm__ volatile(
            "ldr s0, [%0, #0x30]\n\t"
            "str s1, [%0, #4]\n\t"
            "str s0, [%0, #0x30]\n"
            :
            : "r"(d)
            : "memory", "v0");
}

static __attribute__((noinline)) void p_pair(float* d, const float* s, long n) {
    for (long i = 0; i < n; i++)
        __asm__ volatile(
            "ldr s0, [%1]\n\t"
            "str s0, [%0, #0x30]\n"
            :
            : "r"(d), "r"(s)
            : "memory", "v0");
}

static __attribute__((noinline)) void p_loop(float* d, const float* s, long n) {
    for (long i = 0; i < n; i++)
        __asm__ volatile("" : : "r"(d), "r"(s) : "memory");
}

static int cmp_u64(const void* a, const void* b) {
    const uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    return x < y ? -1 : x > y;
}

static void run(const char* name, void (*fn)(float*, const float*, long)) {
    uint64_t t[R];
    fn(dst, src, 20000);
    for (int r = 0; r < R; r++) {
        const bench_ns_t t0 = bench_now_ns();
        fn(dst, src, N);
        t[r] = bench_now_ns() - t0;
    }
    qsort(t, R, sizeof(t[0]), cmp_u64);
    printf("DETAIL %s %.3f %.3f %.3f\n", name, (double)t[0] / N, (double)t[R / 2] / N,
           (double)t[R - 1] / N);
}

int main(void) {
    run("fc2_flds_fstps", p_fc2);
    run("fc2_live3_flds_fstps", p_fc2); /* same stores: x87 state does not exist natively */
    run("chain_flds_fstps", p_chain);
    run("pair_flds_fstps", p_pair);
    run("loop_only", p_loop);
    return 0;
}
