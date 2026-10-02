// EFLAGS sweep of stock Rosetta against a software model. Build: clang -arch x86_64 -O1 -mno-red-zone
#include "common.h"
static volatile uint64_t gbuf __attribute__((aligned(16)));
typedef unsigned __int128 u128; typedef __int128 i128;
enum { CF = 1, PF = 4, AF = 0x10, ZF = 0x40, SF = 0x80, OF = 0x800, ALLF = 0x8d5 };
enum { ADD, SUB, ADC, SBB, CMP, AND, OR, XOR, TEST, NEG, INC, DEC, SHL, SHR, SAR, ROL, ROR, MUL, IMUL1, IMUL2, BSF, BSR, POPCNT, BT, CMPXCHG, XADD, BTS, BTR, BTC, NOPS };
static const char* NAME[] = {"add", "sub", "adc", "sbb", "cmp", "and", "or", "xor", "test", "neg", "inc", "dec", "shl", "shr", "sar", "rol", "ror", "mul", "imul1", "imul2", "bsf", "bsr", "popcnt", "bt", "cmpxchg", "xadd", "bts", "btr", "btc"};
typedef struct { uint64_t res, res2, acc; uint32_t fl; } Out;
typedef struct { uint64_t res, res2, acc; uint32_t fl, mask; int cr, cr2, cacc; } Exp;
static uint64_t M(int n) { return n == 64 ? ~0ull : ((1ull << n) - 1); }
static uint32_t szp(uint64_t r, int n) { uint32_t f = 0; if (!(r & M(n))) f |= ZF; if ((r >> (n - 1)) & 1) f |= SF; if (!(__builtin_popcount((unsigned)(r & 0xff)) & 1)) f |= PF; return f; }
static int64_t sx(uint64_t v, int n) { return n == 64 ? (int64_t)v : (int64_t)(v << (64 - n)) >> (64 - n); }

// ---- hardware execution
#define A2(INS, MOD, T) { T x = (T)a, y = (T)b; __asm__ volatile("pushq %[fi]\n popfq\n " INS " %" MOD "[y], %" MOD "[x]\n pushfq\n popq %[fo]" : [x] "+r"(x), [fo] "=r"(fo64) : [y] "r"(y), [fi] "r"(fi64) : "cc", "memory"); o.res = x; }
#define A1(INS, MOD, T) { T x = (T)a; __asm__ volatile("pushq %[fi]\n popfq\n " INS " %" MOD "[x]\n pushfq\n popq %[fo]" : [x] "+r"(x), [fo] "=r"(fo64) : [fi] "r"(fi64) : "cc", "memory"); o.res = x; }
#define AS(INS, MOD, T) { T x = (T)a; __asm__ volatile("pushq %[fi]\n popfq\n " INS " %%cl, %" MOD "[x]\n pushfq\n popq %[fo]" : [x] "+r"(x), [fo] "=r"(fo64) : [fi] "r"(fi64), "c"((uint8_t)cnt) : "cc", "memory"); o.res = x; }
#define AMUL(INS, MOD, T) { T x = (T)a, y = (T)b, hi = 0; __asm__ volatile("pushq %[fi]\n popfq\n " INS " %" MOD "[y]\n pushfq\n popq %[fo]" : [x] "+a"(x), [h] "=&d"(hi), [fo] "=r"(fo64) : [y] "r"(y), [fi] "r"(fi64) : "cc", "memory"); o.res = x; o.res2 = hi; }
#define ACX(INS, MOD, T) { T x = (T)a, y = (T)b, ac = (T)c; __asm__ volatile("pushq %[fi]\n popfq\n " INS " %" MOD "[y], %" MOD "[x]\n pushfq\n popq %[fo]" : [x] "+r"(x), [ac] "+a"(ac), [fo] "=r"(fo64) : [y] "r"(y), [fi] "r"(fi64) : "cc", "memory"); o.res = x; o.acc = ac; }
#define AXADD(INS, MOD, T) { T x = (T)a, y = (T)b; __asm__ volatile("pushq %[fi]\n popfq\n " INS " %" MOD "[y], %" MOD "[x]\n pushfq\n popq %[fo]" : [x] "+r"(x), [y] "+r"(y), [fo] "=r"(fo64) : [fi] "r"(fi64) : "cc", "memory"); o.res = x; o.res2 = y; }
#define SZ4(MAC, INS) switch (n) { case 8: MAC(INS, "b", uint8_t) break; case 16: MAC(INS, "w", uint16_t) break; case 32: MAC(INS, "k", uint32_t) break; default: MAC(INS, "q", uint64_t) break; }
#define SZ3(MAC, INS) switch (n) { case 16: MAC(INS, "w", uint16_t) break; case 32: MAC(INS, "k", uint32_t) break; default: MAC(INS, "q", uint64_t) break; }

#define MEM2(PFX, INS, MOD, T) __asm__ volatile("pushq %[fi]\n popfq\n " PFX INS " %" MOD "[y], %[m]\n pushfq\n popq %[fo]" : [m] "+m"(*(T*)&gbuf), [fo] "=r"(fo64) : [y] "r"(y), [fi] "r"(fi64) : "cc", "memory")
#define MEM1(PFX, INS, MOD, T, SFX) __asm__ volatile("pushq %[fi]\n popfq\n " PFX INS SFX " %[m]\n pushfq\n popq %[fo]" : [m] "+m"(*(T*)&gbuf), [fo] "=r"(fo64) : [fi] "r"(fi64) : "cc", "memory")
#define MEMX(PFX, INS, MOD, T) __asm__ volatile("pushq %[fi]\n popfq\n " PFX INS " %" MOD "[y], %[m]\n pushfq\n popq %[fo]" : [m] "+m"(*(T*)&gbuf), [y] "+r"(y), [fo] "=r"(fo64) : [fi] "r"(fi64) : "cc", "memory")
#define MEMC(PFX, INS, MOD, T) __asm__ volatile("pushq %[fi]\n popfq\n " PFX INS " %" MOD "[y], %[m]\n pushfq\n popq %[fo]" : [m] "+m"(*(T*)&gbuf), [ac] "+a"(ac), [fo] "=r"(fo64) : [y] "r"(y), [fi] "r"(fi64) : "cc", "memory")
#define M2(INS, MOD, T) { T y = (T)b; *(T*)&gbuf = (T)a; if (var == 2) MEM2("lock ", INS, MOD, T); else MEM2("", INS, MOD, T); o.res = *(T*)&gbuf; }
#define M1(INS, MOD, T) { *(T*)&gbuf = (T)a; if (var == 2) MEM1("lock ", INS, MOD, T, SFXS); else MEM1("", INS, MOD, T, SFXS); o.res = *(T*)&gbuf; }
#define MX(INS, MOD, T) { T y = (T)b; *(T*)&gbuf = (T)a; if (var == 2) MEMX("lock ", INS, MOD, T); else MEMX("", INS, MOD, T); o.res = *(T*)&gbuf; o.res2 = y; }
#define MC(INS, MOD, T) { T y = (T)b, ac = (T)c; *(T*)&gbuf = (T)a; if (var == 2) MEMC("lock ", INS, MOD, T); else MEMC("", INS, MOD, T); o.res = *(T*)&gbuf; o.acc = ac; }
#define SZ4M(MAC, INS) switch (n) { case 8: MAC(INS, "b", uint8_t) break; case 16: MAC(INS, "w", uint16_t) break; case 32: MAC(INS, "k", uint32_t) break; default: MAC(INS, "q", uint64_t) break; }
#define SZ3M(MAC, INS) switch (n) { case 16: MAC(INS, "w", uint16_t) break; case 32: MAC(INS, "k", uint32_t) break; default: MAC(INS, "q", uint64_t) break; }
#define M1S(INS) switch (n) { case 8: { enum { SFXS_ = 0 }; M1S8(INS) } break; case 16: M1S16(INS) break; case 32: M1S32(INS) break; default: M1S64(INS) break; }
static Out hw(int op, int n, int var, uint64_t a, uint64_t b, uint64_t c, uint32_t fi, unsigned cnt) {
  Out o = {0, 0, 0, 0}; uint64_t fo64 = 0, fi64 = fi;
  if (var) { if (op >= BTS && op <= BTC) b %= n; switch (op) {
    case ADD: SZ4M(M2, "add") break; case SUB: SZ4M(M2, "sub") break; case ADC: SZ4M(M2, "adc") break; case SBB: SZ4M(M2, "sbb") break;
    case AND: SZ4M(M2, "and") break; case OR: SZ4M(M2, "or") break; case XOR: SZ4M(M2, "xor") break;
#define SFXS "b"
    case NEG: if (n == 8) { *(uint8_t*)&gbuf = a; if (var == 2) MEM1("lock ", "neg", "b", uint8_t, "b"); else MEM1("", "neg", "b", uint8_t, "b"); o.res = *(uint8_t*)&gbuf; } else if (n == 16) { *(uint16_t*)&gbuf = a; if (var == 2) MEM1("lock ", "neg", "w", uint16_t, "w"); else MEM1("", "neg", "w", uint16_t, "w"); o.res = *(uint16_t*)&gbuf; } else if (n == 32) { *(uint32_t*)&gbuf = a; if (var == 2) MEM1("lock ", "neg", "k", uint32_t, "l"); else MEM1("", "neg", "k", uint32_t, "l"); o.res = *(uint32_t*)&gbuf; } else { *(uint64_t*)&gbuf = a; if (var == 2) MEM1("lock ", "neg", "q", uint64_t, "q"); else MEM1("", "neg", "q", uint64_t, "q"); o.res = *(uint64_t*)&gbuf; } break;
    case INC: if (n == 8) { *(uint8_t*)&gbuf = a; if (var == 2) MEM1("lock ", "inc", "b", uint8_t, "b"); else MEM1("", "inc", "b", uint8_t, "b"); o.res = *(uint8_t*)&gbuf; } else if (n == 16) { *(uint16_t*)&gbuf = a; if (var == 2) MEM1("lock ", "inc", "w", uint16_t, "w"); else MEM1("", "inc", "w", uint16_t, "w"); o.res = *(uint16_t*)&gbuf; } else if (n == 32) { *(uint32_t*)&gbuf = a; if (var == 2) MEM1("lock ", "inc", "k", uint32_t, "l"); else MEM1("", "inc", "k", uint32_t, "l"); o.res = *(uint32_t*)&gbuf; } else { *(uint64_t*)&gbuf = a; if (var == 2) MEM1("lock ", "inc", "q", uint64_t, "q"); else MEM1("", "inc", "q", uint64_t, "q"); o.res = *(uint64_t*)&gbuf; } break;
    case DEC: if (n == 8) { *(uint8_t*)&gbuf = a; if (var == 2) MEM1("lock ", "dec", "b", uint8_t, "b"); else MEM1("", "dec", "b", uint8_t, "b"); o.res = *(uint8_t*)&gbuf; } else if (n == 16) { *(uint16_t*)&gbuf = a; if (var == 2) MEM1("lock ", "dec", "w", uint16_t, "w"); else MEM1("", "dec", "w", uint16_t, "w"); o.res = *(uint16_t*)&gbuf; } else if (n == 32) { *(uint32_t*)&gbuf = a; if (var == 2) MEM1("lock ", "dec", "k", uint32_t, "l"); else MEM1("", "dec", "k", uint32_t, "l"); o.res = *(uint32_t*)&gbuf; } else { *(uint64_t*)&gbuf = a; if (var == 2) MEM1("lock ", "dec", "q", uint64_t, "q"); else MEM1("", "dec", "q", uint64_t, "q"); o.res = *(uint64_t*)&gbuf; } break;
    case XADD: SZ4M(MX, "xadd") break; case CMPXCHG: SZ4M(MC, "cmpxchg") break;
    case BTS: SZ3M(M2, "bts") break; case BTR: SZ3M(M2, "btr") break; case BTC: SZ3M(M2, "btc") break;
  } o.fl = (uint32_t)fo64 & ALLF; return o; }
  switch (op) {
    case BTS: SZ3(A2, "bts") break; case BTR: SZ3(A2, "btr") break; case BTC: SZ3(A2, "btc") break;
    case ADD: SZ4(A2, "add") break; case SUB: SZ4(A2, "sub") break; case ADC: SZ4(A2, "adc") break; case SBB: SZ4(A2, "sbb") break; case CMP: SZ4(A2, "cmp") break;
    case AND: SZ4(A2, "and") break; case OR: SZ4(A2, "or") break; case XOR: SZ4(A2, "xor") break; case TEST: SZ4(A2, "test") break;
    case NEG: SZ4(A1, "neg") break; case INC: SZ4(A1, "inc") break; case DEC: SZ4(A1, "dec") break;
    case SHL: SZ4(AS, "shl") break; case SHR: SZ4(AS, "shr") break; case SAR: SZ4(AS, "sar") break; case ROL: SZ4(AS, "rol") break; case ROR: SZ4(AS, "ror") break;
    case MUL: if (n == 8) { uint16_t x = (uint8_t)a; uint8_t y = (uint8_t)b; __asm__ volatile("pushq %[fi]\n popfq\n mulb %[y]\n pushfq\n popq %[fo]" : [x] "+a"(x), [fo] "=r"(fo64) : [y] "r"(y), [fi] "r"(fi64) : "cc", "memory"); o.res = x & 0xff; o.res2 = x >> 8; } else SZ4(AMUL, "mul") break;
    case IMUL1: if (n == 8) { uint16_t x = (uint8_t)a; uint8_t y = (uint8_t)b; __asm__ volatile("pushq %[fi]\n popfq\n imulb %[y]\n pushfq\n popq %[fo]" : [x] "+a"(x), [fo] "=r"(fo64) : [y] "r"(y), [fi] "r"(fi64) : "cc", "memory"); o.res = x & 0xff; o.res2 = x >> 8; } else SZ4(AMUL, "imul") break; case IMUL2: SZ3(A2, "imul") break;
    case BSF: SZ3(A2, "bsf") break; case BSR: SZ3(A2, "bsr") break; case POPCNT: SZ3(A2, "popcnt") break; case BT: SZ3(A2, "bt") break;
    case CMPXCHG: SZ4(ACX, "cmpxchg") break; case XADD: SZ4(AXADD, "xadd") break;
  }
  o.fl = (uint32_t)fo64 & ALLF; return o; }
// ---- model
static uint32_t addflags(uint64_t a, uint64_t b, int cin, int n, uint64_t* r) {
  u128 t = (u128)a + b + cin; uint64_t rr = (uint64_t)t & M(n); uint32_t f = szp(rr, n); if (n == 64 ? (t >> 64) & 1 : (t >> n) & 1) f |= CF;
  if ((a ^ b ^ rr) & 0x10) f |= AF; if ((~(a ^ b) & (a ^ rr)) >> (n - 1) & 1) f |= OF; *r = rr; return f; }
static uint32_t subflags(uint64_t a, uint64_t b, int bin, int n, uint64_t* r) {
  uint64_t rr = (a - b - bin) & M(n); uint32_t f = szp(rr, n); if ((u128)a < (u128)b + bin) f |= CF;
  if ((a ^ b ^ rr) & 0x10) f |= AF; if (((a ^ b) & (a ^ rr)) >> (n - 1) & 1) f |= OF; *r = rr; return f; }
static Exp model(int op, int n, uint64_t a, uint64_t b, uint64_t c, uint32_t fi, unsigned cnt) {
  Exp e = {0}; uint64_t m = M(n); a &= m; b &= m; c &= m; uint32_t f = 0, written = ALLF, undef = 0; e.cr = 1; uint64_t r = 0;
  int cin = fi & CF;
  switch (op) {
    case ADD: f = addflags(a, b, 0, n, &r); break; case ADC: f = addflags(a, b, cin, n, &r); break;
    case SUB: f = subflags(a, b, 0, n, &r); break; case SBB: f = subflags(a, b, cin, n, &r); break;
    case CMP: f = subflags(a, b, 0, n, &r); r = a; break;
    case AND: r = a & b; f = szp(r, n); undef = AF; break; case OR: r = a | b; f = szp(r, n); undef = AF; break; case XOR: r = a ^ b; f = szp(r, n); undef = AF; break;
    case TEST: r = a; f = szp(a & b, n); undef = AF; break;
    case NEG: f = subflags(0, a, 0, n, &r); break;
    case INC: f = addflags(a, 1, 0, n, &r); written = ALLF & ~CF; f &= ~CF; break;
    case DEC: f = subflags(a, 1, 0, n, &r); written = ALLF & ~CF; f &= ~CF; break;
    case SHL: case SHR: case SAR: case ROL: case ROR: {
      unsigned c5 = cnt & (n == 64 ? 63 : 31); r = a;
      if (c5 == 0) { written = 0; break; }
      if (op == SHL) { r = c5 >= 64 ? 0 : (a << c5) & m; if (c5 <= (unsigned)n) { if ((a >> (n - c5)) & 1) f |= CF; } else undef |= CF; f |= szp(r, n); if (c5 == 1) { if (((r >> (n - 1)) & 1) ^ (f & CF)) f |= OF; } else undef |= OF; undef |= AF; }
      else if (op == SHR) { r = c5 >= 64 ? 0 : a >> c5; if (c5 <= (unsigned)n) { if ((a >> (c5 - 1)) & 1) f |= CF; } else undef |= CF; f |= szp(r, n); if (c5 == 1) { if ((a >> (n - 1)) & 1) f |= OF; } else undef |= OF; undef |= AF; }
      else if (op == SAR) { int64_t sa = sx(a, n); unsigned s = c5 > 63 ? 63 : c5; r = (uint64_t)(sa >> s) & m; if ((sa >> (c5 - 1 > 63 ? 63 : c5 - 1)) & 1) f |= CF; f |= szp(r, n); undef |= AF; if (c5 != 1) undef |= OF; }
      else { unsigned rot = c5 % n; written = CF | OF;
        if (op == ROL) { r = rot ? ((a << rot) | (a >> (n - rot))) & m : a; if (r & 1) f |= CF; if (c5 == 1) { if (((r >> (n - 1)) & 1) ^ (r & 1)) f |= OF; } else undef |= OF; }
        else { r = rot ? ((a >> rot) | (a << (n - rot))) & m : a; if ((r >> (n - 1)) & 1) f |= CF; if (c5 == 1) { if (((r >> (n - 1)) ^ (r >> (n - 2))) & 1) f |= OF; } else undef |= OF; } }
      break; }
    case MUL: { u128 t = (u128)a * b; r = (uint64_t)t & m; e.res2 = (uint64_t)(t >> n) & m; e.cr2 = 1; if (e.res2) f |= CF | OF; undef = SF | ZF | AF | PF; break; }
    case IMUL1: case IMUL2: { i128 t = (i128)sx(a, n) * sx(b, n); r = (uint64_t)t & m; if (op == IMUL1) { e.res2 = (uint64_t)((u128)t >> n) & m; e.cr2 = 1; } if (t != (i128)sx(r, n)) f |= CF | OF; undef = SF | ZF | AF | PF; break; }
    case BSF: case BSR: if (b == 0) { f |= ZF; e.cr = 0; } else r = op == BSF ? (uint64_t)__builtin_ctzll(b) : (uint64_t)(63 - __builtin_clzll(b)); undef = ALLF & ~ZF; break;
    case POPCNT: r = (uint64_t)__builtin_popcountll(b); if (!b) f |= ZF; break;
    case BTS: case BTR: case BTC: { uint64_t bit = 1ull << (b % n); r = op == BTS ? (a | bit) : op == BTR ? (a & ~bit) : (a ^ bit); written = CF; if ((a >> (b % n)) & 1) f |= CF; e.mask = CF | ZF; break; }
    case BT: r = a; written = CF; if ((a >> (b % n)) & 1) f |= CF; undef = 0; e.mask = 0; break;
    case CMPXCHG: {
#ifdef CMPXCHG_REVERSED
      f = subflags(a, c, 0, n, &r);
#else
      f = subflags(c, a, 0, n, &r);
#endif
      if (c == a) { e.res = b; e.acc = c; } else { e.res = a; e.acc = a; } e.cacc = 1; r = e.res; break; }
    case XADD: f = addflags(a, b, 0, n, &r); e.res2 = a; e.cr2 = 1; break;
  }
  if (op == CMPXCHG) e.res = r; else e.res = r;
  e.fl = (fi & ~written) | (f & written); e.mask = ALLF & ~undef;
  if (op == BT || op == BTS || op == BTR || op == BTC) e.mask = CF | ZF; return e; }
static uint64_t corners[] = {0, 1, 2, 3, 7, 8, 0xf, 0x10, 0x1f, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x7fffffff, 0x80000000ull, 0xffffffffull, 0x7fffffffffffffffull, 0x8000000000000000ull, ~0ull, 0x0f0f0f0f0f0f0f0full};
static uint64_t pick(void) { uint64_t r = rng(); switch (r & 3) { case 0: return corners[(r >> 8) % (sizeof corners / 8)]; case 1: return (r >> 8) >> ((r >> 40) % 64); case 2: return corners[(r >> 8) % (sizeof corners / 8)] + ((r >> 20) & 3) - 1; default: return rng(); } }
int main(int argc, char** argv) {
  setvbuf(stdout, 0, _IOLBF, 0);
  int N = argc > 1 ? atoi(argv[1]) : 6000; int tot_bad = 0; long tot = 0;
  const char* fn[] = {"CF", "PF", "AF", "ZF", "SF", "OF"}; uint32_t fb[] = {CF, PF, AF, ZF, SF, OF};
  const char* VN[] = {"reg", "mem", "lock mem"};
  for (int var = 0; var < 3; var++) for (int op = 0; op < NOPS; op++) for (int n = 8; n <= 64; n *= 2) {
    if (var && !(op <= SBB && op != CMP) && !(op == AND || op == OR || op == XOR || op == NEG || op == INC || op == DEC || op == XADD || op == CMPXCHG || (op >= BTS && op <= BTC))) continue;
    if (var && op == CMP) continue;
    if ((op >= BTS && op <= BTC) && n == 8) continue;
    if ((op == IMUL2 || op == BSF || op == BSR || op == POPCNT || op == BT) && n == 8) continue;
    int bad[6] = {0}, badres = 0, badres2 = 0, badacc = 0, cnt_ex[6] = {0}; char ex[6][160] = {{0}}; char exr[160] = "";
    for (int i = 0; i < N; i++) {
      uint64_t a = pick(), b = pick(), c = (i % 3 == 0) ? a : pick(); uint32_t fi = 0x202 | (uint32_t)(rng() & ALLF); unsigned cn = (unsigned)(rng() % 70);
      if (op >= SHL && op <= ROR) { if (i % 5 == 0) cn = 1; else if (i % 5 == 1) cn = (unsigned)n; }
      Out o = hw(op, n, var, a, b, c, fi, cn); Exp e = model(op, n, a, b, c, fi, cn); tot++;
      for (int k = 0; k < 6; k++) if ((e.mask & fb[k]) && ((o.fl ^ e.fl) & fb[k])) { if (!bad[k]++) snprintf(ex[k], sizeof ex[k], "a=%llx b=%llx c=%llx cnt=%u fin=%x got=%x want=%x", (unsigned long long)(a & M(n)), (unsigned long long)(b & M(n)), (unsigned long long)(c & M(n)), cn, fi & ALLF, o.fl, e.fl); }
      if (e.cr && o.res != e.res) { if (!badres++) snprintf(exr, sizeof exr, "a=%llx b=%llx cnt=%u got=%llx want=%llx", (unsigned long long)(a & M(n)), (unsigned long long)(b & M(n)), cn, (unsigned long long)o.res, (unsigned long long)e.res); }
      if (e.cr2 && o.res2 != e.res2) badres2++; if (e.cacc && o.acc != e.acc) badacc++;
    }
    int any = badres || badres2 || badacc; for (int k = 0; k < 6; k++) any |= bad[k] != 0;
    printf("%-8s %2d-bit %-8s N=%d:", NAME[op], n, VN[var], N); for (int k = 0; k < 6; k++) printf(" %s=%d", fn[k], bad[k]); printf(" res=%d res2=%d acc=%d %s\n", badres, badres2, badacc, any ? "<-- MISMATCH" : "ok");
    for (int k = 0; k < 6; k++) if (bad[k]) printf("    first %s mismatch: %s\n", fn[k], ex[k]); if (badres) printf("    first result mismatch: %s\n", exr);
    for (int k = 0; k < 6; k++) tot_bad += bad[k]; tot_bad += badres + badres2 + badacc;
  }
  printf("TOTAL executions=%ld mismatches=%d\n", tot, tot_bad); return 0; }
