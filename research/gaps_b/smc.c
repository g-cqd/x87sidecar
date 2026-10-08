// Self-modifying code / JIT-region behaviour and invalidation costs under stock Rosetta (x86_64).
#include "common.h"
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>
#include <mach/mach_time.h>
static double now_ns(void) { return (double)clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW); }
typedef int (*fn_t)(void);
static int cmpd(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return x < y ? -1 : x > y; }
static void stats(const char* what, double* v, int n, const char* unit) { qsort(v, n, sizeof *v, cmpd); printf("%-52s median=%9.1f min=%9.1f max=%9.1f %s (n=%d)\n", what, v[n / 2], v[0], v[n - 1], unit, n); }
static uint8_t* jit_page(int flags_extra, int prot) { uint8_t* p = mmap(0, 16384, prot, MAP_PRIVATE | MAP_ANON | flags_extra, -1, 0); return p == MAP_FAILED ? 0 : p; }
static void set_mov_ret(uint8_t* p, uint32_t imm) { p[0] = 0xB8; memcpy(p + 1, &imm, 4); p[5] = 0xC3; }
int main(int argc, char** argv) {
  setvbuf(stdout, 0, _IONBF, 0);
  // ---- 1. can we get RWX / MAP_JIT memory in an x86 process?
  uint8_t* p1 = jit_page(0, PROT_READ | PROT_WRITE | PROT_EXEC); printf("mmap RWX plain: %s\n", p1 ? "ok" : "FAILED");
  uint8_t* p2 = jit_page(MAP_JIT, PROT_READ | PROT_WRITE | PROT_EXEC); printf("mmap RWX MAP_JIT: %s\n", p2 ? "ok" : "FAILED");
  void (*wp)(int) = dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_np"); printf("pthread_jit_write_protect_np in x86 process: %s\n", wp ? "present" : "absent");
  uint8_t* P = p2 ? p2 : p1; if (!P) return 1;
  // ---- 2. rewrite + call correctness, in place (RWX)
  int stale = 0; for (int i = 1; i <= 2000; i++) { set_mov_ret(P, (uint32_t)i); int r = ((fn_t)P)(); if (r != i) stale++; }
  printf("in-place rewrite of imm32 then call, 2000 iterations: stale results=%d\n", stale);
  // same-instruction-stream patching: byte store immediately before executing the patched instruction in the same function
  { // f: movb $7, patch+1(%rip); patch: mov $1,%eax; ret   (store reaches the very next instruction)
    uint8_t code[] = {0xC6, 0x05, 0x01, 0x00, 0x00, 0x00, 0x07, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3}; memcpy(P + 256, code, sizeof code);
    int r1 = ((fn_t)(P + 256))(); int r2 = ((fn_t)(P + 256))(); printf("SMC patching the very next instruction: first call=%d second call=%d (x86 HW: 7 and 7)\n", r1, r2); }
  { // store into instruction 2 slots ahead
    uint8_t code[] = {0xC6, 0x05, 0x02, 0x00, 0x00, 0x00, 0x09, 0x90, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3}; memcpy(P + 512, code, sizeof code);
    int r1 = ((fn_t)(P + 512))(); printf("SMC patching a later instruction in same cache line: %d (HW: 9)\n", r1); }
  { // patch then loop back: call a function that patches itself each call incrementing imm
    uint8_t code[] = {0xB8, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x05, 0xF9, 0xFF, 0xFF, 0xFF, 0xC3}; // mov $0,%eax ; incl -7(%rip) [-> imm32 at +1 .. rip after incl = +11, target +1 => disp = -10]
    code[7] = 0xF6; memcpy(P + 768, code, sizeof code); int a = ((fn_t)(P + 768))(), b = ((fn_t)(P + 768))(), c = ((fn_t)(P + 768))(); printf("self-incrementing immediate (inc imm32 of own mov): %d %d %d (HW: 0 1 2)\n", a, b, c); }
  // ---- 3. timing: rewrite+call vs call only
  enum { REP = 9, N = 20000 }; double v[REP];
  for (int r = 0; r < REP; r++) { set_mov_ret(P, 1); double t0 = now_ns(); int s = 0; for (int i = 0; i < N; i++) s += ((fn_t)P)(); double t1 = now_ns(); v[r] = (t1 - t0) / N; if (s != N) printf("?");}
  stats("call only (warm, translated)", v, REP, "ns/call");
  for (int r = 0; r < REP; r++) { double t0 = now_ns(); int s = 0; for (int i = 0; i < N; i++) { set_mov_ret(P, (uint32_t)i); s += ((fn_t)P)(); } double t1 = now_ns(); v[r] = (t1 - t0) / N; }
  stats("rewrite imm + call (SMC, RWX)", v, REP, "ns/cycle");
  // ---- 4. mprotect toggle RW -> RX each time
  { uint8_t* Q = jit_page(0, PROT_READ | PROT_WRITE); int fails = 0;
    for (int r = 0; r < REP; r++) { double t0 = now_ns(); for (int i = 0; i < 5000; i++) { mprotect(Q, 16384, PROT_READ | PROT_WRITE); set_mov_ret(Q, (uint32_t)i); if (mprotect(Q, 16384, PROT_READ | PROT_EXEC)) fails++; if (((fn_t)Q)() != i) fails++; } v[r] = (now_ns() - t0) / 5000; }
    stats("mprotect RW; write; mprotect RX; call", v, REP, "ns/cycle"); printf("   correctness failures: %d\n", fails); }
  // ---- 5. data on the same page as hot code (write-fault handling for shared pages)
  { uint8_t* Q = P + 4096; set_mov_ret(Q, 5); volatile uint32_t* data = (volatile uint32_t*)(Q + 2048);
    for (int r = 0; r < REP; r++) { double t0 = now_ns(); for (int i = 0; i < N; i++) { *data = (uint32_t)i; (void)((fn_t)Q)(); } v[r] = (now_ns() - t0) / N; }
    stats("data store on SAME page as executed code + call", v, REP, "ns/iter");
    uint8_t* Z = P + 8192; set_mov_ret(Z, 5); volatile uint32_t* d2 = (volatile uint32_t*)(P + 12288 + 64); // different page
    for (int r = 0; r < REP; r++) { double t0 = now_ns(); for (int i = 0; i < N; i++) { *d2 = (uint32_t)i; (void)((fn_t)Z)(); } v[r] = (now_ns() - t0) / N; }
    stats("data store on DIFFERENT page + call", v, REP, "ns/iter");
    printf("   data value readback ok: %d\n", *data == (uint32_t)(N - 1) && *d2 == (uint32_t)(N - 1)); }
  // ---- 6. cold translation cost: straight-line code of N 'add $1,%eax' (3 bytes) then ret
  { int sizes[] = {1024, 4096, 16384, 65536}; uint8_t* big = mmap(0, 1 << 20, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | (p2 ? MAP_JIT : 0), -1, 0);
    if (big == MAP_FAILED) { printf("mmap 1MB failed\n"); return 1; }
    for (unsigned k = 0; k < 4; k++) { int bytes = sizes[k]; double first[REP], second[REP];
      for (int r = 0; r < REP; r++) { uint8_t* c = big + r * 65600 * 1; // fresh region each rep (not previously translated)
        int n = bytes / 3; for (int i = 0; i < n; i++) { c[3 * i] = 0x83; c[3 * i + 1] = 0xC0; c[3 * i + 2] = 0x01; } c[3 * n] = 0xC3;
        double t0 = now_ns(); ((fn_t)c)(); double t1 = now_ns(); ((fn_t)c)(); double t2 = now_ns(); first[r] = (t1 - t0) / 1000.0; second[r] = (t2 - t1) / 1000.0; }
      char b1[80], b2[80]; snprintf(b1, sizeof b1, "cold first execution of %5d-byte straight-line add code", bytes); snprintf(b2, sizeof b2, "  second (warm) execution, same code"); stats(b1, first, REP, "us"); stats(b2, second, REP, "us"); }
    // translation cost per byte of code, rough
  }
  // ---- 7. mixed instruction cold translation (x87-free): mov/add/cmp/jcc/ call-heavy; 64KB of 'mov 8(%rsp),%rax; add %rax,%rcx; xor %edx,%edx; cmp; jne +0' pattern
  { uint8_t* big = mmap(0, 1 << 20, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | (p2 ? MAP_JIT : 0), -1, 0); static const uint8_t pat[] = {0x48, 0x8B, 0x44, 0x24, 0x08, 0x48, 0x01, 0xC1, 0x31, 0xD2, 0x48, 0x39, 0xC1, 0x75, 0x00}; int unit = sizeof pat; double first[REP];
    for (int r = 0; r < REP; r++) { uint8_t* c = big + r * 70000; int n = 65536 / unit; for (int i = 0; i < n; i++) memcpy(c + i * unit, pat, unit); c[n * unit] = 0xC3; double t0 = now_ns(); ((fn_t)c)(); first[r] = (now_ns() - t0) / 1000.0; }
    stats("cold first execution of 64KB mov/add/cmp/jcc code (~13K insns)", first, REP, "us"); }
  return 0; }
