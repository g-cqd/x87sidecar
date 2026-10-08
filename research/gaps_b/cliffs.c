// Per-call cost of "special" x86 operations and transitions under stock Rosetta (x86_64) -- same file builds natively for arm64 (subset).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/syscall.h>
static double now_ns(void) { return (double)clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW); }
static int cmpd(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return x < y ? -1 : x > y; }
#define REP 7
#define BENCH(name, N, body) do { double v[REP]; for (int r = 0; r < REP + 1; r++) { double t0 = now_ns(); for (long i = 0; i < (N); i++) { body; } double t1 = now_ns(); if (r) v[r - 1] = (t1 - t0) / (N); } \
  qsort(v, REP, sizeof(double), cmpd); printf("%-44s median=%9.2f min=%9.2f max=%9.2f ns/op\n", name, v[REP / 2], v[0], v[REP - 1]); } while (0)
static volatile int hits; static void h(int s) { hits++; }
int main(void) {
  setvbuf(stdout, 0, _IONBF, 0);
  struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = h; sigaction(SIGUSR1, &sa, 0);
  BENCH("signal round trip: raise(SIGUSR1)+handler", 20000, raise(SIGUSR1));
  BENCH("getpid() via libc", 200000, (void)getpid());
  BENCH("syscall(SYS_getpid) via libc syscall()", 200000, (void)syscall(SYS_getpid));
#if defined(__x86_64__)
  uint32_t a, b, c, d; uint64_t x; uint16_t w;
  BENCH("empty loop body (baseline)", 2000000, __asm__ volatile("" ::: "memory"));
  BENCH("cpuid leaf 0", 200000, __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0)));
  BENCH("cpuid leaf 1", 200000, __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0)));
  BENCH("rdtsc", 200000, __asm__ volatile("rdtsc" : "=a"(a), "=d"(d)));
  BENCH("rdtscp", 200000, __asm__ volatile("rdtscp" : "=a"(a), "=d"(d), "=c"(c)));
  BENCH("fnstcw m16", 1000000, __asm__ volatile("fnstcw %0" : "=m"(w)));
  { uint16_t cw = 0x37f; BENCH("fldcw m16 (same value)", 1000000, __asm__ volatile("fldcw %0" ::"m"(cw))); }
  { uint16_t c1 = 0x37f, c2 = 0x27f; BENCH("fldcw alternating 0x37f/0x27f (+fnstcw)", 500000, { __asm__ volatile("fldcw %0" ::"m"(c1)); __asm__ volatile("fldcw %0" ::"m"(c2)); }); __asm__ volatile("fldcw %0" ::"m"(c1)); }
  BENCH("fnstsw %ax", 1000000, __asm__ volatile("fnstsw %%ax" : "=a"(w)));
  BENCH("fwait", 1000000, __asm__ volatile("fwait"));
  BENCH("fninit", 200000, __asm__ volatile("fninit"));
  BENCH("fnclex", 1000000, __asm__ volatile("fnclex"));
  BENCH("pushfq; popq", 1000000, __asm__ volatile("pushfq\n popq %0" : "=r"(x) :: "cc"));
  BENCH("pushq; popfq (IF=1)", 1000000, __asm__ volatile("pushq $0x202\n popfq" ::: "cc"));
  BENCH("lahf", 1000000, __asm__ volatile("lahf" : "=a"(w) :: "cc"));
  BENCH("sahf", 1000000, __asm__ volatile("sahf" ::: "cc", "rax"));
  { uint32_t m; BENCH("stmxcsr", 1000000, __asm__ volatile("stmxcsr %0" : "=m"(m))); uint32_t m1 = 0x1f80; BENCH("ldmxcsr (same)", 500000, __asm__ volatile("ldmxcsr %0" ::"m"(m1))); uint32_t m2 = 0x9fc0; BENCH("ldmxcsr alternating (FTZ/DAZ toggle)", 250000, { __asm__ volatile("ldmxcsr %0" ::"m"(m2)); __asm__ volatile("ldmxcsr %0" ::"m"(m1)); }); }
  BENCH("mov %ds,%ax", 1000000, __asm__ volatile("mov %%ds, %0" : "=r"(w)));
  BENCH("mov %ax,%es (null selector)", 500000, __asm__ volatile("xorl %%eax,%%eax\n mov %%ax, %%es" ::: "rax"));
  BENCH("mov %gs:0,%rax (TLS self)", 2000000, __asm__ volatile("movq %%gs:0, %0" : "=r"(x)));
  BENCH("mov %gs:0x10,%rax", 2000000, __asm__ volatile("movq %%gs:0x10, %0" : "=r"(x)));
  { uint64_t nsec; BENCH("syscall insn: getpid (0x2000014)", 200000, __asm__ volatile("movl $0x2000014, %%eax\n syscall" : "=a"(nsec) :: "rcx", "r11", "rdx", "cc", "memory")); }
  BENCH("xgetbv", 500000, __asm__ volatile("xgetbv" : "=a"(a), "=d"(d) : "c"(0)));
  BENCH("lock xadd (uncontended)", 1000000, { static volatile uint32_t v; __asm__ volatile("lock xaddl %0, %1" : "+r"(a), "+m"(v) :: "memory"); });
  BENCH("mfence", 1000000, __asm__ volatile("mfence" ::: "memory"));
  BENCH("pause", 1000000, __asm__ volatile("pause"));
#endif
  return hits < 0; }
