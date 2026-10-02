// x87 fidelity probes for STOCK Rosetta. usage: x87a <section>
#include "common.h"
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>

#define BIN(name, ins) \
  static f80 name(f80 a, f80 b, uint16_t* s) { f80 r; uint16_t w; \
    __asm__ volatile("fnclex\n fldt %3\n fldt %2\n " ins "\n fnstsw %1\n fstpt %0\n fstp %%st(0)\n" \
                     : "=m"(r), "=a"(w) : "m"(a), "m"(b) : "st", "st(1)"); *s = w; return r; }
// st0 = a (op) b ; (b is loaded first so it is st1)
BIN(op_add, "fadd %%st(1), %%st")
BIN(op_sub, "fsub %%st(1), %%st")
BIN(op_mul, "fmul %%st(1), %%st")
BIN(op_div, "fdiv %%st(1), %%st")
static f80 op_sqrt(f80 a, f80 b, uint16_t* s) { f80 r; uint16_t w; (void)b;
  __asm__ volatile("fnclex\n fldt %2\n fsqrt\n fnstsw %1\n fstpt %0\n" : "=m"(r), "=a"(w) : "m"(a) : "st"); *s = w; return r; }
static f80 op_rint(f80 a, f80 b, uint16_t* s) { f80 r; uint16_t w; (void)b;
  __asm__ volatile("fnclex\n fldt %2\n frndint\n fnstsw %1\n fstpt %0\n" : "=m"(r), "=a"(w) : "m"(a) : "st"); *s = w; return r; }
typedef f80 (*opfn)(f80, f80, uint16_t*);
static f80 F(double d) { f80 r; __asm__ volatile("fldl %1\n fstpt %0" : "=m"(r) : "m"(d) : "st"); return r; }
static f80 POW2(int e) { return mk((uint16_t)(0x3fff + e), 0x8000000000000000ull); }
static f80 RAND80(int emin, int emax) {
  uint64_t m = rng() | 0x8000000000000000ull; int e = emin + (int)(rng() % (uint64_t)(emax - emin + 1));
  uint16_t se = (uint16_t)(0x3fff + e) | ((rng() & 1) ? 0x8000 : 0); return mk(se, m); }

static const char* opn[] = {"add", "sub", "mul", "div", "sqrt", "rint"};
static opfn ops[] = {op_add, op_sub, op_mul, op_div, op_sqrt, op_rint};

// R lines are verified by check_round.py
static void sec_round(void) {
  for (int pc = 0; pc < 4; pc++) { if (pc == 1) continue;
    for (int rc = 0; rc < 4; rc++) {
      uint16_t cw = 0x037f & ~0x0f00; cw |= (pc << 8) | (rc << 10); set_cw(cw);
      for (int op = 0; op < 5; op++) for (int i = 0; i < 400; i++) {
        f80 a = RAND80(-8, 8), b = RAND80(-8, 8);
        if (op == 4) a = mk(f80se(a) & 0x7fff, f80m(a));
        if (i % 7 == 0) b = mk(f80se(b), f80m(b) & ~0x00000000ffffffffull | 0x80000000ffffffffull); // close ties
        uint16_t sw; f80 r = ops[op](a, b, &sw);
        printf("R %s cw=%04x ", opn[op], cw); p80(a); printf(" "); p80(b); printf(" -> "); p80(r); printf(" sw=%04x\n", sw);
      }
    }
  }
  set_cw(0x37f);
}
// frndint + fist rounding for halves in each rounding mode
static void sec_rint(void) {
  double v[] = {0.5, 1.5, 2.5, 3.5, -0.5, -1.5, -2.5, 0.4999, 2.5000001, -2.5000001, 1e9 + 0.5, 4503599627370497.5};
  const char* rcn[] = {"nearest", "down", "up", "trunc"};
  for (int rc = 0; rc < 4; rc++) {
    set_cw((uint16_t)(0x037f | (rc << 10)));
    printf("RC=%s:", rcn[rc]);
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
      double d = v[i]; long long q; int32_t l32; f80 r; uint16_t sw;
      r = op_rint(F(d), F(0), &sw);
      __asm__ volatile("fldl %1\n fistpll %0\n" : "=m"(q) : "m"(d) : "st");
      __asm__ volatile("fldl %1\n fistpl %0\n" : "=m"(l32) : "m"(d) : "st");
      printf(" [%g rint=", d); p80(r); printf(" fistpll=%lld fistpl=%d]", q, l32);
    }
    printf("\n");
  }
  set_cw(0x37f);
}
static void sec_pcrange(void) {
  uint16_t sw; f80 r;
  for (int pc = 0; pc < 4; pc++) { if (pc == 1) continue; set_cw((uint16_t)(0x037f & ~0x300) | (pc << 8));
    printf("PC=%d cw=%04x\n", pc, rd_cw());
    r = op_mul(POW2(-200), POW2(-200), &sw); printf("  2^-200*2^-200 = "); p80(r); printf(" sw=%04x (expect 3c37:8000.. if extended exponent range kept)\n", sw);
    r = op_mul(POW2(200), POW2(200), &sw); printf("  2^200*2^200   = "); p80(r); printf(" sw=%04x\n", sw);
    r = op_mul(POW2(-100), POW2(-100), &sw); printf("  2^-100*2^-100 = "); p80(r); printf(" sw=%04x\n", sw);
    r = op_add(POW2(0), POW2(-30), &sw); printf("  1+2^-30       = "); p80(r); printf(" sw=%04x\n", sw);
    r = op_add(POW2(0), POW2(-63), &sw); printf("  1+2^-63       = "); p80(r); printf(" sw=%04x\n", sw);
    r = op_div(F(1), F(3), &sw); printf("  1/3           = "); p80(r); printf(" sw=%04x\n", sw);
    r = op_sqrt(F(2), F(0), &sw); printf("  sqrt(2)       = "); p80(r); printf(" sw=%04x\n", sw);
    f80 big = mk(0x7ffe, 0xffffffffffffffffull); r = op_add(big, big, &sw); printf("  maxext+maxext = "); p80(r); printf(" sw=%04x\n", sw);
  }
  set_cw(0x37f);
}
static void sec_denorm(void) {
  uint16_t sw; f80 r; set_cw(0x037f);
  r = op_mul(mk(0x0001, 0x8000000000000000ull), POW2(-1), &sw); printf("minnormal*0.5 = "); p80(r); printf(" sw=%04x (expect 0000:4000.. UE? exact => none)\n", sw);
  r = op_mul(POW2(-16000), POW2(-445), &sw); printf("2^-16445 (min denorm) = "); p80(r); printf(" sw=%04x\n", sw);
  r = op_mul(POW2(-16000), POW2(-446), &sw); printf("2^-16446 (half min denorm, tie->0) = "); p80(r); printf(" sw=%04x\n", sw);
  r = op_mul(mk(0, 3), POW2(-1), &sw); printf("3*2^-16445 *0.5 = "); p80(r); printf(" sw=%04x (tie to even: 2*2^-16445)\n", sw);
  r = op_add(mk(0, 1), mk(0, 1), &sw); printf("denorm+denorm = "); p80(r); printf(" sw=%04x (DE expected)\n", sw);
  r = op_mul(POW2(-10000), POW2(-10000), &sw); printf("2^-10000*2^-10000 = "); p80(r); printf(" sw=%04x (expect 0, UE|PE=0x30)\n", sw);
  r = op_mul(POW2(10000), POW2(10000), &sw); printf("2^10000*2^10000 = "); p80(r); printf(" sw=%04x (expect inf, OE|PE=0x28)\n", sw);
  set_cw(0x037f | (3 << 10)); r = op_mul(POW2(10000), POW2(10000), &sw); printf("RC=trunc overflow = "); p80(r); printf(" sw=%04x (expect max finite 7ffe:ffff..)\n", sw);
  set_cw(0x037f);
}
static const char* swbits(uint16_t w) { static char b[64]; snprintf(b, sizeof b, "%s%s%s%s%s%s%s", (w&1)?"IE ":"", (w&2)?"DE ":"", (w&4)?"ZE ":"", (w&8)?"OE ":"", (w&16)?"UE ":"", (w&32)?"PE ":"", (w&64)?"SF ":""); return b; }
#define EXC(name, want, expr) do { fpinit(); uint16_t s_; expr; s_ = rd_sw(); uint16_t g_ = s_ & 0x7f; \
  printf("%-34s sw=%04x flags=[%s] want=%02x %s\n", name, s_, swbits(s_), want, g_ == (want) ? "OK" : "DIFF"); fpinit(); } while (0)
static void sec_exc(void) {
  uint16_t s; (void)s;
  EXC("1/0", 0x04, (void)op_div(F(1), F(0), &s));
  EXC("0/0", 0x01, (void)op_div(F(0), F(0), &s));
  EXC("sqrt(-1)", 0x01, (void)op_sqrt(F(-1), F(0), &s));
  EXC("1/3 inexact", 0x20, (void)op_div(F(1), F(3), &s));
  EXC("1+2^-65 inexact", 0x20, (void)op_add(POW2(0), POW2(-65), &s));
  EXC("denorm*1 (DE only)", 0x02, (void)op_mul(mk(0, 5), F(1), &s));
  EXC("2^10000*2^10000", 0x28, (void)op_mul(POW2(10000), POW2(10000), &s));
  EXC("2^-10000*2^-10000", 0x30, (void)op_mul(POW2(-10000), POW2(-10000), &s));
  EXC("inf-inf", 0x01, (void)op_sub(mk(0x7fff, 0x8000000000000000ull), mk(0x7fff, 0x8000000000000000ull), &s));
  EXC("unnormal + 1.0", 0x01, (void)op_add(mk(0x3fff, 0x4000000000000000ull), F(1), &s));
  EXC("pseudo-denormal + 1.0", 0x22, (void)op_add(mk(0, 0x8000000000000000ull), F(1), &s)); // intel: DE|PE
  { f80 q = mk(0x7fff, 0xc000000000000000ull), sn = mk(0x7fff, 0xa000000000000000ull); uint32_t sn32 = 0x7fa00000; double a = 1;
    EXC("fcom qNaN (ordered)", 0x01, __asm__ volatile("fldt %0\n fldl %1\n fcom %%st(1)\n fstp %%st(0)\n fstp %%st(0)\n" ::"m"(q), "m"(a) : "st", "st(1)"));
    EXC("fucom qNaN (quiet)", 0x00, __asm__ volatile("fldt %0\n fldl %1\n fucom %%st(1)\n fstp %%st(0)\n fstp %%st(0)\n" ::"m"(q), "m"(a) : "st", "st(1)"));
    EXC("fucom sNaN", 0x01, __asm__ volatile("fldt %0\n fldl %1\n fucom %%st(1)\n fstp %%st(0)\n fstp %%st(0)\n" ::"m"(sn), "m"(a) : "st", "st(1)"));
    EXC("fld m32 sNaN", 0x01, __asm__ volatile("flds %0\n fstp %%st(0)\n" ::"m"(sn32) : "st"));
    EXC("fld m80 sNaN (no IE)", 0x00, __asm__ volatile("fldt %0\n fstp %%st(0)\n" ::"m"(sn) : "st"));
    EXC("sNaN + 1.0", 0x01, (void)op_add(sn, F(1), &s));
    EXC("qNaN + 1.0 (quiet)", 0x00, (void)op_add(q, F(1), &s)); }
  { double big = 1e300; float f; EXC("fstp m32 of 1e300", 0x28, __asm__ volatile("fldl %1\n fstps %0\n" : "=m"(f) : "m"(big) : "st"));
    int16_t i16; double d = 40000; EXC("fistp m16 of 40000", 0x01, __asm__ volatile("fldl %1\n fistps %0\n" : "=m"(i16) : "m"(d) : "st"));
    int32_t i32; d = 2.5; EXC("fistp m32 of 2.5", 0x20, __asm__ volatile("fldl %1\n fistpl %0\n" : "=m"(i32) : "m"(d) : "st")); }
  EXC("fyl2x(-1)", 0x01, __asm__ volatile("fld1\n fld1\n fchs\n fyl2x\n fstp %%st(0)\n" ::: "st", "st(1)"));
  EXC("fxtract(0)", 0x04, __asm__ volatile("fldz\n fxtract\n fstp %%st(0)\n fstp %%st(0)\n" ::: "st", "st(1)"));
  EXC("fsqrt(denorm)", 0x22, (void)op_sqrt(mk(0, 4), F(0), &s));
  // sticky accumulation + fnclex
  fpinit(); (void)op_div(F(1), F(0), &s); (void)op_div(F(1), F(3), &s);
  printf("sticky ZE+PE: sw=%04x [%s] (want 0x24)\n", rd_sw() & 0xff, swbits(rd_sw()));
  fpclex(); printf("after fnclex: sw=%04x\n", rd_sw());
  fpinit(); __asm__ volatile("fld1\n fldz\n fcompp\n" ::: "st", "st(1)"); uint16_t w = rd_sw(); __asm__ volatile("fnclex"); uint16_t w2 = rd_sw();
  printf("fnclex keeps C bits: before=%04x after=%04x (C bits 0x4500 preserved?)\n", w, w2);
  fpinit(); printf("fninit: cw=%04x sw=%04x\n", rd_cw(), rd_sw());
  set_cw(0xffff); printf("fldcw 0xffff reads back %04x\n", rd_cw()); set_cw(0x37f);
}
// ---- unmasked exceptions
static sigjmp_buf jb; static volatile int got_sig, got_code;
static void onfpe(int sig, siginfo_t* si, void* uc) { (void)uc; got_sig = sig; got_code = si->si_code; siglongjmp(jb, 1); }
static void sec_sigfpe(void) {
  struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = onfpe; sa.sa_flags = SA_SIGINFO; sigaction(SIGFPE, &sa, 0); sigaction(SIGILL, &sa, 0);
  const char* nm[] = {"ZE (1/0)", "IE (0/0)", "OE", "PE (1/3)"}; uint16_t unmask[] = {0x04, 0x01, 0x08, 0x20};
  for (int k = 0; k < 4; k++) {
    fpinit(); got_sig = 0; uint16_t s; set_cw((uint16_t)(0x037f & ~unmask[k]));
    if (sigsetjmp(jb, 1) == 0) {
      switch (k) { case 0: (void)op_div(F(1), F(0), &s); break; case 1: (void)op_div(F(0), F(0), &s); break;
        case 2: (void)op_mul(POW2(10000), POW2(10000), &s); break; default: (void)op_div(F(1), F(3), &s); }
      uint16_t w = rd_sw(); __asm__ volatile("fwait");
      printf("unmasked %-10s: no signal delivered at fwait; sw=%04x (ES=%d B=%d)\n", nm[k], w, !!(w & 0x80), !!(w & 0x8000));
    } else printf("unmasked %-10s: signal %d si_code=%d delivered\n", nm[k], got_sig, got_code);
    fpinit();
  }
}
// ---- stack over/underflow
static void sec_stack(void) {
  fpinit(); for (int i = 0; i < 9; i++) __asm__ volatile("fld1" ::: "st");
  uint16_t w = rd_sw(); f80 top; __asm__ volatile("fstpt %0" : "=m"(top) :: "st");
  printf("9th fld1: sw=%04x [%s] C1=%d (overflow wants IE|SF, C1=1) st0 after=", w, swbits(w), !!(w & 0x200)); p80(top); printf("\n");
  fpinit(); __asm__ volatile("fstp %%st(0)" ::: "st"); w = rd_sw(); printf("pop of empty: sw=%04x [%s] C1=%d (underflow wants IE|SF, C1=0)\n", w, swbits(w), !!(w & 0x200));
  fpinit(); f80 r; __asm__ volatile("fstpt %0" : "=m"(r)); printf("fstpt empty -> "); p80(r); printf(" (real indefinite = ffff:c000000000000000)\n");
  fpinit(); uint16_t s; f80 x = op_add(F(1), F(2), &s); (void)x; __asm__ volatile("fadd %%st(1), %%st\n" ::: "st"); w = rd_sw(); printf("fadd with empty st(1): sw=%04x [%s]\n", w, swbits(w)); fpinit();
  fpinit(); __asm__ volatile("fld1\n fld1\n fld1\n" ::: "st"); { uint8_t env[28]; __asm__ volatile("fnstenv %0\n fldenv %0" : "=m"(env)); uint16_t sw, tw; memcpy(&sw, env + 4, 2); memcpy(&tw, env + 8, 2); printf("3 pushes: sw=%04x TOP=%d tw=%04x (want TOP=5, tw=0xffc0)\n", sw, (sw >> 11) & 7, tw); } fpinit();
}
// ---- tag word / env / fxsave / fnsave layout
static void dumpw(const char* t, const uint8_t* p, int n) { printf("%s", t); for (int i = 0; i < n; i++) printf("%02x%s", p[i], (i % 4 == 3) ? " " : ""); printf("\n"); }
static void sec_env(void) {
  fpinit(); double dv = 3.0, dv2 = 0; f80 inf = mk(0x7fff, 0x8000000000000000ull), qn = mk(0x7fff, 0xc000000000000000ull), dn = mk(0, 1);
  __asm__ volatile("fld1\n fldz\n fldt %0\n fldt %1\n fldt %2\n" :: "m"(inf), "m"(dn), "m"(qn) : "st");
  uint8_t env[28], fx[512] __attribute__((aligned(16))), sv[108];
  __asm__ volatile("fldl %2\n fmull %2\n fstpl %1\n fnstenv %0\n" : "=m"(env), "=m"(dv2) : "m"(dv) : "st");
  // note: fnstenv masks exceptions after saving; reload
  uint16_t cw, sw, tw; memcpy(&cw, env, 2); memcpy(&sw, env + 4, 2); memcpy(&tw, env + 8, 2);
  printf("fnstenv: cw=%04x sw=%04x tw=%04x (tw want 0x1abf for st0..4=qnan,denorm,inf,zero,1.0 w/ 5 live regs; note extra fld/fstp above is net zero)\n", cw, sw, tw);
  dumpw("  env raw: ", env, 28);
  uint32_t fip, fcs, fdp, fds; memcpy(&fip, env + 12, 4); memcpy(&fcs, env + 16, 4); memcpy(&fdp, env + 20, 4); memcpy(&fds, env + 24, 4);
  printf("  FIP=%08x FCS=%08x FDP=%08x FDS=%08x (real HW: FIP/FDP = last non-control insn / its mem operand; &dv=%p)\n", fip, fcs, fdp, fds, (void*)&dv);
  __asm__ volatile("fxsave %0" : "=m"(fx));
  uint16_t fcw, fsw, fop; uint64_t rip, rdp; uint32_t mx, mxm; memcpy(&fcw, fx, 2); memcpy(&fsw, fx + 2, 2); memcpy(&fop, fx + 6, 2); memcpy(&rip, fx + 8, 8); memcpy(&rdp, fx + 16, 8); memcpy(&mx, fx + 24, 4); memcpy(&mxm, fx + 28, 4);
  printf("fxsave: fcw=%04x fsw=%04x abridged_ftw=%02x (want f8) fop=%04x fip=%llx fdp=%llx mxcsr=%08x mask=%08x\n", fcw, fsw, fx[4], fop, (unsigned long long)rip, (unsigned long long)rdp, mx, mxm);
  for (int i = 0; i < 5; i++) { f80 v; memcpy(&v, fx + 32 + 16 * i, 10); printf("  fx ST(%d)=", i); p80(v); printf("\n"); }
  __asm__ volatile("fnsave %0" : "=m"(sv));
  for (int i = 0; i < 5; i++) { f80 v; memcpy(&v, sv + 28 + 10 * i, 10); printf("  fnsave ST(%d)=", i); p80(v); printf("\n"); }
  // modify saved ST1 then frstor
  f80 nv = mk(0x4000, 0xc000000000000000ull); memcpy(sv + 28 + 10, &nv, 10);
  __asm__ volatile("frstor %0" ::"m"(sv) : "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)");
  f80 a, b; __asm__ volatile("fstpt %0\n fstpt %1\n" : "=m"(a), "=m"(b) :: "st", "st(1)"); printf("frstor with modified ST1: pop0="); p80(a); printf(" pop1="); p80(b); printf(" (want qnan, then 4000:c000..)\n");
  fpinit();
}
// ---- fxam
static void sec_fxam(void) {
  struct { const char* n; f80 v; const char* want; } t[] = {
    {"+0", mk(0, 0), "100"}, {"-0", mk(0x8000, 0), "100"}, {"denormal", mk(0, 1), "110"}, {"pseudo-denormal", mk(0, 0x8000000000000000ull), "110?"},
    {"normal 1.0", mk(0x3fff, 0x8000000000000000ull), "010"}, {"-normal", mk(0xbfff, 0x8000000000000000ull), "010"}, {"unnormal", mk(0x3fff, 0x4000000000000000ull), "000"},
    {"+inf", mk(0x7fff, 0x8000000000000000ull), "011"}, {"-inf", mk(0xffff, 0x8000000000000000ull), "011"}, {"pseudo-inf", mk(0x7fff, 0), "000"},
    {"qNaN", mk(0x7fff, 0xc000000000000000ull), "001"}, {"sNaN", mk(0x7fff, 0xa000000000000000ull), "001"}, {"pseudo-NaN", mk(0x7fff, 0x4000000000000000ull), "000"},
    {"real indefinite", mk(0xffff, 0xc000000000000000ull), "001"}};
  for (unsigned i = 0; i < sizeof t / sizeof *t; i++) { fpinit(); __asm__ volatile("fldt %0\n fxam\n" ::"m"(t[i].v) : "st"); uint16_t w = rd_sw(); __asm__ volatile("fstp %%st(0)" ::: "st");
    printf("fxam %-16s C3C2C0=%d%d%d C1=%d want C3C2C0=%s\n", t[i].n, !!(w & 0x4000), !!(w & 0x400), !!(w & 0x100), !!(w & 0x200), t[i].want); }
  fpinit(); __asm__ volatile("fxam"); uint16_t w = rd_sw(); printf("fxam empty C3C2C0=%d%d%d (want 101)\n", !!(w & 0x4000), !!(w & 0x400), !!(w & 0x100));
  // fld/fstp roundtrip of exotic encodings must be bit exact
  int bad = 0; for (unsigned i = 0; i < sizeof t / sizeof *t; i++) { fpinit(); f80 o; __asm__ volatile("fldt %1\n fstpt %0" : "=m"(o) : "m"(t[i].v) : "st"); if (memcmp(&o, &t[i].v, 10)) { bad++; printf("ROUNDTRIP DIFF %s: ", t[i].n); p80(t[i].v); printf(" -> "); p80(o); printf("\n"); } }
  printf("fldt/fstpt roundtrip mismatches: %d of %u\n", bad, (unsigned)(sizeof t / sizeof *t));
  // m32/m64 SNaN -> quieted by fld; denormal m64 -> normalized in ext
  uint32_t s32 = 0x7fa00001; f80 o; __asm__ volatile("flds %1\n fstpt %0" : "=m"(o) : "m"(s32) : "st"); printf("flds sNaN32 -> "); p80(o); printf(" (want 7fff:e000020000000000-ish quiet)\n");
  uint64_t d64 = 1; __asm__ volatile("fldl %1\n fstpt %0" : "=m"(o) : "m"(d64) : "st"); printf("fldl denorm 2^-1074 -> "); p80(o); printf(" (want 3bcd:8000000000000000)\n");
}
// ---- fcomi / fucomi / fcmov
static void sec_cmp(void) {
  f80 q = mk(0x7fff, 0xc000000000000000ull), sn = mk(0x7fff, 0xa000000000000000ull);
  struct { const char* n; f80 a, b; } t[] = {{"1<2", F(1), F(2)}, {"2>1", F(2), F(1)}, {"1==1", F(1), F(1)}, {"qNaN", q, F(1)}, {"sNaN", sn, F(1)}, {"+0==-0", F(0), F(-0.0)}};
  for (int k = 0; k < 2; k++) for (unsigned i = 0; i < sizeof t / sizeof *t; i++) { uint64_t fl; uint16_t w;
    fpinit(); if (k == 0) __asm__ volatile("fldt %2\n fldt %3\n fcomi %%st(1), %%st\n pushfq\n popq %0\n fnstsw %1\n fstp %%st(0)\n fstp %%st(0)\n" : "=r"(fl), "=a"(w) : "m"(t[i].a), "m"(t[i].b) : "st", "st(1)", "cc");
    else __asm__ volatile("fldt %2\n fldt %3\n fucomi %%st(1), %%st\n pushfq\n popq %0\n fnstsw %1\n fstp %%st(0)\n fstp %%st(0)\n" : "=r"(fl), "=a"(w) : "m"(t[i].a), "m"(t[i].b) : "st", "st(1)", "cc");
    // st0 = b, st1 = a: compares b with a
    printf("%s %-7s (st0=b,st1=a) ZF=%d PF=%d CF=%d IE=%d\n", k ? "fucomi" : "fcomi ", t[i].n, !!(fl & 0x40), !!(fl & 4), !!(fl & 1), w & 1); }
  const char* nm[] = {"b", "e", "be", "u", "nb", "ne", "nbe", "nu"}; int diffs = 0;
  for (int fl = 0; fl < 8; fl++) { uint64_t f = 0x202 | ((fl & 1) ? 1 : 0) | ((fl & 2) ? 0x40 : 0) | ((fl & 4) ? 4 : 0); int CF = fl & 1, ZF = !!(fl & 2), PF = !!(fl & 4);
    int want[8] = {CF, ZF, CF || ZF, PF, !CF, !ZF, !CF && !ZF, !PF}; int got[8]; f80 A = F(1), B = F(2), r;
#define CM(k, ins) __asm__ volatile("fldt %1\n fldt %2\n pushq %3\n popfq\n " ins " %%st(1), %%st\n fstpt %0\n fstp %%st(0)\n" : "=m"(r) : "m"(A), "m"(B), "r"(f) : "st", "st(1)", "cc"); got[k] = (f80se(r) == 0x3fff) ; fpinit();
    CM(0, "fcmovb") CM(1, "fcmove") CM(2, "fcmovbe") CM(3, "fcmovu") CM(4, "fcmovnb") CM(5, "fcmovne") CM(6, "fcmovnbe") CM(7, "fcmovnu")
    for (int k = 0; k < 8; k++) if (got[k] != want[k]) { diffs++; printf("fcmov%s DIFF at CF=%d ZF=%d PF=%d: took=%d want=%d\n", nm[k], CF, ZF, PF, got[k], want[k]); } }
  printf("fcmov sweep (8 conditions x 8 flag combos): %d diffs\n", diffs);
}
// ---- fist / bcd
static void sec_fist(void) {
  set_cw(0x037f); int16_t i16; int32_t i32; int64_t i64; uint16_t w;
  struct { const char* n; f80 v; } t[] = {{"40000", F(40000)}, {"-32769", F(-32769)}, {"2^31", F(2147483648.0)}, {"-2^31", F(-2147483648.0)}, {"2^63", POW2(63)}, {"-2^63", mk(0xc03e, 0x8000000000000000ull)}, {"NaN", mk(0x7fff, 0xc000000000000000ull)}, {"+inf", mk(0x7fff, 0x8000000000000000ull)}, {"2.5", F(2.5)}, {"-2.5", F(-2.5)}, {"3.5", F(3.5)}};
  for (unsigned i = 0; i < sizeof t / sizeof *t; i++) { fpinit();
    __asm__ volatile("fldt %2\n fistps %0\n fnstsw %1\n" : "=m"(i16), "=a"(w) : "m"(t[i].v) : "st"); uint16_t w1 = w; fpinit();
    __asm__ volatile("fldt %2\n fistpl %0\n fnstsw %1\n" : "=m"(i32), "=a"(w) : "m"(t[i].v) : "st"); uint16_t w2 = w; fpinit();
    __asm__ volatile("fldt %2\n fistpll %0\n fnstsw %1\n" : "=m"(i64), "=a"(w) : "m"(t[i].v) : "st"); uint16_t w3 = w; fpinit();
    int16_t t16; int32_t t32; int64_t t64;
    __asm__ volatile("fldt %1\n fisttps %0\n" : "=m"(t16) : "m"(t[i].v) : "st"); fpinit();
    __asm__ volatile("fldt %1\n fisttpl %0\n" : "=m"(t32) : "m"(t[i].v) : "st"); fpinit();
    __asm__ volatile("fldt %1\n fisttpll %0\n" : "=m"(t64) : "m"(t[i].v) : "st"); fpinit();
    printf("%-7s fist16=%04x(sw%02x) fist32=%08x(sw%02x) fist64=%016llx(sw%02x) | fisttp16=%04x fisttp32=%08x fisttp64=%016llx\n", t[i].n, (uint16_t)i16, w1 & 0x3f, (uint32_t)i32, w2 & 0x3f, (unsigned long long)i64, w3 & 0x3f, (uint16_t)t16, (uint32_t)t32, (unsigned long long)t64); }
  uint8_t bcd[10]; // fbld/fbstp
  uint8_t in[10] = {0x78, 0x56, 0x34, 0x12, 0x90, 0x78, 0x56, 0x34, 0x12, 0x00};
  fpinit(); f80 r; __asm__ volatile("fbld %1\n fstpt %0\n" : "=m"(r) : "m"(in) : "st"); printf("fbld 1234567890123456789? (18 digits 123456789012345678): "); p80(r); printf("\n");
  fpinit(); __asm__ volatile("fldt %1\n fbstp %0\n" : "=m"(bcd) : "m"(r) : "st"); dumpw("fbstp back: ", bcd, 10);
  f80 big = F(1e19); fpinit(); __asm__ volatile("fldt %1\n fbstp %0\n fnstsw %%ax\n" : "=m"(bcd) :"m"(big) : "st", "ax"); dumpw("fbstp 1e19 (want ffffc000000000000000 indefinite): ", bcd, 10);
  f80 neg = F(-2.5); fpinit(); __asm__ volatile("fldt %1\n fbstp %0\n" : "=m"(bcd) : "m"(neg) : "st"); dumpw("fbstp -2.5 nearest (want 8000000000000000 0002): ", bcd, 10);
  uint8_t badb[10] = {0xAB, 0xCD, 0, 0, 0, 0, 0, 0, 0, 0}; fpinit(); __asm__ volatile("fbld %1\n fstpt %0\n" : "=m"(r) : "m"(badb) : "st"); printf("fbld invalid digits AB CD: "); p80(r); printf(" (undefined on HW)\n");
}
// ---- fprem/fprem1/fscale/fxtract
static void sec_prem(void) {
  struct { const char* n; double a, b; } t[] = {{"10 mod 3", 10, 3}, {"11 mod 4", 11, 4}, {"-11 mod 4", -11, 4}, {"5.5 mod 2", 5.5, 2}, {"7 mod 8", 7, 8}};
  for (int k = 0; k < 2; k++) for (unsigned i = 0; i < sizeof t / sizeof *t; i++) { fpinit(); f80 r; uint16_t w;
    if (k == 0) __asm__ volatile("fldl %3\n fldl %2\n fprem\n fnstsw %1\n fstpt %0\n fstp %%st(0)\n" : "=m"(r), "=a"(w) : "m"(t[i].a), "m"(t[i].b) : "st", "st(1)");
    else __asm__ volatile("fldl %3\n fldl %2\n fprem1\n fnstsw %1\n fstpt %0\n fstp %%st(0)\n" : "=m"(r), "=a"(w) : "m"(t[i].a), "m"(t[i].b) : "st", "st(1)");
    printf("%s %-10s r=", k ? "fprem1" : "fprem ", t[i].n); p80(r); printf(" C2=%d Q=%d%d%d (Q2=C0 Q1=C3 Q0=C1)\n", !!(w & 0x400), !!(w & 0x100), !!(w & 0x4000), !!(w & 0x200)); }
  for (int k = 0; k < 2; k++) { fpinit(); f80 a = POW2(70), b = F(3), r; uint16_t w; int loops = 0, firstc2 = -1;
    __asm__ volatile("fldt %0\n fldt %1\n" :: "m"(b), "m"(a) : "st"); // st0=a st1=b
    do { if (k == 0) __asm__ volatile("fprem\n fnstsw %0\n" : "=a"(w)); else __asm__ volatile("fprem1\n fnstsw %0\n" : "=a"(w)); loops++; if (firstc2 < 0) firstc2 = !!(w & 0x400); } while ((w & 0x400) && loops < 10);
    __asm__ volatile("fstpt %0\n fstp %%st(0)\n" : "=m"(r) :: "st", "st(1)"); printf("%s 2^70 mod 3: loops=%d first C2=%d result=", k ? "fprem1" : "fprem ", loops, firstc2); p80(r); printf(" (want 3fff:8000.. = 1 for fprem; -1 or 1 for fprem1; C2=1 on first pass)\n"); }
  { fpinit(); f80 r; double a = 3, e = 2.7; __asm__ volatile("fldl %2\n fldl %1\n fscale\n fstpt %0\n fstp %%st(0)\n" : "=m"(r) : "m"(a), "m"(e) : "st", "st(1)"); printf("fscale 3*2^trunc(2.7): "); p80(r); printf(" (want 4002:c000.. = 12)\n");
    e = -2.7; __asm__ volatile("fldl %2\n fldl %1\n fscale\n fstpt %0\n fstp %%st(0)\n" : "=m"(r) : "m"(a), "m"(e) : "st", "st(1)"); printf("fscale 3*2^trunc(-2.7): "); p80(r); printf(" (want 3ffe:c000.. = 0.75)\n"); }
  { fpinit(); f80 s, e; double a = 10; __asm__ volatile("fldl %2\n fxtract\n fstpt %0\n fstpt %1\n" : "=m"(s), "=m"(e) : "m"(a) : "st", "st(1)"); printf("fxtract 10: sig="); p80(s); printf(" exp="); p80(e); printf(" (want 3fff:a000.. , 4008:c000..? exp=3.0 => 4000:c000..)\n"); }
}
int main(int argc, char** argv) {
  const char* s = argc > 1 ? argv[1] : "";
  if (!strcmp(s, "round")) sec_round(); else if (!strcmp(s, "rint")) sec_rint(); else if (!strcmp(s, "pcrange")) sec_pcrange();
  else if (!strcmp(s, "denorm")) sec_denorm(); else if (!strcmp(s, "exc")) sec_exc(); else if (!strcmp(s, "sigfpe")) sec_sigfpe();
  else if (!strcmp(s, "stack")) sec_stack(); else if (!strcmp(s, "env")) sec_env(); else if (!strcmp(s, "fxam")) sec_fxam();
  else if (!strcmp(s, "cmp")) sec_cmp(); else if (!strcmp(s, "fist")) sec_fist(); else if (!strcmp(s, "prem")) sec_prem();
  else { fprintf(stderr, "sections: round rint pcrange denorm exc sigfpe stack env fxam cmp fist prem\n"); return 2; }
  return 0;
}
