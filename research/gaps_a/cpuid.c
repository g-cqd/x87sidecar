/* cpuid dump, advertised-vs-executable feature check, xgetbv/fxsave/xsave fidelity, rdtsc rate. */
#include "gaps.h"
#include <time.h>
#include <stdalign.h>

static void cpuid(uint32_t l, uint32_t s, uint32_t r[4]) {
    asm volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "0"(l), "2"(s));
}
static alignas(16) unsigned char buf16[64];

#define F(n) static void f_##n(void)
F(sse3){asm volatile("haddps %%xmm0,%%xmm0":::"memory");}
F(ssse3){asm volatile("pshufb %%xmm0,%%xmm0":::"memory");}
F(sse41){asm volatile("pmulld %%xmm0,%%xmm0":::"memory");}
F(sse42){asm volatile("pcmpgtq %%xmm0,%%xmm0\n\tcrc32b %%al,%%eax":::"memory","rax");}
F(popcnt){asm volatile("popcnt %%rax,%%rax":::"rax");}
F(aes){asm volatile("aesenc %%xmm0,%%xmm0":::"memory");}
F(pclmul){asm volatile("pclmulqdq $0,%%xmm0,%%xmm0":::"memory");}
F(avx){asm volatile("vaddps %%ymm0,%%ymm0,%%ymm0":::"memory");}
F(avx2){asm volatile("vpaddd %%ymm0,%%ymm0,%%ymm0":::"memory");}
F(fma){asm volatile("vfmadd231ps %%xmm0,%%xmm0,%%xmm0":::"memory");}
F(f16c){asm volatile("vcvtph2ps %%xmm0,%%xmm0":::"memory");}
F(bmi1){asm volatile("andn %%rax,%%rax,%%rax":::"rax");}
F(bmi2){asm volatile("pdep %%rax,%%rax,%%rax":::"rax");}
F(lzcnt){asm volatile("lzcnt %%rax,%%rax":::"rax");}
F(movbe){asm volatile("movbe (%0),%%rax"::"r"(buf16):"rax");}
F(rdrand){asm volatile("rdrand %%rax":::"rax","cc");}
F(rdseed){asm volatile("rdseed %%rax":::"rax","cc");}
F(adx){asm volatile("adcx %%rax,%%rax":::"rax","cc");}
F(sha){asm volatile("sha1msg1 %%xmm0,%%xmm0":::"memory");}
F(avx512f){asm volatile("vpaddd %%zmm0,%%zmm0,%%zmm0":::"memory");}
F(avx512bw){asm volatile("vpaddb %%zmm0,%%zmm0,%%zmm0":::"memory");}
F(avx512vl){asm volatile("vpaddd %%ymm16,%%ymm16,%%ymm16":::"memory");}
F(rdtscp){asm volatile("rdtscp":::"rax","rcx","rdx");}
F(clflush){asm volatile("clflush (%0)"::"r"(buf16):"memory");}
F(clflushopt){asm volatile("clflushopt (%0)"::"r"(buf16):"memory");}
F(cx16){asm volatile("xor %%eax,%%eax\n\txor %%edx,%%edx\n\txor %%ebx,%%ebx\n\txor %%ecx,%%ecx\n\tcmpxchg16b (%0)"::"r"(buf16):"rax","rbx","rcx","rdx","cc","memory");}
F(lahf64){asm volatile("lahf\n\tsahf":::"rax","cc");}
F(prefetchw){asm volatile("prefetchw (%0)"::"r"(buf16));}
F(femms){asm volatile(".byte 0x0f,0x0e":::"memory");}
F(fsgsbase){asm volatile("rdfsbase %%rax":::"rax");}
F(xsave){asm volatile("xor %%edx,%%edx\n\tmov $3,%%eax\n\txsave (%0)"::"r"(buf16):"rax","rdx","memory");}
F(xgetbv){asm volatile("xor %%ecx,%%ecx\n\txgetbv":::"rax","rcx","rdx");}
F(mwait){asm volatile("monitor":::"memory");}
F(erms_repmovsb){asm volatile("cld":::);}
F(sse4a){asm volatile("movntss %%xmm0,(%0)"::"r"(buf16):"memory");}
F(pause){asm volatile("pause");}
F(movnti){asm volatile("movnti %%rax,(%0)"::"r"(buf16):"memory");}
F(tzcnt){asm volatile("tzcnt %%rax,%%rax":::"rax");}
F(clwb){asm volatile("clwb (%0)"::"r"(buf16):"memory");}
F(vpclmul){asm volatile("vpclmulqdq $0,%%ymm0,%%ymm0,%%ymm0":::"memory");}
F(gfni){asm volatile("gf2p8mulb %%xmm0,%%xmm0":::"memory");}
F(avx_vnni){asm volatile("vpdpbusd %%ymm0,%%ymm0,%%ymm0":::"memory");}
F(avx512vnni){asm volatile("vpdpbusd %%zmm0,%%zmm0,%%zmm0":::"memory");}
F(invpcid){asm volatile("nop");}

struct feat { const char *name; int leaf; int reg; int bit; void (*fn)(void); };
/* reg: 0 eax 1 ebx 2 ecx 3 edx; leaf codes: 1 = leaf1, 7 = leaf7.0, 0x80000001, 0x80000007 */
static struct feat feats[] = {
 {"SSE3",1,2,0,f_sse3},{"PCLMULQDQ",1,2,1,f_pclmul},{"SSSE3",1,2,9,f_ssse3},{"FMA",1,2,12,f_fma},{"CMPXCHG16B",1,2,13,f_cx16},
 {"SSE4.1",1,2,19,f_sse41},{"SSE4.2",1,2,20,f_sse42},{"MOVBE",1,2,22,f_movbe},{"POPCNT",1,2,23,f_popcnt},{"AES",1,2,25,f_aes},
 {"XSAVE",1,2,26,f_xsave},{"OSXSAVE",1,2,27,f_xgetbv},{"AVX",1,2,28,f_avx},{"F16C",1,2,29,f_f16c},{"RDRAND",1,2,30,f_rdrand},
 {"CLFSH",1,3,19,f_clflush},
 {"FSGSBASE",7,1,0,f_fsgsbase},{"BMI1",7,1,3,f_bmi1},{"AVX2",7,1,5,f_avx2},{"BMI2",7,1,8,f_bmi2},{"AVX512F",7,1,16,f_avx512f},
 {"RDSEED",7,1,18,f_rdseed},{"ADX",7,1,19,f_adx},{"CLFLUSHOPT",7,1,23,f_clflushopt},{"CLWB",7,1,24,f_clwb},{"SHA",7,1,29,f_sha},
 {"AVX512BW",7,1,30,f_avx512bw},{"AVX512VL",7,1,31,f_avx512vl},{"GFNI",7,2,8,f_gfni},{"VPCLMULQDQ",7,2,10,f_vpclmul},{"AVX512VNNI",7,2,11,f_avx512vnni},
 {"LAHF64",0x80000001,2,0,f_lahf64},{"LZCNT",0x80000001,2,5,f_lzcnt},{"PREFETCHW",0x80000001,2,8,f_prefetchw},{"SSE4A",0x80000001,2,6,f_sse4a},
 {"RDTSCP",0x80000001,3,27,f_rdtscp},{"3DNOW(femms)",0x80000001,3,31,f_femms},
};

static void xmm_rt(void) {
    alignas(16) unsigned char img[512] = {0}; alignas(16) unsigned char pat[16];
    for (int i = 0; i < 16; i++) pat[i] = 0xA0 + i;
    double d = 1.5;
    asm volatile("movdqu (%0),%%xmm3\n\tfldl (%1)\n\tfxsave (%2)\n\tfstp %%st(0)" :: "r"(pat), "r"(&d), "r"(img) : "memory", "xmm3");
    int ok = !memcmp(img + 160 + 48, pat, 16);
    uint64_t m; memcpy(&m, img + 32, 8); uint16_t se; memcpy(&se, img + 40, 2);
    printf("  fxsave: xmm3 at +0xd0 %s; ST0 mantissa=%#llx sexp=%#x (1.5 = 0xc000000000000000/0x3fff); MXCSR=%#x\n",
           ok ? "matches" : "MISMATCH", (unsigned long long)m, se, *(uint32_t *)(img + 24));
}

int main(void) {
    gaps_install();
    uint32_t r[4];
    cpuid(0, 0, r); uint32_t maxl = r[0]; char vend[13]; memcpy(vend, &r[1], 4); memcpy(vend + 4, &r[3], 4); memcpy(vend + 8, &r[2], 4); vend[12] = 0;
    printf("== cpuid\n  leaf0: max=%u vendor=%s\n", maxl, vend);
    cpuid(1, 0, r); printf("  leaf1: eax=%08x ebx=%08x ecx=%08x edx=%08x (family %u model %u step %u)\n", r[0], r[1], r[2], r[3],
        ((r[0] >> 8) & 0xf) + ((r[0] >> 20) & 0xff), ((r[0] >> 4) & 0xf) | ((r[0] >> 12) & 0xf0), r[0] & 0xf);
    printf("  leaf1.ebx: logical_cpus=%u apic_id=%u clflush_line=%u\n", (r[1] >> 16) & 0xff, r[1] >> 24, ((r[1] >> 8) & 0xff) * 8);
    uint32_t ids[] = {2, 4, 5, 6, 7, 0xa, 0xb, 0xd, 0x14, 0x15, 0x16, 0x19, 0x80000000u, 0x80000001u, 0x80000005u, 0x80000006u, 0x80000007u, 0x80000008u};
    for (unsigned i = 0; i < sizeof ids / sizeof *ids; i++) {
        uint32_t id = ids[i];
        if (id < 0x80000000u && id > maxl) { cpuid(id,0,r); printf("  leaf%#x.0: (beyond max basic leaf %u) %08x %08x %08x %08x\n", id, maxl, r[0], r[1], r[2], r[3]); continue; }
        cpuid(id, 0, r); printf("  leaf%#x.0: %08x %08x %08x %08x\n", id, r[0], r[1], r[2], r[3]);
    }
    uint32_t sub[][2] = {{4,1},{4,2},{4,3},{0xb,1},{0xd,1},{0xd,2},{0xd,3},{7,1},{0x14,1}};
    for (unsigned i = 0; i < sizeof sub / sizeof *sub; i++) { cpuid(sub[i][0], sub[i][1], r); printf("  leaf%#x.%u: %08x %08x %08x %08x\n", sub[i][0], sub[i][1], r[0], r[1], r[2], r[3]); }
    char brand[49] = {0};
    for (int i = 0; i < 3; i++) { cpuid(0x80000002 + i, 0, r); memcpy(brand + 16 * i, r, 16); }
    printf("  brand: \"%s\"\n", brand);

    printf("== advertised vs executes (ADV = cpuid bit set; EXEC = ran without signal)\n");
    for (unsigned i = 0; i < sizeof feats / sizeof *feats; i++) {
        struct feat *f = &feats[i];
        uint32_t id = f->leaf, sl = 0; cpuid(id, sl, r);
        int adv = (r[f->reg] >> f->bit) & 1;
        int sig = PROBE_Q(f->name, f->fn());
        printf("  %-14s ADV=%d EXEC=%d%s%s\n", f->name, adv, sig == 0, sig ? " sig=" : "", sig ? signame(sig) : "");
        if (adv != (sig == 0)) printf("      ^^ MISMATCH\n");
    }
    cpuid(1, 0, r);
    if ((r[2] >> 27) & 1) {
        uint32_t lo, hi; asm volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        printf("  XCR0=%#x%08x\n", hi, lo);
    } else printf("  OSXSAVE=0: xgetbv not advertised\n");
    cpuid(0xd, 0, r); printf("  cpuid.d.0: supported-xcr0-low=%#x xsave-size(enabled)=%u max-size=%u\n", r[0], r[1], r[2]);
    xmm_rt();

    printf("== timing source\n");
    for (int k = 0; k < 2; k++) {
        uint64_t t0 = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW), a, b; uint32_t lo, hi;
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi)); a = ((uint64_t)hi << 32) | lo;
        while (clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) - t0 < 20000000) {}
        uint64_t t1 = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi)); b = ((uint64_t)hi << 32) | lo;
        printf("  rdtsc: %llu ticks in %llu ns -> %.3f GHz (cpuid.15: %s)\n", (unsigned long long)(b - a), (unsigned long long)(t1 - t0), (double)(b - a) / (t1 - t0), "see leaf 0x15 above");
    }
    { uint64_t t0 = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW); for (int i = 0; i < 2000; i++) cpuid(0, 0, r); uint64_t t1 = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
      printf("  cpuid avg ~%.0f ns/call (noisy: shared machine)\n", (double)(t1 - t0) / 2000);
      t0 = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW); uint32_t lo, hi; for (int i = 0; i < 20000; i++) asm volatile("rdtsc" : "=a"(lo), "=d"(hi)); t1 = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
      printf("  rdtsc avg ~%.0f ns/call (noisy)\n", (double)(t1 - t0) / 20000); }
    return 0;
}
