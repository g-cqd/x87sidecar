// dumps stock transcendental results as 80-bit values; checked by check_trans.py
#include "common.h"
static f80 R(int emin, int emax, int allowneg) { uint64_t m = rng() | 0x8000000000000000ull; int e = emin + (int)(rng() % (uint64_t)(emax - emin + 1));
  return mk((uint16_t)((0x3fff + e) | ((allowneg && (rng() & 1)) ? 0x8000 : 0)), m); }
#define U1(name, ins) static void name(f80 a) { f80 r; uint16_t w; \
  __asm__ volatile("fnclex\n fldt %2\n " ins "\n fnstsw %1\n fstpt %0\n" : "=m"(r), "=a"(w) : "m"(a) : "st"); printf("T " #name " "); p80(a); printf(" -> "); p80(r); printf(" sw=%04x\n", w & 0xff00 | (w & 0xff)); fpinit(); }
U1(fsin, "fsin") U1(fcos, "fcos") U1(f2xm1, "f2xm1")
static void fsincos_(f80 a) { f80 s, c; uint16_t w; __asm__ volatile("fnclex\n fldt %3\n fsincos\n fnstsw %2\n fstpt %1\n fstpt %0\n" : "=m"(s), "=m"(c), "=a"(w) : "m"(a) : "st", "st(1)");
  printf("T fsincos "); p80(a); printf(" -> "); p80(c); printf(" "); p80(s); printf(" sw=%04x\n", w); fpinit(); }
static void fptan_(f80 a) { f80 t, one; uint16_t w; __asm__ volatile("fnclex\n fldt %3\n fptan\n fnstsw %2\n fstpt %1\n fstpt %0\n" : "=m"(t), "=m"(one), "=a"(w) : "m"(a) : "st", "st(1)");
  printf("T fptan "); p80(a); printf(" -> "); p80(one); printf(" "); p80(t); printf(" sw=%04x\n", w); fpinit(); }
#define B2(name, ins) static void name(f80 y, f80 x) { f80 r; uint16_t w; \
  __asm__ volatile("fnclex\n fldt %2\n fldt %3\n " ins "\n fnstsw %1\n fstpt %0\n" : "=m"(r), "=a"(w) : "m"(y), "m"(x) : "st", "st(1)"); printf("T " #name " "); p80(y); printf(" "); p80(x); printf(" -> "); p80(r); printf(" sw=%04x\n", w); fpinit(); }
B2(fpatan, "fpatan") B2(fyl2x, "fyl2x") B2(fyl2xp1, "fyl2xp1")
int main(void) {
  int N = 300;
  for (int i = 0; i < N; i++) { f80 a = R(-12, 40, 1); fsin(a); fcos(a); fsincos_(a); fptan_(a); }
  f80 sp[] = {mk(0x3fff, 0x8000000000000000ull), mk(0x4000, 0xc90fdaa22168c235ull), mk(0x4001, 0xc90fdaa22168c235ull), mk(0x4003, 0xc90fdaa22168c235ull), mk(0x4004, 0x8000000000000000ull),
    mk(0x403d, 0x8000000000000000ull), mk(0x403e, 0xffffffffffffffffull), mk(0x403e, 0x8000000000000000ull), mk(0x403f, 0x8000000000000000ull), mk(0x4040, 0x8000000000000000ull), mk(0x7ffe, 0x8000000000000000ull)};
  for (unsigned i = 0; i < sizeof sp / sizeof *sp; i++) { fsin(sp[i]); fcos(sp[i]); fptan_(sp[i]); }
  for (int i = 0; i < N; i++) { f80 a = R(-30, -1, 1); if (rng() & 1) a = mk(f80se(a), f80m(a)); f2xm1(a); }
  for (int i = 0; i < N; i++) { fpatan(R(-10, 10, 1), R(-10, 10, 1)); }
  for (int i = 0; i < N; i++) { f80 x = R(-20, 30, 0); fyl2x(R(-3, 3, 1), x); }
  for (int i = 0; i < N; i++) { f80 x = R(-40, -2, 1); fyl2xp1(R(-3, 3, 1), x); }
  return 0; }
