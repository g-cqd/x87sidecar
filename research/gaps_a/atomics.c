/* cmpxchg16b / cmpxchg8b / lock-prefixed ops incl. misaligned, line- and page-crossing operands; TSO litmus tests; fences. */
#include "gaps.h"
#include <pthread.h>
#include <sys/mman.h>
#include <stdatomic.h>

static unsigned char *page;  /* two adjacent RW 16K pages */
static void cx16_tests(void) {
    printf("== cmpxchg16b\n");
    for (int off = 0; off < 32; off += (off < 17 ? (off == 0 ? 8 : (off == 8 ? 1 : 15)) : 16)) {
        if (off == 9 || off == 24) continue;
        uint64_t *m = (uint64_t *)(page + 64 + off); m[0] = 0x1111; m[1] = 0x2222; uint64_t okf = 0, rax = 0x1111, rdx = 0x2222;
        int sig = PROBE_Q("cx16", { asm volatile("lea 1f(%%rip),%%r11\n\tmovq %%r11,%4\n\tmov $0xaaaa,%%rbx\n\tmov $0xbbbb,%%rcx\n1:\tlock cmpxchg16b (%%rdi)\n\tsetz %%r10b\n\tmovzx %%r10b,%%r10\n\tmov %%r10,%2"
            : "+a"(rax), "+d"(rdx), "=m"(okf) : "D"(m), "m"(g_expect) : "rbx", "rcx", "r10", "r11", "memory", "cc"); });
        if (sig) printf("  success path, addr%%16=%2d -> %s code=%d addr=%#llx\n", off, signame(sig), g_code, (unsigned long long)(g_addr - (uint64_t)m));
        else printf("  success path, addr%%16=%2d -> ZF=%llu mem=%llx:%llx\n", off, (unsigned long long)okf, (unsigned long long)m[1], (unsigned long long)m[0]);
    }
    { uint64_t *m = (uint64_t *)(page + 128); m[0] = 1; m[1] = 2; uint64_t rax = 7, rdx = 9, z;
      asm volatile("mov $0xaaaa,%%rbx\n\tmov $0xbbbb,%%rcx\n\tlock cmpxchg16b (%%rdi)\n\tsetz %%r10b\n\tmovzx %%r10b,%0" : "=&r"(z), "+a"(rax), "+d"(rdx) : "D"(m) : "rbx", "rcx", "r10", "memory", "cc");
      printf("  fail path: ZF=%llu rdx:rax=%llx:%llx (expect ZF=0, 2:1) mem unchanged=%d\n", (unsigned long long)z, (unsigned long long)rdx, (unsigned long long)rax, m[0] == 1 && m[1] == 2); }
    printf("== cmpxchg8b\n");
    for (int off = 0; off <= 8; off += 4) { uint32_t *m = (uint32_t *)(page + 256 + off); m[0] = 5; m[1] = 6; uint32_t ea = 5, ed = 6; uint64_t z;
      int sig = PROBE_Q("cx8", { asm volatile("mov $1,%%ebx\n\tmov $2,%%ecx\n\tlock cmpxchg8b (%%rdi)\n\tsetz %%r10b\n\tmovzx %%r10b,%0" : "=&r"(z), "+a"(ea), "+d"(ed) : "D"(m) : "rbx", "rcx", "r10", "memory", "cc"); });
      printf("  addr%%8=%d -> %s\n", off, sig ? signame(sig) : (m[0] == 1 && m[1] == 2 ? "ok (stored)" : "WRONG")); }
}
static void lock_tests(void) {
    printf("== lock-prefixed RMW at misaligned / crossing addresses (4-byte and 8-byte operands)\n");
    struct { const char *name; long off; } c[] = {{"aligned 4", 64}, {"+1 within 16B granule", 65}, {"+2 within 16B", 66}, {"+13 crosses 16B granule", 77}, {"+62 crosses 64B line", 126}, {"-3 before 16K page end (crosses page)", 16384 - 3}};
    for (unsigned k = 0; k < sizeof c / sizeof *c; k++) {
        for (int w = 4; w <= 8; w += 4) {
            unsigned char *a = page + c[k].off; memset(a, 0, 8);
            int sig;
            if (w == 4) sig = PROBE_Q("lockadd4", { asm volatile("lock addl $5,(%0)" :: "r"(a) : "memory", "cc"); asm volatile("lock xaddl %%eax,(%0)" :: "r"(a), "a"(1) : "memory", "cc"); });
            else sig = PROBE_Q("lockadd8", { asm volatile("lock addq $5,(%0)" :: "r"(a) : "memory", "cc"); asm volatile("lock xaddq %%rax,(%0)" :: "r"(a), "a"(1) : "memory", "cc"); });
            uint64_t v = 0; memcpy(&v, a, w);
            int sig2 = PROBE_Q("xchg", { uint64_t t = 7; if (w == 4) asm volatile("xchgl %%eax,(%0)" :: "r"(a), "a"(t) : "memory"); else asm volatile("xchgq %%rax,(%0)" :: "r"(a), "a"(t) : "memory"); });
            int sig3 = PROBE_Q("lock cmpxchg", { uint64_t t = 7; if (w == 4) asm volatile("lock cmpxchgl %%ecx,(%0)" :: "r"(a), "a"(t), "c"(9) : "memory", "cc"); else asm volatile("lock cmpxchgq %%rcx,(%0)" :: "r"(a), "a"(t), "c"(9) : "memory", "cc"); });
            int sig4 = PROBE_Q("lock bts", { asm volatile("lock btsl $1,(%0)" :: "r"(a) : "memory", "cc"); });
            printf("  %-40s w=%d  lock add/xadd: %s (val=%llu, want 6)  xchg: %s  lock cmpxchg: %s  lock bts: %s\n", c[k].name, w, sig ? signame(sig) : "ok", (unsigned long long)v, sig2 ? signame(sig2) : "ok", sig3 ? signame(sig3) : "ok", sig4 ? signame(sig4) : "ok");
        }
    }
    int sig = PROBE_Q("fences", { asm volatile("mfence\n\tsfence\n\tlfence\n\tpause" ::: "memory"); }); printf("  mfence/sfence/lfence/pause: %s\n", sig ? signame(sig) : "execute");
    sig = PROBE_Q("movnti", { asm volatile("movnti %%rax,(%0)\n\tmovntdq %%xmm0,(%1)\n\tsfence" :: "r"(page + 512), "r"(page + 528) : "memory"); }); printf("  movnti/movntdq+sfence: %s\n", sig ? signame(sig) : "execute");
}

/* ---------------- split-lock atomicity ---------------- */
static volatile unsigned char *split_p; static int split_w;
static void *splitter(void *a) { volatile unsigned char *p = split_p; for (int i = 0; i < 300000; i++) { if (split_w == 4) asm volatile("lock addl $1,(%0)" :: "r"(p) : "memory", "cc"); else asm volatile("lock addq $1,(%0)" :: "r"(p) : "memory", "cc"); } return 0; }
static void split_tests(void) {
    printf("== atomicity of split (misaligned) lock add: 2 threads x 300000 increments, expected total 600000\n");
    struct { const char *n; long off; int w; } c[] = {{"aligned 4", 64, 4}, {"4B at +13 (crosses 16B granule)", 77, 4}, {"8B at +9 (crosses 16B granule)", 73, 8}, {"4B at +62 (crosses 64B line)", 126, 4}, {"8B at +60 (crosses 64B line)", 124, 8}, {"4B 2 bytes before 16K page end", 16384 - 2, 4}};
    for (unsigned k = 0; k < sizeof c / sizeof *c; k++) { split_p = page + c[k].off; split_w = c[k].w; memset((void *)split_p, 0, 8);
        pthread_t t[2]; pthread_create(&t[0], 0, splitter, 0); pthread_create(&t[1], 0, splitter, 0); pthread_join(t[0], 0); pthread_join(t[1], 0);
        uint64_t v = 0; memcpy(&v, (void *)split_p, c[k].w); printf("  %-34s total=%llu %s\n", c[k].n, (unsigned long long)v, v == 600000 ? "(exact)" : "LOST UPDATES: not atomic"); }
}
/* ---------------- litmus ---------------- */
#define N 400000
static struct { volatile int x; char p0[60]; volatile int y; char p1[60]; volatile int r0, r1; char p2[56]; volatile int go[4]; char p3[48]; } L __attribute__((aligned(128)));
static volatile int *R0, *R1; static atomic_int sync_ctr; static int mode;
static atomic_int arrive, release_;
static void barrier(int nthreads, int it) { int a = atomic_fetch_add(&arrive, 1) + 1; if (a == nthreads) { atomic_store(&arrive, 0); atomic_store(&release_, it + 1); } else while (atomic_load(&release_) <= it) asm volatile("pause"); }
static long cnt_fwd, cnt_weird, cnt_both0;
static volatile int rr[N][4];
static void *thr(void *arg) {
    int id = (int)(long)arg;
    for (int it = 0; it < N; it++) {
        if (id == 0) { L.x = 0; L.y = 0; }
        barrier(2, it * 2);
        if (mode == 0) {  /* MP */
            if (id == 0) { asm volatile("movl $1,%0\n\tmovl $1,%1" : "=m"(L.x), "=m"(L.y)); }
            else { int a, b; asm volatile("movl %2,%0\n\tmovl %3,%1" : "=&r"(a), "=r"(b) : "m"(L.y), "m"(L.x)); rr[it][0] = a; rr[it][1] = b; }
        } else if (mode == 1) { /* SB */
            int a; if (id == 0) { asm volatile("movl $1,%1\n\tmovl %2,%0" : "=r"(a) : "m"(L.x), "m"(L.y)); } else { asm volatile("movl $1,%1\n\tmovl %2,%0" : "=r"(a) : "m"(L.y), "m"(L.x)); }
            rr[it][id] = a;
        } else if (mode == 2) { /* LB */
            int a; if (id == 0) { asm volatile("movl %2,%0\n\tmovl $1,%1" : "=r"(a), "=m"(L.y) : "m"(L.x)); } else { asm volatile("movl %2,%0\n\tmovl $1,%1" : "=r"(a), "=m"(L.x) : "m"(L.y)); }
            rr[it][id] = a;
        } else { /* SB with mfence: 0/0 must disappear */
            int a; if (id == 0) { asm volatile("movl $1,%1\n\tmfence\n\tmovl %2,%0" : "=r"(a) : "m"(L.x), "m"(L.y) : "memory"); } else { asm volatile("movl $1,%1\n\tmfence\n\tmovl %2,%0" : "=r"(a) : "m"(L.y), "m"(L.x) : "memory"); }
            rr[it][id] = a;
        }
        barrier(2, it * 2 + 1);
    }
    return 0;
}
static void run_litmus(int m, const char *name, const char *rule) {
    mode = m; atomic_store(&arrive, 0); atomic_store(&release_, 0); memset((void *)rr, 0, sizeof rr);
    pthread_t t[2]; pthread_create(&t[0], 0, thr, (void *)0); pthread_create(&t[1], 0, thr, (void *)1); pthread_join(t[0], 0); pthread_join(t[1], 0);
    long bad = 0, both0 = 0, both1 = 0;
    for (int i = 0; i < N; i++) { if (m == 0) { if (rr[i][0] == 1 && rr[i][1] == 0) bad++; } else if (m == 1 || m == 3) { if (rr[i][0] == 0 && rr[i][1] == 0) both0++; } else { if (rr[i][0] == 1 && rr[i][1] == 1) both1++; } }
    printf("  %-30s iterations=%d  weak outcome count: %ld   (%s)\n", name, N, m == 0 ? bad : (m == 2 ? both1 : both0), rule);
}
/* IRIW-style with 4 threads */
static atomic_int arr4, rel4; static volatile int ix, iy, ir[N][4];
static void b4(int it) { int a = atomic_fetch_add(&arr4, 1) + 1; if (a == 4) { atomic_store(&arr4, 0); atomic_store(&rel4, it + 1); } else while (atomic_load(&rel4) <= it) asm volatile("pause"); }
static void *iriw(void *arg) { int id = (int)(long)arg;
    for (int it = 0; it < N / 2; it++) { if (id == 0) { ix = 0; iy = 0; } b4(it * 2);
        if (id == 0) asm volatile("movl $1,%0" : "=m"(ix)); else if (id == 1) asm volatile("movl $1,%0" : "=m"(iy));
        else if (id == 2) { int a, b; asm volatile("movl %2,%0\n\tmovl %3,%1" : "=&r"(a), "=r"(b) : "m"(ix), "m"(iy)); ir[it][0] = a; ir[it][1] = b; }
        else { int a, b; asm volatile("movl %2,%0\n\tmovl %3,%1" : "=&r"(a), "=r"(b) : "m"(iy), "m"(ix)); ir[it][2] = a; ir[it][3] = b; }
        b4(it * 2 + 1); }
    return 0; }
int main(void) {
    gaps_install(); setvbuf(stdout, NULL, _IOLBF, 0);
    page = mmap(NULL, 32768, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0); mprotect(page + 16384, 4096, PROT_READ | PROT_WRITE);
    cx16_tests(); lock_tests(); split_tests();
    printf("== TSO litmus (two threads, plain movs; shared machine, so counts of the *allowed* outcome are noisy)\n");
    run_litmus(0, "MP: x=1;y=1 | r1=y;r2=x", "r1=1,r2=0 forbidden under TSO");
    run_litmus(2, "LB: r1=x;y=1 | r2=y;x=1", "r1=r2=1 forbidden under TSO");
    run_litmus(1, "SB: x=1;r1=y | y=1;r2=x", "r1=r2=0 ALLOWED on x86 (store buffer)");
    run_litmus(3, "SB + mfence", "r1=r2=0 forbidden with mfence");
    { pthread_t t[4]; for (long i = 0; i < 4; i++) pthread_create(&t[i], 0, iriw, (void *)i); for (int i = 0; i < 4; i++) pthread_join(t[i], 0);
      long bad = 0; for (int i = 0; i < N / 2; i++) if (ir[i][0] == 1 && ir[i][1] == 0 && ir[i][2] == 1 && ir[i][3] == 0) bad++;
      printf("  IRIW: weak outcome count %ld of %d (forbidden under TSO; needs >=4 free cores to be meaningful)\n", bad, N / 2); }
    return 0;
}
