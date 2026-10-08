// 32-bit (compat-mode) corner probes via the same LDT + far-jump gate technique as tests/test_decoder_arpl.c.
// Build: clang -arch x86_64 -O1 -Wl,-pagezero_size,0x4000 -o compat32 compat32.c
#include "common.h"
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <architecture/i386/desc.h>
#include <architecture/i386/table.h>
#include <i386/user_ldt.h>
static uint16_t sel_cs32, sel_ds32, sel_cs64;
static uint16_t ldt_sel(int i) { return (uint16_t)((i << 3) | 4 | 3); }
static uint16_t alloc_seg(int idx, int code) {
  ldt_entry_t e; memset(&e, 0, sizeof e);
  if (code) { e.code.limit00 = 0xFFFF; e.code.type = DESC_CODE_READ; e.code.dpl = 3; e.code.present = 1; e.code.limit16 = 0xF; e.code.opsz = DESC_CODE_32B; e.code.granular = DESC_GRAN_PAGE; }
  else { e.data.limit00 = 0xFFFF; e.data.type = DESC_DATA_WRITE; e.data.dpl = 3; e.data.present = 1; e.data.limit16 = 0xF; e.data.stksz = DESC_DATA_32B; e.data.granular = DESC_GRAN_PAGE; }
  if (i386_set_ldt(idx, &e, 1) < 0) { printf("i386_set_ldt failed errno=%d\n", errno); exit(1); }
  return ldt_sel(idx); }
#define STUB(name, bytes) __asm__(".section __TEXT,__text\n .align 4\n .globl _" #name "\n _" #name ":\n " bytes "\n .byte 0x57, 0x56, 0xcb\n"); extern uint8_t name[];
STUB(g_daa, ".byte 0x27") STUB(g_das, ".byte 0x2f") STUB(g_aaa, ".byte 0x37") STUB(g_aas, ".byte 0x3f") STUB(g_aam, ".byte 0xd4,0x0a") STUB(g_aad, ".byte 0xd5,0x0a")
STUB(g_salc, ".byte 0xd6") STUB(g_inc, ".byte 0x40") STUB(g_movcs, ".byte 0x8c,0xc8") STUB(g_into, ".byte 0xce") STUB(g_pushpop, ".byte 0x60,0x61")
STUB(g_bound, ".byte 0x62,0x0b") STUB(g_cmpxchg_lock16, ".byte 0x66,0xf0,0x0f,0xb1,0x0b") STUB(g_nop, ".byte 0x90")
STUB(g_fstenv, ".byte 0xd9,0x33") /* fnstenv (%ebx): 32-bit protected-mode layout */ STUB(g_fnstenv16, ".byte 0x66,0xd9,0x33") /* 16-bit operand-size fnstenv */
STUB(g_pushes, ".byte 0x06,0x07") STUB(g_op82, ".byte 0x82,0xc0,0x01") STUB(g_int1, ".byte 0xf1") STUB(g_enter, ".byte 0xc8,0x08,0x00,0x00,0xc9")
STUB(g_xlat, ".byte 0xd7") STUB(g_les, ".byte 0xc4,0x03") STUB(g_ud2, ".byte 0x0f,0x0b") STUB(g_cpuid, ".byte 0x0f,0xa2") STUB(g_rdtsc, ".byte 0x0f,0x31")
STUB(g_movseg, ".byte 0x8e,0xd8") STUB(g_pushcs, ".byte 0x0e,0x58") STUB(g_lahf, ".byte 0x9f") STUB(g_sysenter, ".byte 0x0f,0x34")
typedef struct { uint32_t eax, ebx, ecx, edx; uint64_t fi; uint32_t foff; uint16_t fsel, pad; uint32_t ds32, cs64; uint64_t stk; uint32_t oeax, oebx, oecx, oedx, ofl; } IO;
static uint8_t lowstk[8192] __attribute__((aligned(16)));
static sigjmp_buf jb; static volatile int sig_hit;
static void onsig(int s) { sig_hit = s; siglongjmp(jb, 1); }
static int run32(uint8_t* stub, IO* io) {
  io->foff = (uint32_t)(uintptr_t)stub; io->fsel = sel_cs32; io->ds32 = sel_ds32; io->cs64 = sel_cs64; io->stk = (uint64_t)(uintptr_t)(lowstk + sizeof lowstk - 64);
  register IO* p __asm__("r11") = io; sig_hit = 0;
  if (sigsetjmp(jb, 1)) return sig_hit;
  __asm__ volatile("movq %%rsp, %%r12\n mov %%ds, %%r13w\n mov %%es, %%r14w\n mov %%ss, %%r15w\n leaq 1f(%%rip), %%rsi\n movl 36(%%r11), %%edi\n movl 32(%%r11), %%r10d\n"
    " mov %%r10w, %%ds\n mov %%r10w, %%es\n mov %%r10w, %%ss\n movq 40(%%r11), %%rsp\n pushq 16(%%r11)\n popfq\n movl 0(%%r11), %%eax\n movl 4(%%r11), %%ebx\n movl 8(%%r11), %%ecx\n movl 12(%%r11), %%edx\n ljmp *24(%%r11)\n"
    "1:\n mov %%r13w, %%ds\n mov %%r14w, %%es\n mov %%r15w, %%ss\n movl %%eax, 48(%%r11)\n movl %%ebx, 52(%%r11)\n movl %%ecx, 56(%%r11)\n movl %%edx, 60(%%r11)\n pushfq\n popq %%rax\n movl %%eax, 64(%%r11)\n movq %%r12, %%rsp\n"
    :: "r"(p) : "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r10", "r12", "r13", "r14", "r15", "cc", "memory");
  return 0; }
enum { CF = 1, PF = 4, AF = 0x10, ZF = 0x40, SF = 0x80, OF = 0x800 };
static uint32_t pzs(uint8_t r) { uint32_t f = 0; if (!r) f |= ZF; if (r & 0x80) f |= SF; if (!(__builtin_popcount(r) & 1)) f |= PF; return f; }
static void sweep(const char* nm, uint8_t* stub, int kind) {
  long n = 0, bad = 0, badf[6] = {0}; char first[200] = ""; uint32_t fm[] = {CF, PF, AF, ZF, SF, OF};
  for (int al = 0; al < 256; al++) for (int ah = (kind >= 2 ? 0 : 0); ah < (kind >= 2 ? 6 : 1); ah++) for (int cf = 0; cf < 2; cf++) for (int af = 0; af < 2; af++) {
    IO io = {0}; io.eax = 0xAABB0000u | (ah << 8) | al; io.fi = 0x202 | (cf ? CF : 0) | (af ? AF : 0) | (rng() & (PF | ZF | SF | OF));
    if (kind == 4) io.ebx = 0; // salc
    int s = run32(stub, &io); if (s) { printf("%s: signal %d at al=%02x\n", nm, s, al); return; }
    uint8_t A = al, H = ah; uint32_t f = 0, defined = 0; uint8_t RA = A, RH = H; int CFv = cf, AFv = af;
    switch (kind) {
      case 0: { uint8_t old = A; int ocf = CFv; CFv = 0; if ((A & 15) > 9 || AFv) { int c = (A + 6) > 0xff; A += 6; CFv = ocf || c; AFv = 1; } else AFv = 0; if (old > 0x99 || ocf) { A += 0x60; CFv = 1; } else CFv = 0; f = pzs(A) | (CFv ? CF : 0) | (AFv ? AF : 0); defined = CF | AF | PF | ZF | SF; RA = A; break; }
      case 1: { uint8_t old = A; int ocf = CFv; CFv = 0; if ((A & 15) > 9 || AFv) { int b = A < 6; A -= 6; CFv = ocf || b; AFv = 1; } else AFv = 0; if (old > 0x99 || ocf) { A -= 0x60; CFv = 1; } f = pzs(A) | (CFv ? CF : 0) | (AFv ? AF : 0); defined = CF | AF | PF | ZF | SF; RA = A; break; }
      case 2: { uint16_t ax = (uint16_t)(H << 8 | A); if ((A & 15) > 9 || AFv) { ax += 0x106; AFv = 1; CFv = 1; } else { AFv = 0; CFv = 0; } RA = ax & 0xf; RH = ax >> 8; f = (CFv ? CF : 0) | (AFv ? AF : 0); defined = CF | AF; break; }
      case 3: { uint16_t ax = (uint16_t)(H << 8 | A); if ((A & 15) > 9 || AFv) { ax -= 6; ax = (uint16_t)(ax - 0x100); AFv = 1; CFv = 1; } else { AFv = 0; CFv = 0; } RA = ax & 0xf; RH = ax >> 8; f = (CFv ? CF : 0) | (AFv ? AF : 0); defined = CF | AF; break; }
      case 5: { RH = A / 10; RA = A % 10; f = pzs(RA); defined = PF | ZF | SF; break; }       // aam
      case 6: { RA = (uint8_t)(A + H * 10); RH = 0; f = pzs(RA); defined = PF | ZF | SF; break; } // aad
      case 4: { RA = CFv ? 0xff : 0; defined = 0; f = 0; break; }
    }
    uint32_t want_eax = 0xAABB0000u | (RH << 8) | RA; if (kind == 4) want_eax = 0xAABB0000u | (H << 8) | RA;
    n++; int b = 0; if ((io.oeax & 0xffff) != (want_eax & 0xffff) || (io.oeax >> 16) != 0xAABB) b = 1;
    for (int k = 0; k < 6; k++) if ((defined & fm[k]) && ((io.ofl ^ f) & fm[k])) { badf[k]++; b = 1; }
    if (b && !bad++) snprintf(first, sizeof first, "in eax=%08x fl=%x -> eax=%08x fl=%x; want eax=%08x flags(def)=%x", io.eax, (unsigned)io.fi & 0x8d5, io.oeax, io.ofl & 0x8d5, want_eax, f); }
  printf("%-6s n=%ld mismatches=%ld (CF=%ld PF=%ld AF=%ld ZF=%ld SF=%ld OF=%ld) %s%s\n", nm, n, bad, badf[0], badf[1], badf[2], badf[3], badf[4], badf[5], bad ? "first: " : "ok", first); }
int main(int argc, char** argv) {
  struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = onsig; sigaction(SIGILL, &sa, 0); sigaction(SIGSEGV, &sa, 0); sigaction(SIGBUS, &sa, 0); sigaction(SIGFPE, &sa, 0); sigaction(SIGTRAP, &sa, 0);
  sel_cs32 = alloc_seg(3, 1); sel_ds32 = alloc_seg(4, 0); __asm__ volatile("mov %%cs, %0" : "=r"(sel_cs64));
  printf("selectors: cs32=%04x ds32=%04x cs64=%04x\n", sel_cs32, sel_ds32, sel_cs64);
  IO io = {0}; io.eax = 0x1234; int s;
  s = run32(g_nop, &io); printf("gate sanity (nop): signal=%d eax=%x\n", s, io.oeax);
  io.eax = 0xAABB0010; io.ebx = 0; s = run32(g_inc, &io); printf("0x40 in compat = inc eax: eax=%08x (want AABB0011) sig=%d\n", io.oeax, s);
  s = run32(g_movcs, &io); printf("mov %%cs,%%eax in compat: eax=%08x (cs32=%04x) sig=%d\n", io.oeax, sel_cs32, s);
  sweep("daa", g_daa, 0); sweep("das", g_das, 1); sweep("aaa", g_aaa, 2); sweep("aas", g_aas, 3); sweep("salc", g_salc, 4); sweep("aam10", g_aam, 5); sweep("aad10", g_aad, 6);
  { IO i2 = {0}; i2.eax = 1; i2.fi = 0x202 | 0x800; s = run32(g_into, &i2); printf("into with OF=1: signal=%d (real HW: SIGSEGV/#OF->SIGSEGV or SIGFPE?)\n", s); }
  { IO i2 = {0}; i2.eax = 1; s = run32(g_pushpop, &i2); printf("pusha/popa: signal=%d eax=%x\n", s, i2.oeax); }
  { IO i2 = {0}; static uint8_t buf[28] __attribute__((aligned(16))); i2.ebx = (uint32_t)(uintptr_t)buf; __asm__ volatile("fninit\n fld1\n fldz\n" ::: "st", "st(1)"); s = run32(g_fstenv, &i2); __asm__ volatile("fninit");
    printf("compat fnstenv 32-bit layout: signal=%d cw=%02x%02x tag=%02x%02x (want tag 0xfff0|...)\n", s, buf[1], buf[0], buf[9], buf[8]); }
  { struct { const char* n; uint8_t* st; } d[] = {{"push es/pop es", g_pushes}, {"0x82 add al,1", g_op82}, {"int1/icebp", g_int1}, {"enter 8,0; leave", g_enter}, {"xlat", g_xlat}, {"les (%ebx),%eax", g_les}, {"ud2", g_ud2}, {"cpuid", g_cpuid}, {"rdtsc", g_rdtsc}, {"mov %eax,%ds (eax=ds32)", g_movseg}, {"push cs; pop eax", g_pushcs}, {"bound (%ebx),%ecx", g_bound}, {"sysenter", g_sysenter}};
    static uint8_t tbl[16] __attribute__((aligned(16))) = {0, 1, 2, 3};
    for (unsigned i = 0; i < sizeof d / sizeof *d; i++) { IO i2 = {0}; i2.eax = sel_ds32; i2.ebx = (uint32_t)(uintptr_t)tbl; i2.ecx = 5; i2.fi = 0x202; int sg = run32(d[i].st, &i2); printf("decode %-26s signal=%d eax=%08x\n", d[i].n, sg, i2.oeax); } }
  return 0; }
