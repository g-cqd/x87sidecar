/*
 * test_fpr_pool_pressure.c — x87 instructions after scalar SSE code that
 * leaves stock few or no scratch FPRs.
 *
 * Every translator draws its FPR temporaries from the V16-V31 slots stock
 * leaves free, and stock keeps each recent scalar SSE result written to a
 * fresh register (cvtsd2ss here) in one of them until the block ends.  K such
 * results ahead of the x87 code in the same block leave 8-K: K=8 leaves none.
 * The single-instruction translators assumed all 8, so an inline FSINCOS
 * (peak 8) after one cvtsd2ss aborted the whole process on alloc_free_fpr's
 * assert (Crysis 3, 2026-10-11).  Now an instruction that cannot fit goes to
 * stock and the rest of its run stays ours.
 *
 * Each case checks the x87 results and that the eight cvtsd2ss results
 * survive: a translator using a slot stock still holds would corrupt them.
 */
#include <math.h>
#include <stdio.h>

static int failures = 0;

/* Relative 1e-13: loose enough for the inline polynomials (a few ulp) and for
   libm references such as exp2(a) - 1, tight enough that a clobbered register
   or a mis-ordered stack cannot pass. */
static void check(const char* name, double got, double expected) {
    if (fabs(got - expected) <= 1e-13 * fabs(expected)) {
        printf("PASS  %-44s  got=%.17g\n", name, got);
    } else {
        printf("FAIL  %-44s  got=%.17g expected=%.17g\n", name, got, expected);
        failures++;
    }
}

struct out {
    double r0, r1;
    float f[8];
};

static const double kIn[8] = {1.5, -2.25, 3.125, -4.0625, 5.5, -6.75, 7.875, -8.5};

#define LOAD8                                                    \
    "movsd 0(%[in]), %%xmm0\n\t movsd 8(%[in]), %%xmm1\n\t"      \
    "movsd 16(%[in]), %%xmm2\n\t movsd 24(%[in]), %%xmm3\n\t"    \
    "movsd 32(%[in]), %%xmm4\n\t movsd 40(%[in]), %%xmm5\n\t"    \
    "movsd 48(%[in]), %%xmm6\n\t movsd 56(%[in]), %%xmm7\n\t"
/* The destinations must not be written earlier in the block: stock then
   merges the scalar at once and keeps no temporary (zeroing them first with
   xorps leaves the pool at 8). */
#define CVT(i) "cvtsd2ss %%xmm" #i ", %%xmm1" #i "\n\t"
#define CVT_1 "cvtsd2ss %%xmm0, %%xmm8\n\t"
#define CVT_3 CVT_1 "cvtsd2ss %%xmm1, %%xmm9\n\t" CVT(0)
#define CVT_8 CVT_3 CVT(1) CVT(2) CVT(3) CVT(4) CVT(5)
#define STORE8                                                                     \
    "movss %%xmm8, 0(%[f])\n\t movss %%xmm9, 4(%[f])\n\t"                          \
    "movss %%xmm10, 8(%[f])\n\t movss %%xmm11, 12(%[f])\n\t"                       \
    "movss %%xmm12, 16(%[f])\n\t movss %%xmm13, 20(%[f])\n\t"                      \
    "movss %%xmm14, 24(%[f])\n\t movss %%xmm15, 28(%[f])\n\t"

/* One basic block: SSE results, then ST1=b ST0=a, the x87 snippet (which
   leaves exactly two values), then both values and the SSE results stored. */
#define CASE(fn, CVTS, X87)                                                            \
    static void fn(double a, double b, struct out* o) {                                \
        __asm__ volatile(LOAD8 CVTS "fldl %[b]\n\t fldl %[a]\n\t" X87                  \
                         "fstpl %[r0]\n\t fstpl %[r1]\n\t" STORE8                      \
                         : [r0] "=m"(o->r0), [r1] "=m"(o->r1)                          \
                         : [in] "r"(kIn), [f] "r"(o->f), [a] "m"(a), [b] "m"(b)        \
                         : "memory", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5",   \
                           "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12",  \
                           "xmm13", "xmm14", "xmm15", "st", "st(1)", "st(2)");         \
    }

/* fsincos: ST0=cos ST1=sin ST2=b; fstp st(2) leaves ST0=sin ST1=cos. */
#define X_FSINCOS "fsincos\n\t fstp %%st(2)\n\t"
/* fptan: ST0=1.0 ST1=tan ST2=b; fstp st(2) leaves ST0=tan ST1=1.0. */
#define X_FPTAN "fptan\n\t fstp %%st(2)\n\t"
/* fxtract: ST0=significand ST1=exponent ST2=b; leaves ST0=exp ST1=sig. */
#define X_FXTRACT "fxtract\n\t fstp %%st(2)\n\t"

/* Pool 7: FSINCOS, FPTAN, FYL2X and FYL2XP1 (peak 8) no longer fit; FSIN
   (peak 7) still does and stays inline. */
CASE(fsincos_k1, CVT_1, X_FSINCOS)
CASE(fptan_k1, CVT_1, X_FPTAN)
CASE(fyl2x_k1, CVT_1, "fyl2x\n\t fld1\n\t")
CASE(fyl2xp1_k1, CVT_1, "fyl2xp1\n\t fld1\n\t")
CASE(fsin_k1, CVT_1, "fsin\n\t")
/* Pool 5, the Crysis 3 state: none of the transcendentals fit. */
CASE(fsincos_k3, CVT_3, X_FSINCOS)
CASE(fsin_k3, CVT_3, "fsin\n\t")
CASE(fcos_k3, CVT_3, "fcos\n\t")
CASE(fpatan_k3, CVT_3, "fpatan\n\t fld1\n\t")
CASE(f2xm1_k3, CVT_3, "f2xm1\n\t")
CASE(fxtract_k3, CVT_3, X_FXTRACT)
CASE(fscale_k3, CVT_3, "fscale\n\t")
/* An x87 run whose middle instruction does not fit: the FMUL before and the
   FADDP/FMUL after stay ours, FSINCOS goes to stock in between. */
CASE(mixed_run_k3, CVT_3, "fmul %%st(0), %%st\n\t fsincos\n\t faddp\n\t fmul %%st(1), %%st\n\t")
/* Pool 0: not even a load or the reply's native-state boundary fits. */
CASE(fmul_k8, CVT_8, "fmul %%st(1), %%st\n\t")
CASE(fsincos_k8, CVT_8, X_FSINCOS)

static void check_sse(const char* name, const struct out* o, int k) {
    /* CVT_8 order: xmm8<-xmm0, xmm9<-xmm1, xmm10..xmm15<-xmm0..xmm5. */
    static const int src[8] = {0, 1, 0, 1, 2, 3, 4, 5};
    int ok = 1;
    for (int i = 0; i < k; i++) {
        const float want = (float)kIn[src[i]];
        if (o->f[i] != want) {
            printf("FAIL  %-44s  xmm%d=%g expected %g\n", name, 8 + i, o->f[i], want);
            ok = 0;
        }
    }
    if (ok) {
        printf("PASS  %-44s\n", name);
    } else {
        failures++;
    }
}

#define RUN(fn, k, a, b, want0, want1)              \
    do {                                            \
        struct out o;                               \
        fn((a), (b), &o);                           \
        check(#fn ".r0", o.r0, (want0));            \
        check(#fn ".r1", o.r1, (want1));            \
        check_sse(#fn " keeps SSE results", &o, k); \
    } while (0)

int main(void) {
    const double a = 0.75, b = 1.25;

    RUN(fsincos_k1, 1, a, b, sin(a), cos(a));
    RUN(fptan_k1, 1, a, b, tan(a), 1.0);
    RUN(fyl2x_k1, 1, a, b, 1.0, b * log2(a));
    RUN(fyl2xp1_k1, 1, 0.25, b, 1.0, b * log2(1.25));
    RUN(fsin_k1, 1, a, b, sin(a), b);

    RUN(fsincos_k3, 3, a, b, sin(a), cos(a));
    RUN(fsin_k3, 3, a, b, sin(a), b);
    RUN(fcos_k3, 3, a, b, cos(a), b);
    RUN(fpatan_k3, 3, a, b, 1.0, atan2(b, a));
    RUN(f2xm1_k3, 3, a, b, exp2(a) - 1.0, b);
    RUN(fxtract_k3, 3, 6.0, b, 2.0, 1.5);
    RUN(fscale_k3, 3, a, 3.0, a * 8.0, 3.0);
    RUN(mixed_run_k3, 3, a, b, (sin(a * a) + cos(a * a)) * b, b);

    RUN(fmul_k8, 8, a, b, a * b, b);
    RUN(fsincos_k8, 8, a, b, sin(a), cos(a));

    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
