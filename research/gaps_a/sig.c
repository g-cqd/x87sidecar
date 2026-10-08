/* Signal / exception fidelity under Rosetta: fault addresses, register exactness, FP traps, TF, async delivery, context edits. */
#include "gaps.h"
#include <pthread.h>
#include <sys/mman.h>
#include <time.h>
#include <fenv.h>

struct regs { uint64_t r[16]; uint64_t rip, rflags; };
static struct regs g_regs;
static volatile int g_sigs, g_ccode; static volatile uint64_t g_saddr; static volatile size_t g_mcsize;
static volatile uint32_t g_mxcsr; static unsigned char g_xmm_in_ctx[16 * 16]; static volatile int g_hstack_in_alt;
static unsigned char altstack[64 * 1024];

static void grab(ucontext_t *u) {
    _STRUCT_X86_THREAD_STATE64 *s = &u->uc_mcontext->__ss;
    g_regs.r[0] = s->__rax; g_regs.r[1] = s->__rcx; g_regs.r[2] = s->__rdx; g_regs.r[3] = s->__rbx; g_regs.r[4] = s->__rsp; g_regs.r[5] = s->__rbp;
    g_regs.r[6] = s->__rsi; g_regs.r[7] = s->__rdi; g_regs.r[8] = s->__r8; g_regs.r[9] = s->__r9; g_regs.r[10] = s->__r10; g_regs.r[11] = s->__r11;
    g_regs.r[12] = s->__r12; g_regs.r[13] = s->__r13; g_regs.r[14] = s->__r14; g_regs.r[15] = s->__r15; g_regs.rip = s->__rip; g_regs.rflags = s->__rflags;
    g_mcsize = u->uc_mcsize; g_mxcsr = u->uc_mcontext->__fs.__fpu_mxcsr;
    memcpy(g_xmm_in_ctx, &u->uc_mcontext->__fs.__fpu_xmm0, sizeof g_xmm_in_ctx);
}
static sigjmp_buf jb2; static volatile int jarm;
static void h(int s, siginfo_t *si, void *uv) {
    ucontext_t *u = uv; grab(u); g_sigs = s; g_ccode = si->si_code; g_saddr = (uint64_t)si->si_addr;
    char probe; g_hstack_in_alt = ((unsigned char *)&probe >= altstack && (unsigned char *)&probe < altstack + sizeof altstack);
    if (jarm) siglongjmp(jb2, 1); _exit(98);
}
static void inst(int s, int flags) { struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = h; sa.sa_flags = SA_SIGINFO | flags; sigemptyset(&sa.sa_mask); sigaction(s, &sa, NULL); }
#define RUN(body) ({ g_sigs = 0; jarm = 1; if (!sigsetjmp(jb2, 1)) { body; } jarm = 0; g_sigs; })
static const char *cn(int s, int c) { static char b[64]; if (!s) return "NO SIGNAL"; snprintf(b, sizeof b, "sig=%s(%d) code=%d", signame(s), s, c); return b; }

/* Loads a known pattern into every GPR, then faults on a load from r8=0x10; saves expected rsp/flags beside the pattern. */
extern void fault_regs(struct regs *pat);
__asm__(".text\n.globl _fault_regs\n_fault_regs:\n"
 "push %rbp\npush %rbx\npush %r12\npush %r13\npush %r14\npush %r15\n"
 "mov $0x80,%eax\nadd $0x80,%al\n"
 "pushfq\npopq 0x88(%rdi)\n"
 "mov %rsp,0x20(%rdi)\n"
 "mov 0x00(%rdi),%rax\nmov 0x08(%rdi),%rcx\nmov 0x10(%rdi),%rdx\nmov 0x18(%rdi),%rbx\nmov 0x28(%rdi),%rbp\nmov 0x30(%rdi),%rsi\n"
 "mov 0x48(%rdi),%r9\nmov 0x50(%rdi),%r10\nmov 0x58(%rdi),%r11\nmov 0x60(%rdi),%r12\nmov 0x68(%rdi),%r13\nmov 0x70(%rdi),%r14\nmov 0x78(%rdi),%r15\n"
 "mov 0x40(%rdi),%r8\nmov 0x38(%rdi),%rdi\n"
 "movq (%r8),%r9\n"   /* faulting load: r8 = 0x10 */
 "pop %r15\npop %r14\npop %r13\npop %r12\npop %rbx\npop %rbp\nret\n");

static void test_regs(void) {
    printf("== register / flag exactness at a SIGSEGV (faulting `mov (%%r8),%%r9`, r8=0x10)\n");
    struct regs pat; for (int i = 0; i < 16; i++) pat.r[i] = 0x1111111111111111ull * (i + 1) ^ 0x00ff00ff00ff00ffull * i;
    pat.r[8] = 0x10; pat.r[4] = 0;
    RUN(fault_regs(&pat));
    printf("  %s si_addr=%#llx uc_mcsize=%zu\n", cn(g_sigs, g_ccode), (unsigned long long)g_saddr, (size_t)g_mcsize);
    const char *nm[16] = {"rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15"};
    int bad = 0;
    for (int i = 0; i < 16; i++) { if (i == 4) { if (g_regs.r[4] != pat.r[4]) { printf("  rsp differs: got %#llx exp %#llx\n", (unsigned long long)g_regs.r[4], (unsigned long long)pat.r[4]); bad++; } continue; }
        if (g_regs.r[i] != pat.r[i]) { printf("  %s differs: got %#llx exp %#llx\n", nm[i], (unsigned long long)g_regs.r[i], (unsigned long long)pat.r[i]); bad++; } }
    uint64_t m = 0x8d5; printf("  rflags arith bits got=%#llx exp=%#llx  (full got=%#llx)\n", (unsigned long long)(g_regs.rflags & m), (unsigned long long)(pat.rflags & m), (unsigned long long)g_regs.rflags);
    printf("  rip: %#llx (fault_regs at %p); GPR mismatches=%d\n", (unsigned long long)g_regs.rip, (void *)fault_regs, bad);
}

static void test_faults(void) {
    printf("== fault kinds\n");
    long pg = 16384; unsigned char *p = mmap(NULL, 4 * pg, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    mprotect(p + pg, pg, PROT_READ); mprotect(p + 2 * pg, pg, PROT_NONE); mprotect(p + 3 * pg, pg, PROT_READ | PROT_WRITE);
    volatile unsigned char *ro = p + pg, *none = p + 2 * pg;
    int s;
    s = RUN(({ volatile unsigned char *q = (void *)0x10; (void)*q; })); printf("  read 0x10: %s addr=%#llx\n", cn(g_sigs, g_ccode), (unsigned long long)g_saddr);
    s = RUN(({ *ro = 1; })); printf("  write to PROT_READ: %s addr=%#llx (want %p)\n", cn(g_sigs, g_ccode), (unsigned long long)g_saddr, (void *)ro);
    s = RUN(({ (void)*none; })); printf("  read PROT_NONE: %s addr=%#llx (want %p)\n", cn(g_sigs, g_ccode), (unsigned long long)g_saddr, (void *)none);
    s = RUN(({ void (*f)(void) = (void *)ro; f(); })); printf("  exec non-exec page: %s addr=%#llx rip=%#llx (want %p)\n", cn(g_sigs, g_ccode), (unsigned long long)g_saddr, (unsigned long long)g_regs.rip, (void *)ro);
    s = RUN(({ asm volatile("movabs $0x8000000000000000,%%rax\n\tmov (%%rax),%%rbx" ::: "rax", "rbx"); })); printf("  non-canonical load: %s addr=%#llx (HW: #GP => si_addr 0)\n", cn(g_sigs, g_ccode), (unsigned long long)g_saddr);
    s = RUN(({ asm volatile("lea 1(%0),%%rax\n\tmovaps (%%rax),%%xmm0" :: "r"(p) : "rax", "xmm0"); })); printf("  movaps misaligned: %s addr=%#llx (HW: #GP => SIGSEGV)\n", cn(g_sigs, g_ccode), (unsigned long long)(g_sigs ? g_saddr : 0));
    s = RUN(({ asm volatile("lea 1(%0),%%rax\n\tmovdqa %%xmm0,(%%rax)" :: "r"(p) : "rax", "memory"); })); printf("  movdqa store misaligned: %s\n", cn(g_sigs, g_ccode));
    s = RUN(({ asm volatile("lea 1(%0),%%rax\n\tvmovaps (%%rax),%%ymm0" :: "r"(p) : "rax", "xmm0"); })); printf("  vmovaps ymm misaligned: %s\n", cn(g_sigs, g_ccode));
    s = RUN(({ asm volatile("lea 3(%0),%%rax\n\tmovl (%%rax),%%ebx" :: "r"(p) : "rax", "rbx"); })); printf("  unaligned scalar load: %s (expected: none)\n", g_sigs ? cn(g_sigs, g_ccode) : "no signal");
    /* rep movsb across into PROT_NONE page: partial progress must be visible */
    { unsigned char *src = p + 3 * pg; memset(src, 0x5a, 64); unsigned char *dst = none - 16;  /* 16 bytes valid-ish? dst page below is RO, so dst fault earlier: use RW */
      mprotect(p + pg, pg, PROT_READ | PROT_WRITE); dst = p + 2 * pg - 16;
      s = RUN(({ asm volatile("rep movsb" : "+S"(src), "+D"(dst) : "c"(64) : "memory"); })); (void)s;
      printf("  rep movsb 64B crossing into PROT_NONE after 16B: %s fault addr=%#llx rcx=%llu rsi-src0=%lld rdi-dst0=%lld (HW: rcx=48, rdi at fault)\n", cn(g_sigs, g_ccode), (unsigned long long)g_saddr,
             (unsigned long long)g_regs.r[1], (long long)(g_regs.r[6] - (uint64_t)(p + 3 * pg)), (long long)(g_regs.r[7] - (uint64_t)(p + 2 * pg - 16))); }
    /* store split across a page boundary where the second page faults: is the first half written? (HW: no) */
    { mprotect(p + pg, pg, PROT_READ | PROT_WRITE); unsigned char *a = p + 2 * pg - 4; memset(p + 2 * pg - 8, 0, 8);
      s = RUN(({ asm volatile("movabs $0x1122334455667788,%%rax\n\tmov %%rax,(%0)" :: "r"(a) : "rax", "memory"); }));
      printf("  8B store straddling RW|NONE boundary: %s addr=%#llx first-half-written=%s (HW: none written, si_addr = start or faulting page)\n", cn(g_sigs, g_ccode), (unsigned long long)g_saddr, p[2 * pg - 4] ? "YES" : "no"); }
}

static void test_fpe(void) {
    printf("== FP / integer exceptions\n");
    int s;
    s = RUN(({ asm volatile("xor %%edx,%%edx\n\tmov $1,%%eax\n\txor %%ecx,%%ecx\n\tdiv %%ecx" ::: "rax", "rcx", "rdx"); })); printf("  div32 by 0: %s (HW: SIGFPE FPE_INTDIV=7)\n", cn(g_sigs, g_ccode));
    s = RUN(({ asm volatile("xor %%edx,%%edx\n\tmov $1,%%rax\n\txor %%ecx,%%ecx\n\tdiv %%rcx" ::: "rax", "rcx", "rdx"); })); printf("  div64 by 0: %s\n", cn(g_sigs, g_ccode));
    s = RUN(({ asm volatile("mov $0x80000000,%%eax\n\tcltd\n\tmov $-1,%%ecx\n\tidiv %%ecx" ::: "rax", "rcx", "rdx"); })); printf("  idiv INT_MIN/-1: %s (HW: SIGFPE)\n", cn(g_sigs, g_ccode));
    s = RUN(({ asm volatile("mov $0x80000000,%%eax\n\txor %%edx,%%edx\n\tmov $1,%%ecx\n\tdiv %%ecx" ::: "rax", "rcx", "rdx"); })); printf("  div quotient fits? (edx:eax=0x80000000/1) %s\n", g_sigs ? cn(g_sigs, g_ccode) : "ok no signal");
    s = RUN(({ asm volatile("mov $2,%%edx\n\txor %%eax,%%eax\n\tmov $1,%%ecx\n\tdiv %%ecx" ::: "rax", "rcx", "rdx"); })); printf("  div quotient overflow (edx>=divisor): %s (HW: SIGFPE)\n", cn(g_sigs, g_ccode));
    { uint32_t mx = 0x1f80 & ~(1u << 9), old; asm volatile("stmxcsr %0" : "=m"(old)); uint32_t chk;
      s = RUN(({ asm volatile("ldmxcsr %0\n\tstmxcsr %1" :: "m"(mx), "m"(chk)); }));
      printf("  ldmxcsr unmask ZM: stored back %#x (wanted %#x)\n", chk, mx);
      volatile float one = 1.0f, zero = 0.0f, r; s = RUN(({ r = one / zero; asm volatile("" :: "x"(r)); })); printf("  divss 1/0 with ZM unmasked: %s (HW: SIGFPE FPE_FLTDIV=%d)\n", g_sigs ? cn(g_sigs, g_ccode) : "NO SIGNAL (result silently inf)", 3);
      asm volatile("ldmxcsr %0" :: "m"(old)); }
    { uint16_t cw = 0x037f & ~4, old; asm volatile("fnstcw %0" : "=m"(old)); volatile double one = 1.0, zero = 0.0; uint16_t sw;
      asm volatile("fnclex");
      s = RUN(({ asm volatile("fldcw %0\n\tfldl %1\n\tfdivl %2\n\tfwait\n\tfstp %%st(0)\n\tfwait" :: "m"(cw), "m"(one), "m"(zero) : "memory"); }));
      asm volatile("fnstsw %0" : "=m"(sw)); asm volatile("fnclex\n\tfldcw %0" :: "m"(old));
      printf("  x87 fdiv 1/0 with ZE unmasked: %s sw=%#x (HW: SIGFPE FPE_FLTDIV at fwait)\n", g_sigs ? cn(g_sigs, g_ccode) : "NO SIGNAL", sw); }
    { uint16_t cw = 0x037f & ~1, old; asm volatile("fnstcw %0" : "=m"(old)); volatile double v = 0.0; /* invalid: 0/0 */
      s = RUN(({ asm volatile("fldcw %0\n\tfldz\n\tfldz\n\tfdivrp\n\tfwait\n\tfstp %%st(0)\n\tfwait" :: "m"(cw) : "memory"); })); asm volatile("fnclex\n\tfldcw %0" :: "m"(old)); (void)v;
      printf("  x87 0/0 with IM unmasked: %s\n", g_sigs ? cn(g_sigs, g_ccode) : "NO SIGNAL"); }
}

static volatile int steps; static void tfh(int s, siginfo_t *si, void *uv) { ucontext_t *u = uv; steps++; g_ccode = si->si_code; g_regs.rip = u->uc_mcontext->__ss.__rip; if (steps >= 3) u->uc_mcontext->__ss.__rflags &= ~0x100; }
static void test_tf(void) {
    printf("== TF single-step\n");
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = tfh; sa.sa_flags = SA_SIGINFO; sigaction(SIGTRAP, &sa, NULL);
    steps = 0; asm volatile("pushfq\n\torq $0x100,(%%rsp)\n\tpopfq\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop" ::: "cc", "memory");
    printf("  SIGTRAPs delivered with TF set across 6 nops: %d (HW: >= 3 until handler clears TF); last si_code=%d (TRAP_TRACE=2)\n", steps, g_ccode);
    inst(SIGTRAP, 0);
}

static volatile int stop_flag, async_n, async_bad, async_after_first; static volatile uint64_t lbl_mid;
extern char async_mid[]; extern char async_loop[];
__asm__(".text\n.globl _async_loop\n.globl _async_mid\n_async_loop:\n 1:\n inc %rbx\n_async_mid:\n inc %rcx\n cmpb $0,_stop_flag(%rip)\n je 1b\n ret\n");
static void usr1(int s, siginfo_t *si, void *uv) { ucontext_t *u = uv; _STRUCT_X86_THREAD_STATE64 *t = &u->uc_mcontext->__ss; async_n++;
    int64_t d = (int64_t)(t->__rbx - t->__rcx); int mid = (t->__rip == (uint64_t)async_mid);
    if (!((d == 1 && mid) || (d == 0 && !mid))) async_bad++; if (t->__rip < (uint64_t)async_loop || t->__rip > (uint64_t)async_loop + 32) async_bad += 1000; }
static void *killer(void *a) { pthread_t t = *(pthread_t *)a; for (int i = 0; i < 1500; i++) { pthread_kill(t, SIGUSR1); usleep(100); } stop_flag = 1; return 0; }
extern void async_loop_fn(void);
static void test_async(void) {
    printf("== asynchronous signal inside a tight translated loop (inc rbx; inc rcx; cmp; je): precise state?\n");
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = usr1; sa.sa_flags = SA_SIGINFO; sigaction(SIGUSR1, &sa, NULL);
    pthread_t me = pthread_self(), k; pthread_create(&k, NULL, killer, &me);
    uint64_t b = 0, c = 0; asm volatile("xor %%ebx,%%ebx\n\txor %%ecx,%%ecx\n\tcall _async_loop" ::: "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "cc", "memory"); (void)b; (void)c;
    pthread_join(k, NULL);
    printf("  signals delivered=%d, inconsistent (rbx-rcx vs rip) = %d (HW: 0; >=1000 means rip outside loop)\n", async_n, async_bad);
}

static volatile unsigned char *gp; static void mod_h(int s, siginfo_t *si, void *uv) { ucontext_t *u = uv; _STRUCT_X86_THREAD_STATE64 *t = &u->uc_mcontext->__ss; t->__rax = 0x1234; t->__rip += 3; t->__rflags |= 1;
    uint32_t pat = 0xdeadbeef; for (int i = 0; i < 4; i++) memcpy((char *)&u->uc_mcontext->__fs.__fpu_xmm3 + 4 * i, &pat, 4); }
static void test_modctx(void) {
    printf("== edit context in handler and sigreturn\n");
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = mod_h; sa.sa_flags = SA_SIGINFO; sigaction(SIGSEGV, &sa, NULL);
    uint64_t rax = 0; uint32_t x[4] = {0}; uint32_t in[4] = {1, 2, 3, 4}; int cf = 0;
    asm volatile("movdqu (%2),%%xmm3\n\txor %%eax,%%eax\n\txor %%r8d,%%r8d\n\tclc\n\tmovq (%%r8),%%r9\n\tsetc %%cl\n\tmovzx %%cl,%%ecx\n\tmov %%ecx,%1\n\tmovdqu %%xmm3,(%3)\n\tmov %%rax,%0" : "=m"(rax), "=m"(cf) : "r"(in), "r"(x) : "rax", "rcx", "r8", "r9", "xmm3", "memory", "cc");
    printf("  after handler set rax=0x1234, rip+=3 (skip faulting 3-byte mov), CF=1, xmm3=0xdeadbeef*4: rax=%#llx CF=%d xmm3=%08x%08x%08x%08x\n", (unsigned long long)rax, cf, x[3], x[2], x[1], x[0]);
    inst(SIGSEGV, SA_ONSTACK);
}

static volatile int nest_depth, nest_max; static void n1(int s, siginfo_t *si, void *u) { nest_depth++; if (nest_depth > nest_max) nest_max = nest_depth; if (nest_depth < 4) raise(SIGUSR2); nest_depth--; }
static void test_nest_alt(void) {
    printf("== nested signals / sigaltstack\n");
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = n1; sa.sa_flags = SA_SIGINFO | SA_NODEFER; sigaction(SIGUSR2, &sa, NULL);
    raise(SIGUSR2); printf("  nested SIGUSR2 depth reached %d (expect 4)\n", nest_max);
    stack_t ss = {.ss_sp = altstack, .ss_size = sizeof altstack, .ss_flags = 0}; sigaltstack(&ss, NULL); inst(SIGSEGV, SA_ONSTACK);
    RUN(({ volatile int *q = (void *)8; *q = 1; })); printf("  SIGSEGV with SA_ONSTACK: handler ran on altstack=%s\n", g_hstack_in_alt ? "yes" : "NO");
    uint64_t sp0; asm volatile("mov %%rsp,%0" : "=r"(sp0));
    printf("  fault-time rsp %s guest stack (0x%llx vs now 0x%llx)\n", g_regs.r[4] ? "reported on" : "?", (unsigned long long)g_regs.r[4], (unsigned long long)sp0);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    stack_t ss = {.ss_sp = altstack, .ss_size = sizeof altstack}; sigaltstack(&ss, NULL);
    inst(SIGSEGV, SA_ONSTACK); inst(SIGBUS, SA_ONSTACK); inst(SIGFPE, SA_ONSTACK); inst(SIGILL, SA_ONSTACK); inst(SIGTRAP, 0);
    test_regs(); test_faults(); test_fpe(); test_tf(); test_async(); test_modctx(); test_nest_alt();
    return 0;
}
