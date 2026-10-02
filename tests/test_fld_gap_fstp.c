/*
 * test_fld_gap_fstp.c -- FLD m32/m64 ... FSTP m32/m64 float copies with
 * unrelated non-x87 instructions in between (the fld_gap_fstp fusion).
 *
 *     movss  %xmm7, 0x20(%rdi)
 *     flds   (%rsi)              push
 *     movss  %xmm1, 0x4(%rdi)    <= 4 independent non-x87 instructions
 *     fstps  0x30(%rdi)          pop
 *
 * The fusion performs the copy at the FLD and leaves the x87 stack alone,
 * so the things that could go wrong are: the copied bits (every load/store
 * size pair, over NaNs, denormals, infinities, zeros and values that need
 * rounding on the way to single), the x87 state afterwards (CW, TOP, tag
 * word -- with 0, 1, 3 and 7 values already on the stack), and the memory
 * ordering that the hoisted store must not change (gap instructions that
 * read or write the FSTP target, alias it through another register, move
 * its base register, or are x87 / branches, must be left unfused).
 *
 * Modes:
 *   (default)  self-check: PASS/FAIL per case group.  Passes under stock
 *              Rosetta and under every sidecar configuration.
 *   --dump     print every result bit and the full FNSTENV image (including
 *              the exception flags, which the sidecar does not model) so two
 *              configurations can be diffed (scripts/check_gap_fuse.sh).
 *
 * Every case also records whether the sidecar is expected to fuse it, in the
 * default configuration and with X87_FUSE_GAP_STRICT=1; the totals are printed
 * as "GAPFUSE_EXPECT default=N strict=M" for the script to compare with the
 * number of "[x87-gapfuse] fused" lines X87_LOG_GAP_FUSE=1 produces.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static int dump_mode = 0;
/* How many copies the hand-written cases below expect the sidecar to fuse
 * (default configuration / X87_FUSE_GAP_STRICT=1); table cases carry theirs. */
static int hand_fuse_default = 0;
static int hand_fuse_strict = 0;
#define EXPECT_FUSED(DEF, STRICT)     \
    do {                              \
        hand_fuse_default += (DEF);   \
        hand_fuse_strict += (STRICT); \
    } while (0)

#define FNINIT() __asm__ volatile(".byte 0xDB, 0xE3" ::: "memory")

typedef struct {
    uint16_t cw, sw, tw;
} env_t;

/* ---- input sets ---------------------------------------------------------- */

static const uint32_t f32_inputs[] = {
    0x00000000, 0x80000000, 0x3f800000, 0xbfc00000, 0x40490fdb, 0x7f7fffff, 0xff7fffff, 0x00800000,
    0x807fffff, 0x007fffff, 0x00000001, 0x80000001, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc12345,
    0x7fffffff, 0x7f800001, 0xff8abcde, 0x7fa00000, 0x3effffff, 0x4b000001, 0x33800000, 0x00400000,
};

static const uint64_t f64_inputs[] = {
    0x0000000000000000ULL,
    0x8000000000000000ULL,
    0x3ff0000000000000ULL,
    0xc004000000000000ULL,
    0x400921fb54442d18ULL,
    0x7fefffffffffffffULL,
    0xffefffffffffffffULL,
    0x0010000000000000ULL,
    0x000fffffffffffffULL,
    0x0000000000000001ULL,
    0x8000000000000001ULL,
    0x7ff0000000000000ULL,
    0xfff0000000000000ULL,
    0x7ff8000000000000ULL,
    0x7ff8dead00000000ULL,
    0xfff8000000000001ULL,
    0x7ff0000000000001ULL,
    0x7ff4000000000000ULL,
    0xfff0000000000007ULL,
    0x7fffffffffffffffULL,
    /* narrowing to single: ties, near-ties, overflow, float denormals */
    0x3ff000000fffffffULL,
    0x3ff0000010000000ULL,
    0x3ff0000010000001ULL,
    0x3ff0000030000000ULL,
    0x47efffffe0000000ULL,
    0x47effffff0000000ULL,
    0x47f0000000000000ULL,
    0xc7efffffefffffffULL,
    0x36a0000000000000ULL,
    0x3690000000000000ULL,
    0x3690000000000001ULL,
    0x3810000000000000ULL,
    0x380fffffe0000000ULL,
    0x380fffffffffffffULL,
    0x3e70000000000000ULL,
    0x1234567890abcdefULL,
};

#define N32 ((int)(sizeof(f32_inputs) / sizeof(f32_inputs[0])))
#define N64 ((int)(sizeof(f64_inputs) / sizeof(f64_inputs[0])))

/* ---- expected values ----------------------------------------------------- */

static uint32_t f2u(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
static float u2f(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static uint64_t d2u(double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}
static double u2d(uint64_t u) {
    double d;
    memcpy(&d, &u, 8);
    return d;
}

/* The conversions go through volatiles so the compiler cannot fold them: the
 * host's own cvtss2sd / cvtsd2ss (round-to-nearest, NaNs quieted) is the
 * reference for what an x87 load followed by a store of the other width does. */
static uint64_t widen(uint32_t bits) {
    volatile float f = u2f(bits);
    volatile double d = (double)f;
    return d2u(d);
}
static uint32_t narrow(uint64_t bits) {
    volatile double d = u2d(bits);
    volatile float f = (float)d;
    return f2u(f);
}

static int is_nan64(uint64_t b) {
    return (b & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL && (b & 0xfffffffffffffULL);
}
static int is_snan64(uint64_t b) {
    return is_nan64(b) && !(b & 0x0008000000000000ULL);
}

/* ---- the case table ------------------------------------------------------ */

typedef void (*case_fn)(void* d, const void* s);
typedef void (*expect_fn)(uint8_t* img);

typedef struct {
    const char* name;
    case_fn fn;
    int load_is_f64; /* FLD width */
    int store_is_f64;
    int dst_off; /* where the FSTP target ends up in the scratch buffer */
    int src_off; /* >0: source is buffer-relative (the FLD reads buf+src_off) */
    int fuse_default;
    int fuse_strict;
    expect_fn expect; /* gap side effects on the expected image (applied before the FSTP) */
} test_case;

/* The scratch buffer: FSTP target at +0x30, copy of the source at +0x10. */
#define PRE_COMMON                \
    "movl $0x11223344, %%ecx\n\t" \
    "movd %%ecx, %%xmm1\n\t"      \
    "movl $0x55667788, %%edx\n\t" \
    "movd %%edx, %%xmm7\n\t"

#define CLOBBERS "rax", "rcx", "rdx", "r9", "xmm1", "xmm2", "xmm3", "xmm7", "memory", "cc"

#define GEN(NAME, LD, ST, SRC, PRE, GAP)                                                  \
    static __attribute__((noinline)) void NAME(void* d, const void* s) {                  \
        __asm__ volatile(PRE_COMMON PRE LD " " SRC "\n\t" GAP "\n\t" ST " 0x30(%[d])\n\t" \
                         :                                                                \
                         : [d] "r"(d), [s] "r"(s)                                         \
                         : CLOBBERS);                                                     \
    }

/* One gap/pre-sequence, expanded for the four (load, store) width pairs. */
#define GEN4(BASE, SRC, PRE, GAP)                   \
    GEN(BASE##_s_s, "flds", "fstps", SRC, PRE, GAP) \
    GEN(BASE##_s_d, "flds", "fstpl", SRC, PRE, GAP) \
    GEN(BASE##_d_s, "fldl", "fstps", SRC, PRE, GAP) \
    GEN(BASE##_d_d, "fldl", "fstpl", SRC, PRE, GAP)

#define S "(%[s])"
#define D(off) #off "(%[d])"

/* -- gap sequences that must fuse (gap0: the adjacent pair, fused when it is a whole run) -- */
GEN4(gap0, S, "", "")                                                      /* adjacent pair */
GEN4(gap1, S, "", "movaps %%xmm1, %%xmm2")                                 /* 1 SSE reg-only */
GEN4(gap2, S, "", "xorps %%xmm2, %%xmm2\n\taddss %%xmm1, %%xmm2")          /* 2 SSE reg-only */
GEN4(gap3, S, "", "addl $1, %%ecx\n\tmovl %%ecx, %%edx\n\tshll $1, %%edx") /* 3 integer */
GEN4(gap4, S, "",
     "cvtsi2ss %%ecx, %%xmm3\n\tmovd %%xmm3, %%eax\n\tleal 4(%%rcx), %%edx\n\tcmpl %%eax, %%edx")
/* the Far Cry 2 shape: SSE store before, disjoint SSE store inside */
GEN4(fc2, S, "movss %%xmm7, 0x20(%[d])\n\t", "movss %%xmm1, 0x4(%[d])")
/* disjoint loads and stores off the same base register */
GEN4(mem4, S, "",
     "movss 0x8(%[d]), %%xmm3\n\tmovss %%xmm3, 0x14(%[d])\n\tmovl %%ecx, 0x18(%[d])\n\tmovl "
     "0x1c(%[d]), %%edx")
/* FLD reads buf+0x10; the gap overwrites it: the load must see the old bytes */
GEN4(storesrc, D(0x10), "", "movl $0x12345678, 0x10(%[d])")
GEN4(adjacent_target, S, "", "movl $0x1, 0x2c(%[d])\n\tmovl $0x2, 0x38(%[d])") /* 0x2c+4, 0x30+8 */

/* -- gap sequences that must NOT fuse -- */
GEN4(five, S, "",
     "addl $1, %%ecx\n\taddl $2, %%ecx\n\taddl $3, %%ecx\n\taddl $4, %%ecx\n\taddl $5, %%ecx")
GEN4(n_dstwrite, S, "", "movl $0xAAAA5555, 0x30(%[d])")
GEN4(n_dstread, S, "", "movl 0x30(%[d]), %%eax\n\tmovl %%eax, 0x40(%[d])")
GEN4(n_dstoverlap, S, "", "movw $0x1234, 0x32(%[d])")
/* f64 target only: bytes 4..7 of it (for an f32 target this store is disjoint and would fuse) */
GEN(n_dstoverlap_hi_s_d, "flds", "fstpl", S, "", "movl $0x1234567, 0x34(%[d])")
GEN(n_dstoverlap_hi_d_d, "fldl", "fstpl", S, "", "movl $0x1234567, 0x34(%[d])")
GEN4(n_xmmstore_overlap, S, "", "movups %%xmm1, 0x28(%[d])") /* 16 bytes: 0x28..0x37 */
/* A second base register (r9) that holds the same address: the gap may alias
 * the target without the two operands naming the same register. */
#define GEN4A(BASE, GAP)                                            \
    GEN(BASE##_s_s, "flds", "fstps", S, "movq %[d], %%r9\n\t", GAP) \
    GEN(BASE##_s_d, "flds", "fstpl", S, "movq %[d], %%r9\n\t", GAP) \
    GEN(BASE##_d_s, "fldl", "fstps", S, "movq %[d], %%r9\n\t", GAP) \
    GEN(BASE##_d_d, "fldl", "fstpl", S, "movq %[d], %%r9\n\t", GAP)
GEN4A(n_aliasstore, "movl $0xdeadbeef, 0x30(%%r9)")
GEN4A(n_aliasload, "movl 0x30(%%r9), %%eax\n\tmovl %%eax, 0x44(%[d])")
GEN4A(n_aliasstore_far, "movl $0xdeadbeef, 0x80(%%r9)") /* different base: still unprovable */

/* the base register of the FSTP changes inside the gap */
#define GEN_BASEWRITE(NAME, LD, ST)                                                             \
    static __attribute__((noinline)) void NAME(void* d, const void* s) {                        \
        void* dd = d;                                                                           \
        __asm__ volatile(PRE_COMMON LD " (%[s])\n\taddq $0x10, %[dd]\n\t" ST " 0x30(%[dd])\n\t" \
                         : [dd] "+r"(dd)                                                        \
                         : [s] "r"(s)                                                           \
                         : CLOBBERS);                                                           \
    }
GEN_BASEWRITE(n_basewrite_s_s, "flds", "fstps")
GEN_BASEWRITE(n_basewrite_s_d, "flds", "fstpl")
GEN_BASEWRITE(n_basewrite_d_s, "fldl", "fstps")
GEN_BASEWRITE(n_basewrite_d_d, "fldl", "fstpl")

/* x87 inside the gap, a taken / untaken branch, a call */
GEN4(n_x87gap, S, "", "fld1\n\tfstp %%st(0)")
GEN4(n_x87gap_fnop, S, "", "fnop")
GEN4(n_fwait, S, "", "fwait")
GEN4(n_branch, S, "", "cmpl $5, %%ecx\n\tjz 1f\n\tmovl $1, %%edx\n1:")
GEN4(n_branch_taken, S, "", "cmpl $0x11223344, %%ecx\n\tjz 1f\n\tmovl $1, %%edx\n1:")

static __attribute__((noinline)) void nothing_helper(void) {
    __asm__ volatile("" ::: "memory");
}
#define GEN_CALL(NAME, LD, ST)                                                             \
    static __attribute__((noinline)) void NAME(void* d, const void* s) {                   \
        register void* rd __asm__("r12") = d;                                              \
        register const void* rs __asm__("r13") = s;                                        \
        __asm__ volatile(                                                                  \
            PRE_COMMON LD                                                                  \
            " (%%r13)\n\t"                                                                 \
            "subq $256, %%rsp\n\tcall *%[h]\n\taddq $256, %%rsp\n\t" ST " 0x30(%%r12)\n\t" \
            : "+r"(rd), "+r"(rs)                                                           \
            : [h] "r"(nothing_helper)                                                      \
            : CLOBBERS, "rsi", "rdi", "r8", "r10", "r11", "xmm0", "xmm4", "xmm5", "xmm6"); \
    }
GEN_CALL(n_call_s_s, "flds", "fstps")
GEN_CALL(n_call_s_d, "flds", "fstpl")
GEN_CALL(n_call_d_s, "fldl", "fstps")
GEN_CALL(n_call_d_d, "fldl", "fstpl")

/* ---- state-sensitive cases (hand written) -------------------------------- */

/* A jump into the middle of the gap from another path: the entry at the label
 * is its own block, in which the FSTP has no FLD. */
static __attribute__((noinline)) void midentry(void* d, const void* s, const void* s2, int path) {
    __asm__ volatile(PRE_COMMON
                     "testl %[p], %[p]\n\t"
                     "jz 3f\n\t"
                     "flds (%[s])\n\t"
                     "movaps %%xmm1, %%xmm2\n\t"
                     "2:\n\t"
                     "movaps %%xmm7, %%xmm3\n\t"
                     "fstps 0x30(%[d])\n\t"
                     "jmp 4f\n\t"
                     "3:\n\t"
                     "flds (%[s2])\n\t"
                     "jmp 2b\n\t"
                     "4:\n\t"
                     :
                     : [d] "r"(d), [s] "r"(s), [s2] "r"(s2), [p] "r"(path)
                     : CLOBBERS);
}

/* Two consecutive copies, each with its own gap. */
static __attribute__((noinline)) void two_pairs(void* d, const void* s) {
    __asm__ volatile(PRE_COMMON
                     "flds (%[s])\n\t"
                     "movss %%xmm1, 0x4(%[d])\n\t"
                     "fstps 0x30(%[d])\n\t"
                     "flds 4(%[s])\n\t"
                     "movss %%xmm7, 0x8(%[d])\n\t"
                     "fstpl 0x38(%[d])\n\t"
                     :
                     : [d] "r"(d), [s] "r"(s)
                     : CLOBBERS);
}

/* RIP-relative (absolute) operands */
static float g_abs_src_f;
static double g_abs_src_d;
static float g_abs_dst_f;
static uint32_t g_abs_other;
static __attribute__((noinline)) void abs_gap_store_f(void) {
    __asm__ volatile(PRE_COMMON "flds %[sf]\n\tmovl %%ecx, %[o]\n\tfstps %[df]\n\t"
                     : [df] "=m"(g_abs_dst_f), [o] "=m"(g_abs_other)
                     : [sf] "m"(g_abs_src_f)
                     : CLOBBERS);
}
static __attribute__((noinline)) void abs_gap_store_d(void) {
    __asm__ volatile(PRE_COMMON "fldl %[sd]\n\tmovss %%xmm1, %[o]\n\tfstps %[df]\n\t"
                     : [df] "=m"(g_abs_dst_f), [o] "=m"(g_abs_other)
                     : [sd] "m"(g_abs_src_d)
                     : CLOBBERS);
}
static __attribute__((noinline)) void abs_gap_regmem(void* d) { /* different kinds: unprovable */
    __asm__ volatile(PRE_COMMON "flds %[sf]\n\tmovl %%ecx, 0x4(%[d])\n\tfstps %[df]\n\t"
                     : [df] "=m"(g_abs_dst_f)
                     : [sf] "m"(g_abs_src_f), [d] "r"(d)
                     : CLOBBERS);
}
static __attribute__((noinline)) void abs_gap_overwrite(void) { /* gap stores to the target */
    __asm__ volatile(PRE_COMMON "flds %[sf]\n\tmovl $0x7777, %[df]\n\tfstps %[df]\n\t"
                     : [df] "=m"(g_abs_dst_f)
                     : [sf] "m"(g_abs_src_f)
                     : CLOBBERS);
}

/* ---- expectations for gap side effects ----------------------------------- */

static void put32(uint8_t* img, int off, uint32_t v) {
    memcpy(img + off, &v, 4);
}
static void ex_none(uint8_t* img) {
    (void)img;
}
static void ex_fc2(uint8_t* img) {
    put32(img, 0x20, 0x55667788);
    put32(img, 0x4, 0x11223344);
}
static void ex_mem4(uint8_t* img) {
    /* movss 8(d)->xmm3 ; movss xmm3->0x14(d) ; movl ecx->0x18 ; movl 0x1c->edx (no memory write) */
    memcpy(img + 0x14, img + 0x8, 4);
    put32(img, 0x18, 0x11223344);
}
static void ex_storesrc(uint8_t* img) {
    put32(img, 0x10, 0x12345678);
}
static void ex_adjacent_target(uint8_t* img) {
    put32(img, 0x2c, 1);
    put32(img, 0x38, 2);
}
static void ex_dstwrite(uint8_t* img) {
    put32(img, 0x30, 0xAAAA5555);
}
static void ex_dstread(uint8_t* img) {
    memcpy(img + 0x40, img + 0x30, 4); /* reads the OLD target */
}
static void ex_dstoverlap(uint8_t* img) {
    uint16_t v = 0x1234;
    memcpy(img + 0x32, &v, 2);
}
static void ex_dstoverlap_hi(uint8_t* img) {
    put32(img, 0x34, 0x1234567);
}
static void ex_xmmstore_overlap(uint8_t* img) {
    uint8_t v[16] = {0x44, 0x33, 0x22, 0x11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    memcpy(img + 0x28, v, 16);
}
static void ex_aliasstore(uint8_t* img) {
    put32(img, 0x30, 0xdeadbeef);
}
static void ex_aliasload(uint8_t* img) {
    memcpy(img + 0x44, img + 0x30, 4);
}
static void ex_aliasstore_far(uint8_t* img) {
    put32(img, 0x80, 0xdeadbeef);
}

#define C4(BASE, FD, FS, EX, DOFF, SOFF)                            \
    {#BASE "_s_s", BASE##_s_s, 0, 0, DOFF, SOFF, FD, FS, EX},       \
        {#BASE "_s_d", BASE##_s_d, 0, 1, DOFF, SOFF, FD, FS, EX},   \
        {#BASE "_d_s", BASE##_d_s, 1, 0, DOFF, SOFF, FD, FS, EX}, { \
        #BASE "_d_d", BASE##_d_d, 1, 1, DOFF, SOFF, FD, FS, EX      \
    }

static const test_case cases[] = {
    C4(gap0, 1, 1, ex_none, 0x30, 0),
    C4(gap1, 1, 1, ex_none, 0x30, 0),
    C4(gap2, 1, 1, ex_none, 0x30, 0),
    C4(gap3, 1, 1, ex_none, 0x30, 0),
    C4(gap4, 1, 1, ex_none, 0x30, 0),
    C4(fc2, 1, 0, ex_fc2, 0x30, 0),
    C4(mem4, 1, 0, ex_mem4, 0x30, 0),
    C4(storesrc, 1, 0, ex_storesrc, 0x30, 0x10),
    C4(adjacent_target, 1, 0, ex_adjacent_target, 0x30, 0),
    C4(five, 0, 0, ex_none, 0x30, 0),
    C4(n_dstwrite, 0, 0, ex_dstwrite, 0x30, 0),
    C4(n_dstread, 0, 0, ex_dstread, 0x30, 0),
    C4(n_dstoverlap, 0, 0, ex_dstoverlap, 0x30, 0),
    {"n_dstoverlap_hi_s_d", n_dstoverlap_hi_s_d, 0, 1, 0x30, 0, 0, 0, ex_dstoverlap_hi},
    {"n_dstoverlap_hi_d_d", n_dstoverlap_hi_d_d, 1, 1, 0x30, 0, 0, 0, ex_dstoverlap_hi},
    C4(n_xmmstore_overlap, 0, 0, ex_xmmstore_overlap, 0x30, 0),
    C4(n_aliasstore, 0, 0, ex_aliasstore, 0x30, 0),
    C4(n_aliasload, 0, 0, ex_aliasload, 0x30, 0),
    C4(n_aliasstore_far, 0, 0, ex_aliasstore_far, 0x30, 0),
    C4(n_basewrite, 0, 0, ex_none, 0x40, 0),
    C4(n_x87gap, 0, 0, ex_none, 0x30, 0),
    C4(n_x87gap_fnop, 0, 0, ex_none, 0x30, 0),
    C4(n_fwait, 0, 0, ex_none, 0x30, 0),
    C4(n_branch, 0, 0, ex_none, 0x30, 0),
    C4(n_branch_taken, 0, 0, ex_none, 0x30, 0),
    C4(n_call, 0, 0, ex_none, 0x30, 0),
};
#define NCASES ((int)(sizeof(cases) / sizeof(cases[0])))

/* ---- running a case ------------------------------------------------------ */

static uint8_t bufmem[512] __attribute__((aligned(64)));
static uint8_t srcmem[64] __attribute__((aligned(64)));

static void read_env(env_t* e) {
    uint8_t env[28] __attribute__((aligned(16)));
    __asm__ volatile("fnstenv %0" : "=m"(env)::"memory");
    e->cw = (uint16_t)(env[0] | (env[1] << 8));
    e->sw = (uint16_t)(env[4] | (env[5] << 8));
    e->tw = (uint16_t)(env[8] | (env[9] << 8));
}

static void set_cw(uint16_t cw) {
    __asm__ volatile("fldcw %0" : : "m"(cw) : "memory");
}

static uint64_t expected_dst(const test_case* c, uint64_t in) {
    uint64_t dst;
    if (!c->load_is_f64) {
        dst = c->store_is_f64 ? widen((uint32_t)in) : (uint64_t)narrow(widen((uint32_t)in));
    } else {
        /* hardware: loading a signalling NaN quiets it, so an f64 copy sets the quiet bit */
        dst = c->store_is_f64 ? (is_snan64(in) ? in | 0x0008000000000000ULL : in)
                              : (uint64_t)narrow(in);
    }
    return dst;
}

/* The unfused sidecar path keeps an f64 signalling NaN as it is when it is loaded
 * and stored as f64 (it does a plain 64-bit copy); stock Rosetta, like hardware,
 * quiets it.  That is not a fusion concern (the fused copy is the same plain
 * copy), so both results are accepted. */
static int dst_matches(const test_case* c, uint64_t in, uint64_t got) {
    const uint64_t want = expected_dst(c, in);
    if (got == want) {
        return 1;
    }
    if (c->load_is_f64 && c->store_is_f64 && is_snan64(in) && got == in) {
        return 1;
    }
    return 0;
}

static void run_case(const test_case* c, uint64_t in, uint16_t cw, uint8_t* out_img, env_t* e) {
    memset(bufmem, 0xA5, sizeof(bufmem));
    memset(srcmem, 0, sizeof(srcmem));
    if (c->load_is_f64) {
        memcpy(srcmem, &in, 8);
        memcpy(bufmem + 0x10, &in, 8);
    } else {
        uint32_t v = (uint32_t)in;
        memcpy(srcmem, &v, 4);
        memcpy(bufmem + 0x10, &v, 4);
    }
    FNINIT();
    if (cw != 0x037F) {
        set_cw(cw);
    }
    c->fn(bufmem, srcmem);
    read_env(e);
    memcpy(out_img, bufmem, 256);
    FNINIT();
}

static void check_case(const test_case* c) {
    static const uint16_t cws[] = {0x037F, 0x027F};
    const int n = c->load_is_f64 ? N64 : N32;
    int bad = 0;
    int first_bad = -1;
    for (int ci = 0; ci < 2; ci++) {
        for (int i = 0; i < n; i++) {
            const uint64_t in = c->load_is_f64 ? f64_inputs[i] : (uint64_t)f32_inputs[i];
            uint8_t img[256];
            env_t e;
            run_case(c, in, cws[ci], img, &e);

            uint8_t want[256];
            memset(want, 0xA5, sizeof(want));
            if (c->load_is_f64) {
                memcpy(want + 0x10, &in, 8);
            } else {
                uint32_t v = (uint32_t)in;
                memcpy(want + 0x10, &v, 4);
            }
            c->expect(want);
            const uint64_t got64 = *(const uint64_t*)(img + c->dst_off);
            const uint32_t got32 = *(const uint32_t*)(img + c->dst_off);
            const uint64_t exp = expected_dst(c, in);
            int ok;
            if (c->store_is_f64) {
                ok = dst_matches(c, in, got64);
                memcpy(want + c->dst_off, &got64, 8);
            } else {
                ok = dst_matches(c, in, got32);
                memcpy(want + c->dst_off, &got32, 4);
            }
            /* everything else in the buffer must be exactly what the gap left */
            if (memcmp(img, want, 256) != 0) {
                ok = 0;
            }
            /* x87 state: control word untouched, stack empty again */
            if (e.cw != cws[ci] || ((e.sw >> 11) & 7) != 0 || e.tw != 0xFFFF) {
                ok = 0;
            }
            if (!ok) {
                if (first_bad < 0) {
                    first_bad = i;
                    printf(
                        "  first mismatch: cw=%04x in=%016llx got=%016llx want=%016llx env cw=%04x "
                        "sw=%04x tw=%04x\n",
                        cws[ci], (unsigned long long)in,
                        (unsigned long long)(c->store_is_f64 ? got64 : got32),
                        (unsigned long long)exp, e.cw, e.sw, e.tw);
                    for (int b = 0; b < 256; b++) {
                        if (img[b] != want[b]) {
                            printf("    buf[0x%02x] got %02x want %02x\n", b, img[b], want[b]);
                            break;
                        }
                    }
                }
                bad++;
            }
        }
    }
    if (bad) {
        printf("FAIL  %s (%d of %d inputs)\n", c->name, bad, 2 * n);
        failures++;
    } else {
        printf("PASS  %s (%d inputs x 2 control words)\n", c->name, n);
    }
}

static void dump_case(const test_case* c) {
    const int n = c->load_is_f64 ? N64 : N32;
    for (int i = 0; i < n; i++) {
        const uint64_t in = c->load_is_f64 ? f64_inputs[i] : (uint64_t)f32_inputs[i];
        uint8_t img[256];
        env_t e;
        run_case(c, in, 0x037F, img, &e);
        uint64_t dst = 0;
        memcpy(&dst, img + c->dst_off, c->store_is_f64 ? 8 : 4);
        printf("D %s in=%016llx dst=%016llx cw=%04x sw=%04x tw=%04x img=", c->name,
               (unsigned long long)in, (unsigned long long)dst, e.cw, e.sw, e.tw);
        for (int b = 0; b < 0x60; b++) {
            printf("%02x", img[b]);
        }
        printf("\n");
    }
}

/* ---- live x87 stack around a copy ---------------------------------------- */

/* Push `depth` known values, do a fused-shape copy, check ST(0..depth-1) and the
 * state, pop them.  An integer instruction before the FLD keeps it a run start. */
#define STACK_CASE(NAME, PUSHES, NPUSH)                                                   \
    static __attribute__((noinline)) void NAME(void* d, const void* s, const double* v) { \
        __asm__ volatile(PRE_COMMON PUSHES                                                \
                         "xorl %%eax, %%eax\n\t"                                          \
                         "flds (%[s])\n\t"                                                \
                         "movaps %%xmm1, %%xmm2\n\t"                                      \
                         "fstps 0x30(%[d])\n\t"                                           \
                         "fnstenv 0x60(%[d])\n\t"                                         \
                         :                                                                \
                         : [d] "r"(d), [s] "r"(s), [v] "r"(v)                             \
                         : CLOBBERS);                                                     \
    }
STACK_CASE(stack1, "fldl 0(%[v])\n\t", 1)
STACK_CASE(stack3, "fldl 0(%[v])\n\tfldl 8(%[v])\n\tfldl 16(%[v])\n\t", 3)
STACK_CASE(
    stack7,
    "fldl 0(%[v])\n\tfldl 8(%[v])\n\tfldl 16(%[v])\n\tfldl 24(%[v])\n\tfldl 32(%[v])\n\tfldl "
    "40(%[v])\n\tfldl 48(%[v])\n\t",
    7)

typedef void (*stack_fn)(void*, const void*, const double*);

static void check_stack(const char* name, stack_fn fn, int depth) {
    static const double v[8] = {11.5, -22.25, 33.125, -44.0625, 55.5, -66.75, 77.875, -88.5};
    const uint32_t ins[] = {0x3fc00000, 0x7f800001, 0xff800000, 0x00000001, 0x80000000};
    int bad = 0;
    for (size_t k = 0; k < sizeof(ins) / sizeof(ins[0]); k++) {
        memset(bufmem, 0xA5, sizeof(bufmem));
        memcpy(srcmem, &ins[k], 4);
        FNINIT();
        fn(bufmem, srcmem, v);
        env_t e;
        /* fnstenv wrote the image at +0x60: read it from there, then drain */
        e.cw = (uint16_t)(bufmem[0x60] | (bufmem[0x61] << 8));
        e.sw = (uint16_t)(bufmem[0x64] | (bufmem[0x65] << 8));
        e.tw = (uint16_t)(bufmem[0x68] | (bufmem[0x69] << 8));
        double got[8] = {0};
        for (int i = 0; i < depth; i++) {
            __asm__ volatile("fstpl %0" : "=m"(got[i])::"memory");
        }
        env_t after;
        read_env(&after);
        const int top = (8 - depth) & 7;
        /* tag word: each of the `depth` occupied physical slots (top..top+depth-1) is 00 */
        uint16_t tw = 0xFFFF;
        for (int i = 0; i < depth; i++) {
            tw &= (uint16_t)~(3u << (2 * ((top + i) & 7)));
        }
        int ok = ((e.sw >> 11) & 7) == (unsigned)top && e.tw == tw && e.cw == 0x037F;
        for (int i = 0; i < depth; i++) {
            ok = ok && d2u(got[i]) == d2u(v[depth - 1 - i]);
        }
        ok = ok && *(uint32_t*)(bufmem + 0x30) == narrow(widen(ins[k])) &&
             ((after.sw >> 11) & 7) == 0 && after.tw == 0xFFFF;
        if (!ok) {
            printf("  stack depth %d in=%08x: top=%u (want %d) tw=%04x (want %04x) cw=%04x\n",
                   depth, ins[k], (e.sw >> 11) & 7, top, e.tw, tw, e.cw);
            bad++;
        }
        if (dump_mode) {
            printf("D %s in=%08x dst=%08x cw=%04x sw=%04x tw=%04x\n", name, ins[k],
                   *(uint32_t*)(bufmem + 0x30), e.cw, e.sw, e.tw);
        }
    }
    if (!dump_mode) {
        if (bad) {
            printf("FAIL  %s\n", name);
            failures++;
        } else {
            printf("PASS  %s (live stack depth %d, copy leaves it intact)\n", name, depth);
        }
    }
}

/* Eight values already on the stack: the push overflows.  Hardware raises a
 * stack fault; neither the unfused sidecar path nor the fusion models that, so
 * this is a documented deviation and is only reported in --dump mode. */
static __attribute__((noinline)) void overflow_copy(void* d, const void* s, const double* v) {
    __asm__ volatile(PRE_COMMON
                     "fldl 0(%[v])\n\tfldl 8(%[v])\n\tfldl 16(%[v])\n\tfldl 24(%[v])\n\t"
                     "fldl 32(%[v])\n\tfldl 40(%[v])\n\tfldl 48(%[v])\n\tfldl 56(%[v])\n\t"
                     "xorl %%eax, %%eax\n\t"
                     "flds (%[s])\n\t"
                     "movaps %%xmm1, %%xmm2\n\t"
                     "fstps 0x30(%[d])\n\t"
                     "fnstenv 0x60(%[d])\n\t"
                     :
                     : [d] "r"(d), [s] "r"(s), [v] "r"(v)
                     : CLOBBERS);
}

/* ---- remaining hand-written checks --------------------------------------- */

/* A non-popping or register-destination store, FILD and FLD ST(i) heads are not
 * float copies; the translator must leave them alone and get them right. */
static void check_non_copies(void) {
    int bad = 0;
    const uint32_t in = 0x40490fdb;
    env_t e;

    /* FST m32 (no pop): ST(0) must still hold the value afterwards */
    {
        double again = 0.0;
        memset(bufmem, 0xA5, 64);
        memcpy(srcmem, &in, 4);
        FNINIT();
        __asm__ volatile(PRE_COMMON
                         "flds (%[s])\n\tmovaps %%xmm1, %%xmm2\n\tfsts 0x30(%[d])\n\t"
                         "xorl %%eax, %%eax\n\tfstpl %[a]\n\t"
                         : [a] "=m"(again)
                         : [d] "r"(bufmem), [s] "r"(srcmem)
                         : CLOBBERS);
        read_env(&e);
        if (*(uint32_t*)(bufmem + 0x30) != in || d2u(again) != widen(in) || e.tw != 0xFFFF) {
            printf("  fst non-pop: dst=%08x again=%016llx tw=%04x\n", *(uint32_t*)(bufmem + 0x30),
                   (unsigned long long)d2u(again), e.tw);
            bad++;
        }
    }
    /* FILD head: an integer conversion, not a float copy */
    {
        const int32_t iv = -123456789;
        memset(bufmem, 0xA5, 64);
        memcpy(srcmem, &iv, 4);
        FNINIT();
        __asm__ volatile(PRE_COMMON "fildl (%[s])\n\tmovaps %%xmm1, %%xmm2\n\tfstps 0x30(%[d])\n\t"
                         :
                         : [d] "r"(bufmem), [s] "r"(srcmem)
                         : CLOBBERS);
        volatile float want = (float)iv;
        read_env(&e);
        if (*(uint32_t*)(bufmem + 0x30) != f2u(want) || e.tw != 0xFFFF) {
            printf("  fild head: dst=%08x want=%08x\n", *(uint32_t*)(bufmem + 0x30), f2u(want));
            bad++;
        }
    }
    /* FLD ST(0) head: duplicates the top of the stack */
    {
        float a = 0;
        double b = 0;
        const double seed = 6.5;
        FNINIT();
        __asm__ volatile(
            "fldl %[seed]\n\txorl %%eax, %%eax\n\tfld %%st(0)\n\tmovaps %%xmm1, %%xmm2\n\t"
            "fstps %[a]\n\tfstpl %[b]\n\t"
            : [a] "=m"(a), [b] "=m"(b)
            : [seed] "m"(seed)
            : "rax", "xmm1", "xmm2", "memory");
        read_env(&e);
        if (f2u(a) != f2u(6.5f) || d2u(b) != d2u(6.5) || e.tw != 0xFFFF) {
            printf("  fld st(0) head: a=%08x b=%016llx\n", f2u(a), (unsigned long long)d2u(b));
            bad++;
        }
    }
    /* FSTP ST(1) tail: the register form is not a memory copy */
    {
        double r = 0;
        const double seed = 1.25;
        const uint32_t v = f2u(8.75f);
        memcpy(srcmem, &v, 4);
        FNINIT();
        __asm__ volatile(
            "fldl %[seed]\n\txorl %%eax, %%eax\n\tflds (%[s])\n\tmovaps %%xmm1, %%xmm2\n\t"
            "fstp %%st(1)\n\tfstpl %[r]\n\t"
            : [r] "=m"(r)
            : [seed] "m"(seed), [s] "r"(srcmem)
            : "rax", "xmm1", "xmm2", "memory");
        read_env(&e);
        if (d2u(r) != d2u(8.75) || e.tw != 0xFFFF) {
            printf("  fstp st(1) tail: r=%016llx\n", (unsigned long long)d2u(r));
            bad++;
        }
    }
    if (bad) {
        printf("FAIL  non_copies (%d)\n", bad);
        failures++;
    } else {
        printf("PASS  non_copies (FST m32, FILD head, FLD ST(0) head, FSTP ST(1) tail)\n");
    }
}

static void check_two_pairs(void) {
    int bad = 0;
    const uint32_t in[2] = {0x3fc00000, 0xc0a00000};
    for (int rep = 0; rep < 3; rep++) {
        memset(bufmem, 0xA5, 128);
        memcpy(srcmem, in, 8);
        FNINIT();
        two_pairs(bufmem, srcmem);
        env_t e;
        read_env(&e);
        if (*(uint32_t*)(bufmem + 0x30) != in[0] || *(uint64_t*)(bufmem + 0x38) != widen(in[1]) ||
            *(uint32_t*)(bufmem + 0x4) != 0x11223344 || *(uint32_t*)(bufmem + 0x8) != 0x55667788 ||
            e.tw != 0xFFFF || ((e.sw >> 11) & 7) != 0) {
            bad++;
        }
    }
    EXPECT_FUSED(2, 0); /* the second FLD follows the first copy's FSTP: chained */
    if (bad) {
        printf("FAIL  two_pairs\n");
        failures++;
    } else {
        printf("PASS  two_pairs (two consecutive fused copies)\n");
    }
}

static void check_midentry(void) {
    int bad = 0;
    const uint32_t a = f2u(1.5f);
    const uint32_t b = f2u(-7.25f);
    static uint8_t s1[16] __attribute__((aligned(16)));
    static uint8_t s2[16] __attribute__((aligned(16)));
    memcpy(s1, &a, 4);
    memcpy(s2, &b, 4);
    for (int path = 0; path < 2; path++) {
        for (int rep = 0; rep < 3; rep++) {
            memset(bufmem, 0xA5, 64);
            FNINIT();
            midentry(bufmem, s1, s2, path);
            env_t e;
            read_env(&e);
            const uint32_t want = path ? a : b;
            if (*(uint32_t*)(bufmem + 0x30) != want || e.tw != 0xFFFF || ((e.sw >> 11) & 7) != 0) {
                printf("  path %d: dst=%08x want=%08x tw=%04x\n", path, *(uint32_t*)(bufmem + 0x30),
                       want, e.tw);
                bad++;
            }
        }
    }
    EXPECT_FUSED(1, 1); /* path A's straight-line block holds the whole FLD...FSTP */
    if (bad) {
        printf("FAIL  midentry\n");
        failures++;
    } else {
        printf("PASS  midentry (jump into the gap from another path)\n");
    }
}

static void check_abs(void) {
    int bad = 0;
    for (int i = 0; i < N32; i++) {
        g_abs_src_f = u2f(f32_inputs[i]);
        g_abs_other = 0;
        g_abs_dst_f = 0;
        FNINIT();
        abs_gap_store_f();
        env_t e;
        read_env(&e);
        if (f2u(g_abs_dst_f) != narrow(widen(f32_inputs[i])) || g_abs_other != 0x11223344 ||
            e.tw != 0xFFFF || ((e.sw >> 11) & 7) != 0) {
            bad++;
        }
    }
    for (int i = 0; i < N64; i++) {
        g_abs_src_d = u2d(f64_inputs[i]);
        g_abs_other = 0;
        FNINIT();
        abs_gap_store_d();
        env_t e;
        read_env(&e);
        if (f2u(g_abs_dst_f) != narrow(f64_inputs[i]) || g_abs_other != 0x11223344 ||
            e.tw != 0xFFFF) {
            bad++;
        }
    }
    {
        static uint8_t area[32] __attribute__((aligned(16)));
        g_abs_src_f = 2.5f;
        FNINIT();
        abs_gap_regmem(area);
        if (f2u(g_abs_dst_f) != f2u(2.5f) || *(uint32_t*)(area + 4) != 0x11223344) {
            bad++;
        }
        FNINIT();
        abs_gap_overwrite();
        if (f2u(g_abs_dst_f) != f2u(2.5f)) { /* the FSTP lands after the gap store */
            bad++;
        }
    }
    EXPECT_FUSED(2, 0); /* abs_gap_store_f and abs_gap_store_d; the other two are unprovable */
    if (bad) {
        printf("FAIL  absolute_operands (%d)\n", bad);
        failures++;
    } else {
        printf("PASS  absolute_operands (rip-relative source, target and gap)\n");
    }
}

int main(int argc, char** argv) {
    /* The sidecar writes its own diagnostics to the same stdout: one write per
     * line keeps them from landing in the middle of ours. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump") == 0) {
            dump_mode = 1;
        }
    }

    if (dump_mode) {
        for (int i = 0; i < NCASES; i++) {
            dump_case(&cases[i]);
        }
        check_stack("stack1", stack1, 1);
        check_stack("stack3", stack3, 3);
        check_stack("stack7", stack7, 7);
        {
            static const double v[8] = {11.5, -22.25, 33.125, -44.0625,
                                        55.5, -66.75, 77.875, -88.5};
            const uint32_t ins[] = {0x3fc00000, 0x7f800001, 0x00000001};
            for (size_t k = 0; k < 3; k++) {
                memset(bufmem, 0xA5, sizeof(bufmem));
                memcpy(srcmem, &ins[k], 4);
                FNINIT();
                overflow_copy(bufmem, srcmem, v);
                printf("D overflow8 in=%08x dst=%08x cw=%04x sw=%04x tw=%04x\n", ins[k],
                       *(uint32_t*)(bufmem + 0x30), (uint16_t)(bufmem[0x60] | (bufmem[0x61] << 8)),
                       (uint16_t)(bufmem[0x64] | (bufmem[0x65] << 8)),
                       (uint16_t)(bufmem[0x68] | (bufmem[0x69] << 8)));
                FNINIT();
            }
        }
        return 0;
    }

    int expect_default = 0;
    int expect_strict = 0;
    for (int i = 0; i < NCASES; i++) {
        check_case(&cases[i]);
        expect_default += cases[i].fuse_default;
        expect_strict += cases[i].fuse_strict;
    }
    check_stack("stack1", stack1, 1);
    check_stack("stack3", stack3, 3);
    check_stack("stack7", stack7, 7);
    EXPECT_FUSED(3, 3);
    check_non_copies();
    check_two_pairs();
    check_midentry();
    check_abs();
    printf("GAPFUSE_EXPECT default=%d strict=%d\n", expect_default + hand_fuse_default,
           expect_strict + hand_fuse_strict);
    printf("%s\n", failures ? "SOME FAILED" : "ALL PASS");
    return failures ? 1 : 0;
}
