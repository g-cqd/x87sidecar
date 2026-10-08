/* 32-bit compatibility-mode / LDT / segment probes under Rosetta (technique from tests/test_decoder_arpl.c).
 * usage: ./compat           -> LDT API survey + every stub in a child process
 *        ./compat N         -> run stub N only (some stubs end in a non-catchable runtime abort; isolate them)
 * build: clang -arch x86_64 -O1 -Wl,-pagezero_size,0x4000 -o compat compat.c */
#include "gaps.h"
#include <architecture/i386/desc.h>
#include <architecture/i386/table.h>
#include <i386/user_ldt.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <spawn.h>
extern char **environ;

static uint16_t sel_cs32, sel_ds32, sel_fs, sel_cs16, sel_cs64;
uint32_t g_far[2]; static struct { uint32_t farsub[2]; uint32_t far2[2]; uint32_t selfs; uint32_t pad; uint32_t tebptr; } blk;
static volatile uint32_t teb_marker __attribute__((aligned(16))) = 0xC0FFEE11; static unsigned char tebpad[0x400];
uint64_t g_rsp64; uint16_t g_ds, g_es, g_ss;
static uint32_t g_out[8];

static uint16_t ldt_sel(int i) { return (uint16_t)((i << 3) | 4 | 3); }
static int set_seg(int idx, int code, uint32_t base, uint32_t limit, int gran_page, int d32) {
    ldt_entry_t e; memset(&e, 0, sizeof e);
    if (code) { e.code.limit00 = limit & 0xffff; e.code.limit16 = (limit >> 16) & 0xf; e.code.base00 = base & 0xffff; e.code.base16 = (base >> 16) & 0xff; e.code.base24 = base >> 24;
        e.code.type = DESC_CODE_READ; e.code.dpl = 3; e.code.present = 1; e.code.opsz = d32 ? DESC_CODE_32B : DESC_CODE_16B; e.code.granular = gran_page ? DESC_GRAN_PAGE : DESC_GRAN_BYTE; }
    else { e.data.limit00 = limit & 0xffff; e.data.limit16 = (limit >> 16) & 0xf; e.data.base00 = base & 0xffff; e.data.base16 = (base >> 16) & 0xff; e.data.base24 = base >> 24;
        e.data.type = DESC_DATA_WRITE; e.data.dpl = 3; e.data.present = 1; e.data.stksz = d32 ? DESC_DATA_32B : DESC_DATA_16B; e.data.granular = gran_page ? DESC_GRAN_PAGE : DESC_GRAN_BYTE; }
    int r = i386_set_ldt(idx, &e, 1); return r < 0 ? -errno : r;
}

extern uint32_t run32(uint32_t eip, uint32_t cs32, uint32_t ds32, uint32_t cs64, uint64_t stacktop, uint32_t blk);
extern char recover64[];
__asm__(".text\n.p2align 4\n.globl _run32\n_run32:\n"
 "push %rbp\npush %rbx\npush %r12\npush %r13\npush %r14\npush %r15\n"
 "mov %rsp,_g_rsp64(%rip)\n"
 "mov %ds,%ax\nmov %ax,_g_ds(%rip)\nmov %es,%ax\nmov %ax,_g_es(%rip)\nmov %ss,%ax\nmov %ax,_g_ss(%rip)\n"
 "mov %edi,_g_far(%rip)\nmov %si,_g_far+4(%rip)\n"
 "lea 1f(%rip),%rbx\nmov %ecx,%r12d\n"
 "mov %r8,%rsp\n"
 "mov %edx,%eax\nmov %ax,%ds\nmov %ax,%es\nmov %ax,%ss\n"
 "mov %r12d,%edi\nmov %ebx,%esi\nmov %r9d,%ebp\nxor %eax,%eax\n"
 "ljmpl *_g_far(%rip)\n"
 "1:\n"
 ".globl _recover64\n_recover64:\n"
 "mov _g_ds(%rip),%cx\nmov %cx,%ds\nmov _g_es(%rip),%cx\nmov %cx,%es\nmov _g_ss(%rip),%cx\nmov %cx,%ss\n"
 "mov _g_rsp64(%rip),%rsp\n"
 "pop %r15\npop %r14\npop %r13\npop %r12\npop %rbx\npop %rbp\nret\n");

/* 32-bit stubs; each ends by returning to 64-bit through `push edi; push esi; lret` (edi=64-bit cs, esi=return offset). */
#define EPI "push %edi\n push %esi\n lret\n"
__asm__(".text\n.p2align 4\n.code32\n"
 ".globl _s_base\n_s_base:\n"
 "_s0: mov $0x1234,%eax\n" EPI
 ".p2align 4\n_s1: xor %eax,%eax\n cpuid\n mov %ebx,%eax\n" EPI
 ".p2align 4\n_s2: mov 16(%ebp),%ax\n mov %ax,%fs\n mov 24(%ebp),%ecx\n mov %fs:(%ecx),%eax\n xor %ecx,%ecx\n mov %cx,%fs\n" EPI
 ".p2align 4\n_s3: mov 16(%ebp),%ax\n mov %ax,%es\n mov 24(%ebp),%ecx\n mov %es:(%ecx),%eax\n" EPI
 ".p2align 4\n_s4: mov 16(%ebp),%ax\n mov %ax,%gs\n mov 24(%ebp),%ecx\n mov %gs:(%ecx),%eax\n xor %ecx,%ecx\n mov %cx,%gs\n" EPI
 ".p2align 4\n_s5: push %ds\n pop %es\n push %cs\n pop %eax\n" EPI
 ".p2align 4\n_s6: lcall *0(%ebp)\n" EPI
 ".p2align 4\n_s6sub: mov $0x55,%eax\n lret\n"
 ".p2align 4\n_s7: ljmpl *8(%ebp)\n"
 ".p2align 4\n_s7t: mov $0x66,%eax\n" EPI
 ".p2align 4\n_s9: mov $20,%eax\n int $0x80\n" EPI
 ".p2align 4\n_s11: mov $0x79,%al\n add $0x35,%al\n daa\n movzbl %al,%eax\n setc %cl\n movzbl %cl,%ecx\n shl $8,%ecx\n or %ecx,%eax\n" EPI
 ".p2align 4\n_s12: mov $1,%eax\n mov $2,%ecx\n mov $3,%ebx\n mov $4,%edx\n pusha\n xor %eax,%eax\n xor %ecx,%ecx\n popa\n lea (%eax,%ecx,8),%eax\n" EPI
 ".p2align 4\n_s14: les 8(%ebp),%eax\n mov %es,%dx\n" EPI
 ".p2align 4\n_s15: mov $0x1234,%ax\n mov %ax,%ds\n mov $0x77,%eax\n" EPI
 ".p2align 4\n_s16: in $0x60,%al\n mov $0x78,%eax\n" EPI
 ".p2align 4\n_s17: mov $0x7f,%ah\n mov $0xff,%al\n sahf\n lahf\n movzbl %ah,%eax\n" EPI
 ".p2align 4\n_s18: mov $5,%eax\n mov $-1, %ecx\n mov (%ecx), %ebx\n mov $0x79,%eax\n" EPI
 ".p2align 4\n_s19: mov $0x80000000,%eax\n cltd\n mov $-1,%ecx\n idiv %ecx\n mov $0x7a,%eax\n" EPI
 ".p2align 4\n_s20: sysenter\n" EPI
 ".p2align 4\n.globl _s_end\n_s_end: nop\n"
 ".code64\n");
extern char s_base[], s0[], s1[], s2[], s3[], s4[], s5[], s6[], s6sub[], s7[], s7t[], s9[], s11[], s12[], s14[], s15[], s16[], s17[], s18[], s19[], s20[];

/* 16-bit code stub in its own array so its address can be arranged inside one 64K window */
__attribute__((aligned(16))) static unsigned char code16[32] = { 0xb8, 0x34, 0x12, /* mov ax,0x1234 */ 0x66, 0x57, /* push edi */ 0x66, 0x56, /* push esi */ 0x66, 0xcb /* retf (o32) */ };

static void recover_h(int s, siginfo_t *si, void *uv) {
    ucontext_t *u = uv; _STRUCT_X86_THREAD_STATE64 *t = &u->uc_mcontext->__ss;
    g_sig = s; g_code = si->si_code; g_addr = (uint64_t)si->si_addr; g_rip = t->__rip; g_rax = t->__rax;
    printf("    [signal %s code=%d si_addr=%#llx ctx: rip=%#llx cs=%#llx fs=%#llx gs=%#llx eax=%#llx]\n", signame(s), si->si_code, (unsigned long long)si->si_addr, (unsigned long long)t->__rip, (unsigned long long)t->__cs, (unsigned long long)t->__fs, (unsigned long long)t->__gs, (unsigned long long)t->__rax);
    t->__cs = sel_cs64; t->__rip = (uint64_t)recover64; t->__rsp = g_rsp64; t->__rflags &= ~0x100;
}
static void survey(int k) {
    switch (k) {
    case 0: for (int idx = 0; idx <= 2; idx++) printf("  LDT idx %d data -> %s\n", idx, set_seg(idx, 0, 0, 0xfffff, 1, 1) >= 0 ? "ok" : "EINVAL (reserved)"); break;
    case 1: { int idxs[] = {3, 7, 100, 1000, 8190, 8191, 8192}; for (unsigned i = 0; i < sizeof idxs / sizeof *idxs; i++) { int r = set_seg(idxs[i], 0, 0, 0xfffff, 1, 1); printf("  LDT idx %-5d data 32-bit flat -> %s\n", idxs[i], r >= 0 ? "ok" : strerror(-r)); } } break;
    case 2: { int r = set_seg(9, 1, 0, 0xfffff, 1, 0); printf("  16-bit-default code segment (D=0) -> %s\n", r >= 0 ? "ok" : strerror(-r)); } break;
    case 3: { ldt_entry_t e[2]; memset(e, 0, sizeof e); int r = i386_set_ldt(11, e, 2); printf("  two descriptors in one call -> %s\n", r >= 0 ? "ok" : strerror(errno)); } break;
    case 4: { int r = i386_set_ldt(LDT_AUTO_ALLOC, NULL, 1); printf("  LDT_AUTO_ALLOC -> %s\n", r >= 0 ? "ok" : strerror(errno)); } break;
    case 5: { set_seg(3, 0, 0, 0xfffff, 1, 1); ldt_entry_t e; int r = i386_get_ldt(3, &e, 1); printf("  i386_get_ldt(3) -> %d (entry type=%d dpl=%d)\n", r, e.data.type, e.data.dpl); } break;
    case 6: { ldt_entry_t e; memset(&e, 0, sizeof e); e.data.type = DESC_DATA_WRITE; e.data.dpl = 0; e.data.present = 1; int r = i386_set_ldt(12, &e, 1); printf("  DPL 0 data descriptor -> %s\n", r >= 0 ? "ok" : strerror(errno)); } break;
    case 7: { int r = set_seg(13, 0, 0x40000000, 0xfffff, 1, 1); printf("  data descriptor with base 0x40000000 -> %s\n", r >= 0 ? "ok" : strerror(-r)); } break;
    case 8: { int r = set_seg(14, 0, 0, 0xfffff, 1, 0); printf("  16-bit-default (B=0) data/stack descriptor -> %s\n", r >= 0 ? "ok" : strerror(-r)); } break;
    case 9: { int r = set_seg(15, 0, 0, 0xfffff, 0, 1); printf("  byte-granular big limit data descriptor -> %s\n", r >= 0 ? "ok" : strerror(-r)); } break;
    }
}
static const char *names[] = {"basic 32-bit entry/exit (eax=0x1234)", "cpuid in 32-bit (ebx vendor 'Genu'=0x756e6547)", "mov fs,<flat ldt sel>; fs:[ptr] reads marker 0xC0FFEE11", "mov es,<flat ldt sel>; es:[ptr] reads marker", "mov gs,<flat ldt sel>; gs:[ptr] (runtime strings: gs != 0 unsupported)", "push ds; pop es; push cs; pop eax (eax=cs32 sel)", "far call/retf within 32-bit cs (eax=0x55)", "ljmp far same cs (eax=0x66)", "-", "int 0x80 getpid in 32-bit (eax = pid)", "-", "daa after add 0x79+0x35 (eax 0x1_14: CF<<8|0x14)", "pusha/popa roundtrip (eax+8*ecx=1+16=17)", "-", "les eax,m16:32 (loads es from far ptr, eax=off)", "mov ds,0x1234 (invalid selector, HW: #GP)", "in al,0x60 in 32-bit (HW: #GP w/o IOPL)", "sahf/lahf roundtrip (eax=ah)", "load from [0xffffffff] (unmapped -> SIGSEGV)", "idiv INT_MIN/-1 in 32-bit (SIGFPE)", "sysenter in 32-bit"};
static uint32_t stub_addr(int n) { char *p[] = {s0, s1, s2, s3, s4, s5, s6, s6, s7, s9, s9, s11, s12, s12, s14, s15, s16, s17, s18, s19, s20}; char *a[] = {s0,s1,s2,s3,s4,s5,s6,s7,0,s9,0,s11,s12,0,s14,s15,s16,s17,s18,s19,s20}; (void)p; return (uint32_t)(uintptr_t)a[n]; }

static void run_one(int n) {
    gaps_install();
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = recover_h; sa.sa_flags = SA_SIGINFO | SA_NODEFER; sigemptyset(&sa.sa_mask);
    int ss[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP}; for (unsigned i = 0; i < 5; i++) sigaction(ss[i], &sa, 0);
    sel_cs32 = ldt_sel(3); sel_ds32 = ldt_sel(4); sel_fs = ldt_sel(5);
    if (set_seg(3, 1, 0, 0xfffff, 1, 1) < 0 || set_seg(4, 0, 0, 0xfffff, 1, 1) < 0) { printf("  LDT setup failed\n"); return; }
    blk.selfs = sel_ds32;
    blk.tebptr = (uint32_t)(uintptr_t)&teb_marker;
    __asm__ volatile("mov %%cs,%0" : "=r"(sel_cs64));
    unsigned char *stk = mmap((void *)0x30000000, 0x10000, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
    if (stk == MAP_FAILED) { printf("  low stack mmap failed: %s\n", strerror(errno)); return; }
    uint64_t top = (uint64_t)(uintptr_t)stk + 0x8000;
    blk.farsub[0] = (uint32_t)(uintptr_t)s6sub; blk.farsub[1] = sel_cs32; blk.far2[0] = (uint32_t)(uintptr_t)s7t; blk.far2[1] = sel_cs32;
    uint32_t eip = stub_addr(n), cs = sel_cs32;
    if (n == 14) { blk.far2[0] = 0xaaaa5555; blk.far2[1] = sel_ds32; }
    if (n == 8 || n == 10 || n == 13) { printf("  (n/a)\n"); return; }
    if (n == 99) { /* 16-bit code segment */
        uint32_t a = (uint32_t)(uintptr_t)code16; int r = set_seg(6, 1, a & ~0xffffu, 0xffff, 0, 0); if (r < 0) { printf("  16-bit code seg: i386_set_ldt -> %s\n", strerror(-r)); return; }
        eip = a & 0xffff; cs = ldt_sel(6); }
    printf("stub %d: %s\n", n, n == 99 ? "16-bit code segment, o32 retf" : names[n]);
    uint32_t r = run32(eip, cs, sel_ds32, sel_cs64, top, (uint32_t)(uintptr_t)&blk);
    printf("  returned eax=%#x\n", r);
    if (n == 9) printf("  (getpid()=%d)\n", getpid());
    if (n == 14) printf("  (expected eax=0xaaaa5555)\n");
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc > 1) { int n = atoi(argv[1]); if (n >= 200) { gaps_install(); survey(n - 200); } else run_one(n); return 0; }
    gaps_install();
    printf("== LDT survey then 32-bit stubs, each in its own process (a runtime abort is not catchable)\n");
    int list[] = {200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 12, 14, 15, 16, 17, 18, 19, 20, 99};
    for (unsigned i = 0; i < sizeof list / sizeof *list; i++) { char n[16]; snprintf(n, sizeof n, "%d", list[i]); fflush(stdout);
        pid_t pid; char *av[] = {argv[0], n, NULL}; posix_spawn(&pid, argv[0], NULL, NULL, av, environ); int st = 0; waitpid(pid, &st, 0);
        if (WIFSIGNALED(st)) printf("  -> child killed by signal %d\n", WTERMSIG(st)); else if (WEXITSTATUS(st)) printf("  -> child exit status %d\n", WEXITSTATUS(st)); }
    return 0;
}
