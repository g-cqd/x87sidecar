/*
 * bench_fld_gap_fstp.c -- isolated float copies through the x87 stack.
 *
 * The Far Cry 2 shape (a flds, an unrelated SSE store, an fstps to a
 * neighbouring field) and a plain adjacent flds/fstps pair, plus SSE-only and
 * empty-loop references.  Each pattern is timed R times over N iterations;
 * "BENCH name total_ns" reports the median repetition (the run_benchmarks.sh
 * protocol) and "DETAIL name min median max ns_per_iter" the spread.
 * scripts/bench_gap_fuse.sh runs it under stock Rosetta and the sidecar.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bench_timing.h"

#define N 2000000L
#define R 9

static float src[16] = {1.5f, 2.5f, 3.5f, 4.5f};
static float dst[64] __attribute__((aligned(64)));
static double dsrc[8] = {1.25, 2.5, 3.5, 4.5};
static double ddst[64] __attribute__((aligned(64)));

static __attribute__((noinline)) void p_fc2(float* d, const float* s, long n) {
    for (long i = 0; i < n; i++)
        __asm__ volatile("movss %%xmm7, 0x20(%0)\n\t"
                         "flds (%1)\n\t"
                         "movss %%xmm1, 0x4(%0)\n\t"
                         "fstps 0x30(%0)\n"
                         :
                         : "r"(d), "r"(s)
                         : "memory", "xmm1", "xmm7");
}

static __attribute__((noinline)) void p_fc2_d(double* d, const double* s, long n) {
    for (long i = 0; i < n; i++)
        __asm__ volatile("movss %%xmm7, 0x20(%0)\n\t"
                         "fldl (%1)\n\t"
                         "movss %%xmm1, 0x4(%0)\n\t"
                         "fstpl 0x30(%0)\n"
                         :
                         : "r"(d), "r"(s)
                         : "memory", "xmm1", "xmm7");
}

/* Same shape with three values live on the x87 stack for the whole loop: every
 * unfused reply then converts three slots at its native-state boundary. */
static __attribute__((noinline)) void p_fc2_live3(float* d, const float* s, long n) {
    static const double v[3] = {11.5, 22.5, 33.5};
    __asm__ volatile("fldl 0(%0)\n\tfldl 8(%0)\n\tfldl 16(%0)\n" : : "r"(v) : "memory");
    for (long i = 0; i < n; i++)
        __asm__ volatile("movss %%xmm7, 0x20(%0)\n\t"
                         "flds (%1)\n\t"
                         "movss %%xmm1, 0x4(%0)\n\t"
                         "fstps 0x30(%0)\n"
                         :
                         : "r"(d), "r"(s)
                         : "memory", "xmm1", "xmm7");
    __asm__ volatile("fstp %%st(0)\n\tfstp %%st(0)\n\tfstp %%st(0)\n" : : : "memory");
}

/* The copy sits on a dependency chain: the target of one iteration is the source
 * of the next (store-to-load forwarding), so its latency is measured, not hidden
 * behind the loop. */
static __attribute__((noinline)) void p_chain(float* d, const float* s, long n) {
    (void)s;
    d[12] = src[0];
    for (long i = 0; i < n; i++)
        __asm__ volatile("flds 0x30(%0)\n\t"
                         "movss %%xmm1, 0x4(%0)\n\t"
                         "fstps 0x30(%0)\n"
                         :
                         : "r"(d)
                         : "memory", "xmm1");
}

static __attribute__((noinline)) void p_pair(float* d, const float* s, long n) {
    for (long i = 0; i < n; i++)
        __asm__ volatile("flds (%1)\n\t"
                         "fstps 0x30(%0)\n"
                         :
                         : "r"(d), "r"(s)
                         : "memory");
}

static __attribute__((noinline)) void p_sse(float* d, const float* s, long n) {
    for (long i = 0; i < n; i++)
        __asm__ volatile("movss %%xmm7, 0x20(%0)\n\t"
                         "movss (%1), %%xmm0\n\t"
                         "movss %%xmm1, 0x4(%0)\n\t"
                         "movss %%xmm0, 0x30(%0)\n"
                         :
                         : "r"(d), "r"(s)
                         : "memory", "xmm0", "xmm1", "xmm7");
}

static __attribute__((noinline)) void p_loop(float* d, const float* s, long n) {
    for (long i = 0; i < n; i++)
        __asm__ volatile("" : : "r"(d), "r"(s) : "memory");
}

static int cmp_u64(const void* a, const void* b) {
    const uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    return x < y ? -1 : x > y;
}

static void run(const char* name, void (*fn)(float*, const float*, long), int check) {
    uint64_t t[R];
    for (int i = 0; i < 64; i++)
        dst[i] = -1.0f;
    fn(dst, src, 20000); /* warm-up: the first call pays the translation */
    if (check && dst[12] != src[0]) {
        printf("CHECK FAILED %s: dst[12]=%f\n", name, dst[12]);
        exit(1);
    }
    for (int r = 0; r < R; r++) {
        const bench_ns_t t0 = bench_now_ns();
        fn(dst, src, N);
        t[r] = bench_now_ns() - t0;
    }
    qsort(t, R, sizeof(t[0]), cmp_u64);
    printf("BENCH %s %llu\n", name, (unsigned long long)t[R / 2]);
    printf("DETAIL %s %.3f %.3f %.3f\n", name, (double)t[0] / N, (double)t[R / 2] / N,
           (double)t[R - 1] / N);
}

int main(void) {
    run("fc2_flds_fstps", p_fc2, 1);
    run("fc2_live3_flds_fstps", p_fc2_live3, 1);
    run("chain_flds_fstps", p_chain, 1);
    run("pair_flds_fstps", p_pair, 1);
    run("sse_movss_equiv", p_sse, 1);
    run("loop_only", p_loop, 0);
    {
        uint64_t t[R];
        p_fc2_d(ddst, dsrc, 20000);
        if (ddst[6] != dsrc[0]) {
            printf("CHECK FAILED fc2_fldl_fstpl\n");
            exit(1);
        }
        for (int r = 0; r < R; r++) {
            const bench_ns_t t0 = bench_now_ns();
            p_fc2_d(ddst, dsrc, N);
            t[r] = bench_now_ns() - t0;
        }
        qsort(t, R, sizeof(t[0]), cmp_u64);
        printf("BENCH fc2_fldl_fstpl %llu\n", (unsigned long long)t[R / 2]);
        printf("DETAIL fc2_fldl_fstpl %.3f %.3f %.3f\n", (double)t[0] / N, (double)t[R / 2] / N,
               (double)t[R - 1] / N);
    }
    return 0;
}
