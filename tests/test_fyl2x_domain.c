/*
 * test_fyl2x_domain.c — FYL2X outside the "positive normal" fast path.
 *
 * The inline log2 the JIT emits is a port of optimized-routines' AdvSIMD
 * inline_log2, which is defined only for positive normal doubles.  x87
 * defines a result for every other input, and stock Rosetta produces it:
 *
 *   ST(0) = ±0        ZE, log2 = −∞  → ST(1) = ∓∞ (sign from y)
 *   ST(0) < 0         IE, result = the real indefinite QNaN
 *   ST(0) = +∞        log2 = +∞      → ST(1) = ±∞ (sign from y)
 *   ST(0) = NaN       the NaN propagates, quieted
 *   0 < ST(0) < 2^-1022 (subnormal) an ordinary in-domain value: log2 of a
 *                     subnormal reaches −1074, and the table algorithm
 *                     cannot see below −1022 without renormalising first
 *
 * Without the domain handling every one of these returns a plausible-looking
 * finite number near ±1022, which is far worse than a NaN: a wrong finite
 * value survives comparisons and propagates.
 *
 * Expected values here are stock Rosetta's own, which match the Intel SDM
 * tables.  The sign of an invalid-operation NaN is not asserted (x87 uses the
 * negative indefinite, ARM's default NaN is positive) — only its NaN-ness.
 */
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_ULP 4

static int failures = 0;

static uint64_t bits(double d) {
    uint64_t u;
    memcpy(&u, &d, sizeof u);
    return u;
}

static void check_exact(const char* name, double got, double expected) {
    if (bits(got) == bits(expected)) {
        printf("PASS  %-40s  got=0x%016llx\n", name, (unsigned long long)bits(got));
        return;
    }
    printf("FAIL  %-40s  got=0x%016llx (%.17g)  expected=0x%016llx (%.17g)\n", name,
           (unsigned long long)bits(got), got, (unsigned long long)bits(expected), expected);
    failures++;
}

static void check_nan(const char* name, double got) {
    if (isnan(got)) {
        printf("PASS  %-40s  NaN 0x%016llx\n", name, (unsigned long long)bits(got));
        return;
    }
    printf("FAIL  %-40s  got=0x%016llx (%.17g)  expected a NaN\n", name,
           (unsigned long long)bits(got), got);
    failures++;
}

static void check_ulp(const char* name, double got, double expected) {
    uint64_t g = bits(got), e = bits(expected);
    if (g == e) {
        printf("PASS  %-40s  got=0x%016llx (%.17g)\n", name, (unsigned long long)g, got);
        return;
    }
    uint64_t ga = g & 0x7fffffffffffffffULL, ea = e & 0x7fffffffffffffffULL;
    uint64_t d = ((g >> 63) == (e >> 63)) ? (ga > ea ? ga - ea : ea - ga) : ga + ea;
    if (d <= MAX_ULP) {
        printf("PASS  %-40s  got=0x%016llx (%.17g) [ulp=%llu]\n", name, (unsigned long long)g, got,
               (unsigned long long)d);
        return;
    }
    printf("FAIL  %-40s  got=0x%016llx (%.17g)  expected=0x%016llx (%.17g)  ulp=%llu\n", name,
           (unsigned long long)g, got, (unsigned long long)e, expected, (unsigned long long)d);
    failures++;
}

__attribute__((noinline)) static double do_fyl2x(double y, double x) {
    double r;
    __asm__ volatile(
        "fldl  %1\n\t" /* push y → ST(0) */
        "fldl  %2\n\t" /* push x → ST(0); y now ST(1) */
        "fyl2x\n\t"    /* ST(1) = y*log2(x); pop ST(0) */
        "fstpl %0\n\t"
        : "=m"(r)
        : "m"(y), "m"(x)
        : "st");
    return r;
}

int main(void) {
    const double inf = (double)INFINITY;
    const double qnan = nan("");
    double snan;
    uint64_t snan_bits = 0x7FF0000000000001ULL;
    memcpy(&snan, &snan_bits, sizeof snan);

    double sub_min, sub_half, sub_three;
    uint64_t b;
    b = 0x0000000000000001ULL; /* 2^-1074, the smallest subnormal */
    memcpy(&sub_min, &b, sizeof b);
    b = 0x0008000000000000ULL; /* 2^-1023 */
    memcpy(&sub_half, &b, sizeof b);
    b = 0x0000000000000003ULL; /* 3·2^-1074 */
    memcpy(&sub_three, &b, sizeof b);

    /* ── ST(0) = ±0: log2 is −∞, so the result is ∓∞ ─────────────────── */
    check_exact("fyl2x(1, +0)", do_fyl2x(1.0, 0.0), -inf);
    check_exact("fyl2x(1, -0)", do_fyl2x(1.0, -0.0), -inf);
    check_exact("fyl2x(-1, +0)", do_fyl2x(-1.0, 0.0), inf);
    check_exact("fyl2x(3, +0)", do_fyl2x(3.0, 0.0), -inf);
    check_exact("fyl2x(-3, -0)", do_fyl2x(-3.0, -0.0), inf);
    /* y = 0 with x = 0 is 0·∞ — invalid. */
    check_nan("fyl2x(0, +0)", do_fyl2x(0.0, 0.0));

    /* ── ST(0) < 0: invalid operation ────────────────────────────────── */
    check_nan("fyl2x(1, -1)", do_fyl2x(1.0, -1.0));
    check_nan("fyl2x(1, -0.5)", do_fyl2x(1.0, -0.5));
    check_nan("fyl2x(1, -2)", do_fyl2x(1.0, -2.0));
    check_nan("fyl2x(1, -1e300)", do_fyl2x(1.0, -1e300));
    check_nan("fyl2x(1, -inf)", do_fyl2x(1.0, -inf));
    check_nan("fyl2x(3, -4)", do_fyl2x(3.0, -4.0));
    check_nan("fyl2x(-1, -4)", do_fyl2x(-1.0, -4.0));
    check_nan("fyl2x(1, -2^-1074)", do_fyl2x(1.0, -sub_min));

    /* ── ST(0) = +∞ ──────────────────────────────────────────────────── */
    check_exact("fyl2x(1, +inf)", do_fyl2x(1.0, inf), inf);
    check_exact("fyl2x(-1, +inf)", do_fyl2x(-1.0, inf), -inf);
    check_exact("fyl2x(3, +inf)", do_fyl2x(3.0, inf), inf);
    check_nan("fyl2x(0, +inf)", do_fyl2x(0.0, inf));

    /* ── NaN propagation ─────────────────────────────────────────────── */
    check_nan("fyl2x(1, qnan)", do_fyl2x(1.0, qnan));
    check_nan("fyl2x(1, snan)", do_fyl2x(1.0, snan));
    check_nan("fyl2x(qnan, 4)", do_fyl2x(qnan, 4.0));

    /* ── positive subnormals: in domain, and below the table's reach ─── */
    check_exact("fyl2x(1, 2^-1074)", do_fyl2x(1.0, sub_min), -1074.0);
    check_exact("fyl2x(1, 2^-1023)", do_fyl2x(1.0, sub_half), -1023.0);
    check_ulp("fyl2x(1, 3·2^-1074)", do_fyl2x(1.0, sub_three), log2(sub_three));
    check_exact("fyl2x(2, 2^-1074)", do_fyl2x(2.0, sub_min), -2148.0);
    check_exact("fyl2x(-1, 2^-1023)", do_fyl2x(-1.0, sub_half), 1023.0);
    /* DBL_MIN is the smallest normal — the boundary of the fast path. */
    check_exact("fyl2x(1, DBL_MIN)", do_fyl2x(1.0, DBL_MIN), -1022.0);
    check_ulp("fyl2x(1, DBL_MIN*1.5)", do_fyl2x(1.0, DBL_MIN * 1.5), log2(DBL_MIN * 1.5));

    /* ── the fast path itself must be untouched ──────────────────────── */
    check_exact("fyl2x(1, 1)", do_fyl2x(1.0, 1.0), 0.0);
    check_exact("fyl2x(1, 2)", do_fyl2x(1.0, 2.0), 1.0);
    check_exact("fyl2x(1, 8)", do_fyl2x(1.0, 8.0), 3.0);
    check_exact("fyl2x(1, 0.5)", do_fyl2x(1.0, 0.5), -1.0);
    check_ulp("fyl2x(1, 10)", do_fyl2x(1.0, 10.0), log2(10.0));
    check_ulp("fyl2x(3, 0.1)", do_fyl2x(3.0, 0.1), 3.0 * log2(0.1));
    check_ulp("fyl2x(1, 1e300)", do_fyl2x(1.0, 1e300), log2(1e300));
    check_ulp("fyl2x(1, 1e-300)", do_fyl2x(1.0, 1e-300), log2(1e-300));
    check_exact("fyl2x(1, DBL_MAX)", do_fyl2x(1.0, DBL_MAX), log2(DBL_MAX));

    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
