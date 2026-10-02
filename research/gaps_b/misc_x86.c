// partial registers, cmov, string ops/DF, lahf/sahf, pushf/popf bits: stock Rosetta, 64-bit mode. Build with -mno-red-zone.
#include "common.h"
static int bad = 0;
#define CHK(name, got, want) do { uint64_t g_ = (got), w_ = (want); if (g_ != w_) { bad++; printf("DIFF %-44s got=%016llx want=%016llx\n", name, (unsigned long long)g_, (unsigned long long)w_); } else printf("ok   %-44s %016llx\n", name, (unsigned long long)g_); } while (0)
static void regs(void) {
  uint64_t r;
  r = 0x1122334455667788ull; __asm__("movb $0xAA, %%ah" : "+a"(r)); CHK("mov $imm,%ah keeps others", r, 0x112233445566AA88ull);
  r = 0x1122334455667788ull; __asm__("movb $0xBB, %%al" : "+a"(r)); CHK("mov $imm,%al", r, 0x11223344556677BBull);
  r = 0x1122334455667788ull; __asm__("movw $0xCCDD, %%ax" : "+a"(r)); CHK("mov $imm,%ax keeps upper 48", r, 0x112233445566CCDDull);
  r = 0x1122334455667788ull; __asm__("movl $0x99, %%eax" : "+a"(r)); CHK("mov $imm,%eax zero-extends", r, 0x99);
  r = 0x1122334455667788ull; __asm__(".byte 0x87,0xc0" : "+a"(r)); CHK("xchg eax,eax (87 c0) zero-extends", r, 0x55667788ull);
  r = 0x1122334455667788ull; __asm__("nop" : "+a"(r)); CHK("nop (90) leaves rax", r, 0x1122334455667788ull);
  { uint64_t a = 0x1122334455667788ull, b = 0xFFFFFFFFFFFFFF55ull; __asm__("movb %%ah, %%bl" : "+b"(b) : "a"(a)); CHK("mov %ah,%bl", b, 0xFFFFFFFFFFFFFF77ull); }
  { uint64_t a = 0x1122334455667788ull, b = 0xFFFFFFFFFFFFFFFFull; __asm__("movzbl %%ah, %%ebx" : "+b"(b) : "a"(a)); CHK("movzbl %ah,%ebx", b, 0x77); }
  { uint64_t a = 0x11223344556677F8ull, b = ~0ull; __asm__("movsbq %%al, %%rbx" : "+b"(b) : "a"(a)); CHK("movsbq %al,%rbx", b, 0xFFFFFFFFFFFFFFF8ull); }
  { uint64_t a = 0x80000000ull, b = 0; __asm__("movslq %%eax, %%rbx" : "+b"(b) : "a"(a)); CHK("movslq", b, 0xFFFFFFFF80000000ull); }
  r = 0x1122334455667788ull; __asm__("incb %%ah" : "+a"(r) :: "cc"); CHK("inc %ah", r, 0x1122334455667888ull);
  { uint64_t a = 0x00000000000000F0ull, b = 0x0000000000000000ull; __asm__("addb %%bl, %%ah\n" : "+a"(a) : "b"(b)); CHK("add %bl,%ah", a, 0xF0); }
  { uint64_t c = ~0ull, a = 1; __asm__("cmpl $1, %%eax\n cmovne %%eax, %%ecx" : "+c"(c) : "a"(a) : "cc"); CHK("cmovne(not taken,32) zero-extends rcx", c, 0xFFFFFFFFull); }
  { uint64_t c = ~0ull, a = 2; __asm__("cmpl $1, %%eax\n cmovne %%eax, %%ecx" : "+c"(c) : "a"(a) : "cc"); CHK("cmovne(taken,32)", c, 2); }
  { uint64_t c = ~0ull, a = 1; __asm__("cmpl $1, %%eax\n cmovnew %%ax, %%cx" : "+c"(c) : "a"(a) : "cc"); CHK("cmovne(not taken,16) keeps upper", c, ~0ull); }
  { uint64_t c = ~0ull; __asm__("xorl %%eax,%%eax\n sete %%cl" : "+c"(c) :: "rax", "cc"); CHK("sete %cl keeps upper", c, 0xFFFFFFFFFFFFFF01ull); }
  { uint64_t c = ~0ull; __asm__("movl %%ecx, %%ecx" : "+c"(c)); CHK("mov %ecx,%ecx zero-extends", c, 0xFFFFFFFFull); }
  { uint64_t a = 0x1122334455667788ull; __asm__("bswap %%eax" : "+a"(a)); CHK("bswap eax zero-extends", a, 0x88776655ull); }
  { uint64_t a = 0xFFFFFFFF00000001ull; __asm__("addl $1, %%eax" : "+a"(a) :: "cc"); CHK("add32 clears upper", a, 2); }
  { uint64_t a = 0x100000001ull, b = 0x7FFFFFFFFull; uint64_t o; __asm__("leal (%%rax,%%rbx), %%ecx" : "=c"(o) : "a"(a), "b"(b)); CHK("lea 32-bit dest truncates", o, (a + b) & 0xffffffffull); }
  { uint64_t a = 0x5, o; __asm__(".byte 0x67\n leaq 4(%%eax,%%eax,2), %%rcx" : "=c"(o) : "a"(a)); CHK("addr-size 0x67 lea (eax+eax*2+4)", o, 19); }
  { uint32_t o; __asm__("movl $0x12345678, %%eax\n movw $0x4321, %%ax\n bswap %%eax\n movl %%eax, %0" : "=r"(o) :: "rax"); CHK("16-bit write then bswap", o, 0x21433412); }
}
static void strings(void) {
  uint8_t buf[64], ref[64];
  // overlapping forward rep movsb: dst=src+1 must replicate buf[0]
  for (int i = 0; i < 64; i++) buf[i] = (uint8_t)('A' + i % 26); memcpy(ref, buf, 64); for (int i = 0; i < 16; i++) ref[i + 1] = ref[i];
  { uint8_t* s = buf; uint8_t* d = buf + 1; uint64_t c = 16; __asm__ volatile("cld\n rep movsb" : "+S"(s), "+D"(d), "+c"(c) :: "memory"); CHK("rep movsb overlap (dst=src+1) == byte-serial", memcmp(buf, ref, 64) == 0, 1); CHK("  rcx after", c, 0); CHK("  rsi advance", (uint64_t)(s - buf), 16); }
  for (int i = 0; i < 64; i++) buf[i] = (uint8_t)('A' + i % 26); memcpy(ref, buf, 64); for (int i = 0; i < 4; i++) memcpy(ref + 8 * (i + 1), ref + 8 * i, 8);
  { uint8_t* s = buf; uint8_t* d = buf + 8; uint64_t c = 4; __asm__ volatile("cld\n rep movsq" : "+S"(s), "+D"(d), "+c"(c) :: "memory"); CHK("rep movsq overlap (dst=src+8)", memcmp(buf, ref, 64) == 0, 1); }
  for (int i = 0; i < 64; i++) buf[i] = (uint8_t)('A' + i % 26); memcpy(ref, buf, 64); for (int i = 0; i < 6; i++) memcpy(ref + 4 * (i + 1), ref + 4 * i, 4);
  { uint8_t* s = buf; uint8_t* d = buf + 4; uint64_t c = 6; __asm__ volatile("cld\n rep movsl" : "+S"(s), "+D"(d), "+c"(c) :: "memory"); CHK("rep movsd overlap (dst=src+4)", memcmp(buf, ref, 64) == 0, 1); }
  // backward with DF
  for (int i = 0; i < 64; i++) buf[i] = (uint8_t)i; memcpy(ref, buf, 64); for (int i = 9; i >= 0; i--) ref[20 + i] = ref[19 + i];  // copy [19..28] to [20..29] backwards -> memmove semantics
  { uint8_t* s = buf + 28; uint8_t* d = buf + 29; uint64_t c = 10; __asm__ volatile("std\n rep movsb\n cld" : "+S"(s), "+D"(d), "+c"(c) :: "memory"); CHK("std rep movsb (memmove backward)", memcmp(buf, ref, 64) == 0, 1); CHK("  rdi after std", (uint64_t)(d - buf), 19); }
  { uint32_t* d = (uint32_t*)(buf + 32); uint64_t c = 3, a = 0xDEADBEEF; __asm__ volatile("std\n rep stosl\n cld" : "+D"(d), "+c"(c) : "a"(a) : "memory"); CHK("std rep stosd rdi after", (uint64_t)((uint8_t*)d - buf), 32 - 12); CHK("  stored at buf+32,28,24", (*(uint32_t*)(buf + 32) == 0xDEADBEEF) + (*(uint32_t*)(buf + 28) == 0xDEADBEEF) + (*(uint32_t*)(buf + 24) == 0xDEADBEEF), 3); }
  { uint8_t* s = buf; uint8_t* d = buf + 32; uint64_t c = 0; __asm__ volatile("cld\n rep movsb" : "+S"(s), "+D"(d), "+c"(c) :: "memory", "cc"); CHK("rep movsb with rcx=0: rsi,rdi unchanged", (uint64_t)((s - buf) + (d - buf)), 32); }
  { uint8_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8}, b[8] = {1, 2, 3, 9, 5, 6, 7, 8}; const uint8_t* s = a; const uint8_t* d = b; uint64_t c = 8, fl; __asm__ volatile("cld\n repe cmpsb\n pushfq\n popq %0" : "=r"(fl), "+S"(s), "+D"(d), "+c"(c) :: "cc", "memory");
    CHK("repe cmpsb stops at mismatch: rcx", c, 4); CHK("  ZF=0 CF=1(4<9) SF=1", (fl & 0x1c1) , 0x81 | 0x0 ); // 4-9 = -5: CF=1,SF=1,ZF=0
  }
  { const uint8_t h[8] = {1, 2, 3, 4, 5, 6, 7, 8}; const uint8_t* d = h; uint64_t c = 8, a = 5, fl; __asm__ volatile("cld\n repne scasb\n pushfq\n popq %0" : "=r"(fl), "+D"(d), "+c"(c) : "a"(a) : "cc", "memory"); CHK("repne scasb finds 5: rcx", c, 3); CHK("  ZF set", (fl >> 6) & 1, 1); }
  { const uint8_t h[8] = {1, 2, 3, 4, 5, 6, 7, 8}; const uint8_t* d = h; uint64_t c = 0, a = 5, fl; __asm__ volatile("movl $0x40, %%r8d\n pushq %%r8\n popfq\n repne scasb\n pushfq\n popq %0" : "=r"(fl), "+D"(d), "+c"(c) : "a"(a) : "r8", "cc", "memory"); CHK("repne scasb rcx=0 leaves ZF as before (ZF=1)", (fl >> 6) & 1, 1); }
  { uint8_t* d = buf; uint64_t c = 3; uint64_t a = 0x11; __asm__ volatile("cld\n rep stosb" : "+D"(d), "+c"(c) : "a"(a) : "memory"); CHK("rep stosb", (uint64_t)(d - buf), 3); }
}
static void flagsops(void) {
  // lahf/sahf round trip over all AH values
  int badc = 0; for (int v = 0; v < 256; v++) { uint64_t r; __asm__ volatile("movl %1, %%eax\n shll $8, %%eax\n sahf\n lahf\n movzbl %%ah, %%eax\n" : "=a"(r) : "r"((uint32_t)v) : "cc"); uint8_t want = (uint8_t)((v & 0xD5) | 2); if ((uint8_t)r != want) { if (!badc) printf("DIFF sahf/lahf v=%02x got=%02x want=%02x\n", v, (uint8_t)r, want); badc++; } }
  printf("%s sahf;lahf sweep of AH=0..255: %d mismatches\n", badc ? "DIFF" : "ok  ", badc); bad += badc ? 1 : 0;
  // popf/pushf bits
  struct { const char* n; uint64_t in; } t[] = {{"all-ones 0x3ffffd7 (no TF)", 0x3ffffd7ull & ~0x100ull}, {"AC+ID+NT", 0x240000 | 0x200000 | 0x4000 | 2}, {"only IOPL=3", 0x3000 | 2}, {"RF+VM+VIF+VIP", 0x10000 | 0x20000 | 0x80000 | 0x100000 | 2}, {"DF", 0x400 | 2}};
  for (unsigned i = 0; i < sizeof t / sizeof *t; i++) { uint64_t out; __asm__ volatile("pushq %1\n popfq\n pushfq\n popq %0\n pushq $0x202\n popfq" : "=r"(out) : "r"(t[i].in) : "cc", "memory");
    printf("popf(%-30s)=%07llx -> pushf=%07llx\n", t[i].n, (unsigned long long)t[i].in, (unsigned long long)out); }
  printf("  (real x86 ring-3: IOPL/VM/VIF/VIP unchanged (0), RF reads 0, bit1=1; NT/AC/ID/DF/IF writable)\n");
}
int main(int argc, char** argv) { setvbuf(stdout, 0, _IONBF, 0); int m = argc > 1 ? atoi(argv[1]) : 7; if (m & 1) regs(); if (m & 2) strings(); if (m & 4) flagsops(); printf("DIFFS=%d\n", bad); return 0; }
