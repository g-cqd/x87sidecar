/*
 * test_fld_gap_fstp_a32.c -- the fld_gap_fstp fusion with 32-bit address-size
 * operands, the form Wine's i386 code produces.  Linked below 4 GB (lowzero) so
 * every buffer is addressable through 32-bit registers; the memory operands
 * carry the 0x67 address-size override, so the translator sees addr_size S32.
 *
 *   flds   (%esi)
 *   movss  %xmm1, 0x4(%edi)
 *   fstps  0x30(%edi)
 *
 * Covers base-only and base+index*scale targets, a gap operand with a different
 * scale (not provably disjoint), and a 64-bit-addressed gap operand against a
 * 32-bit target (different address size: not provably disjoint).  Checks the
 * copied bits against the host's own conversions and the x87 state afterwards.
 * "--list" names the cases and how many copies the sidecar should fuse in each
 * (default, X87_FUSE_GAP_STRICT=1); "--only NAME" runs one (scripts/check_gap_fuse.sh).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Rosetta ends a translation block at every 4 KiB page boundary, so a function
 * that straddles one would be two blocks and could never fuse.  Aligning the
 * (small) case functions keeps what is fused independent of the image layout. */
#define FN __attribute__((noinline, aligned(128)))

static int failures = 0;

#define FNINIT() __asm__ volatile(".byte 0xDB, 0xE3" ::: "memory")

static uint8_t buf[256] __attribute__((aligned(64)));
static uint8_t srcbuf[64] __attribute__((aligned(64)));

static const uint32_t in32[] = {0x00000000, 0x80000000, 0x3f800000, 0xbfc00000,
                                0x7f7fffff, 0x00000001, 0x7f800000, 0xff800000,
                                0x7fc00000, 0x7f800001, 0xff8abcde, 0x4b000001};

static uint32_t expect32(uint32_t bits) { /* flds + fstps */
    volatile float f;
    memcpy((void*)&f, &bits, 4);
    volatile double d = (double)f;
    volatile float g = (float)d;
    uint32_t out;
    memcpy(&out, (const void*)&g, 4);
    return out;
}

#define PRE "movl $0x11223344, %%ecx\n\tmovd %%ecx, %%xmm1\n\t"
#define CL "rax", "rcx", "rdx", "xmm1", "memory", "cc"

/* d, s are 32-bit pointers; i is a 32-bit index */
#define CASE(NAME, GAP, EXTRA_IN)                                                       \
    static FN void NAME(uint32_t d, uint32_t s, uint32_t i) {                           \
        __asm__ volatile(PRE "flds (%k[s])\n\t" GAP "\n\tfstps 0x30(%k[d],%k[i],4)\n\t" \
                         :                                                              \
                         : [d] "r"(d), [s] "r"(s), [i] "r"(i)EXTRA_IN                   \
                         : CL);                                                         \
    }

CASE(c_gap_disjoint, "movss %%xmm1, 0x4(%k[d],%k[i],4)", ) /* same base/index/scale */
CASE(c_gap_regonly, "movaps %%xmm1, %%xmm2", )
CASE(c_gap_other_scale, "movss %%xmm1, 0x30(%k[d],%k[i],2)", )   /* not provable */
CASE(c_gap_overlap, "movl $0x5a5a5a5a, 0x30(%k[d],%k[i],4)", )   /* overlaps */
CASE(c_gap_wide_base, "movl $0x5a5a5a5a, 0x30(%q[d],%q[i],4)", ) /* S64 vs S32 */

typedef void (*fn_t)(uint32_t, uint32_t, uint32_t);
typedef struct {
    const char* name;
    fn_t fn;
    int fuse;
    int fuse_strict;
    int gap_store_off; /* >=0: offset of a gap store, value in gap_store_val (applied before the
                          FSTP) */
    uint32_t gap_store_val;
} test_case;

static const test_case cases[] = {
    {"a32_gap_disjoint", c_gap_disjoint, 1, 0, 0x4 + 4 * 2, 0x11223344},
    {"a32_gap_regonly", c_gap_regonly, 1, 1, -1, 0},
    {"a32_gap_other_scale", c_gap_other_scale, 0, 0, 0x30 + 2 * 2, 0x11223344},
    {"a32_gap_overlap", c_gap_overlap, 0, 0, 0x30 + 4 * 2, 0x5a5a5a5a},
    {"a32_gap_wide_base", c_gap_wide_base, 0, 0, 0x30 + 4 * 2, 0x5a5a5a5a},
};

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char* only = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0) { /* name, copies expected fused (default, strict) */
            for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
                printf("GROUP %s %d %d\n", cases[c].name, cases[c].fuse, cases[c].fuse_strict);
            }
            return 0;
        }
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            only = argv[++i];
        }
    }
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        if (only != NULL && strcmp(only, cases[c].name) != 0) {
            continue;
        }
        int bad = 0;
        for (size_t k = 0; k < sizeof(in32) / sizeof(in32[0]); k++) {
            const uint32_t idx = 2;
            memset(buf, 0xA5, sizeof(buf));
            memcpy(srcbuf, &in32[k], 4);
            FNINIT();
            cases[c].fn((uint32_t)(uintptr_t)buf, (uint32_t)(uintptr_t)srcbuf, idx);
            uint8_t env[28] __attribute__((aligned(16)));
            __asm__ volatile("fnstenv %0" : "=m"(env)::"memory");
            const uint16_t sw = (uint16_t)(env[4] | (env[5] << 8));
            const uint16_t tw = (uint16_t)(env[8] | (env[9] << 8));

            uint8_t want[256];
            memset(want, 0xA5, sizeof(want));
            if (cases[c].gap_store_off >= 0) {
                memcpy(want + cases[c].gap_store_off, &cases[c].gap_store_val, 4);
            }
            const uint32_t e = expect32(in32[k]);
            memcpy(want + 0x30 + idx * 4, &e, 4);
            if (memcmp(buf, want, sizeof(buf)) != 0 || tw != 0xFFFF || ((sw >> 11) & 7) != 0) {
                bad++;
            }
        }
        if (bad) {
            printf("FAIL  %s (%d inputs)\n", cases[c].name, bad);
            failures++;
        } else {
            printf("PASS  %s (%d inputs)\n", cases[c].name, (int)(sizeof(in32) / sizeof(in32[0])));
        }
    }
    printf("%s\n", failures ? "SOME FAILED" : "ALL PASS");
    return failures ? 1 : 0;
}
