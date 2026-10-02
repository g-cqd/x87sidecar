/* Address-space probe: what a translated x86_64 process can map, especially in the low 4 GB that a 32-bit Wine guest needs. */
#include "gaps.h"
#include <sys/mman.h>
#include <sys/resource.h>
#include <mach/mach.h>
#include <pthread.h>

static int try_fixed(uint64_t a, size_t len, const char *what) {
    void *p = mmap((void *)a, len, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
    int e = errno;
    if (p == MAP_FAILED) { printf("  MAP_FIXED %-28s @%#llx len=%#zx -> FAIL errno=%d (%s)\n", what, (unsigned long long)a, len, e, strerror(e)); return 0; }
    *(volatile char *)p = 1; munmap(p, len);
    printf("  MAP_FIXED %-28s @%#llx len=%#zx -> ok\n", what, (unsigned long long)a, len); return 1;
}
static void *stk(void *a) { char buf[1]; printf("  secondary thread stack addr %p\n", buf); return 0; }
int main(int argc, char **argv) {
    gaps_install();
    printf("== page sizes\n  getpagesize=%d sysconf=%ld vm_page_size=%lu\n", getpagesize(), sysconf(_SC_PAGESIZE), (unsigned long)vm_page_size);
    { char *p = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0); int r = mprotect(p + 4096, 4096, PROT_NONE);
      printf("  mprotect of one 4 KiB page inside a 16 KiB mapping -> %s; ", r ? "FAIL" : "ok");
      int sig = PROBE_Q("touch protected", { *(volatile char *)(p + 4096) = 1; }); printf("touching it -> %s; neighbours %s\n", sig ? signame(sig) : "no fault", (p[0] = 1, p[8192] = 1, "writable")); 
      r = mprotect(p + 4096, 4096, PROT_READ | PROT_WRITE); (void)r; }
    printf("== low 4 GB (a 32-bit guest's whole world); __PAGEZERO is whatever this binary was linked with\n");
    struct rlimit rl; getrlimit(RLIMIT_AS, &rl); printf("  RLIMIT_AS cur=%llx max=%llx\n", (unsigned long long)rl.rlim_cur, (unsigned long long)rl.rlim_max);
    uint64_t addrs[] = {0x1000, 0x10000, 0x400000, 0x10000000, 0x40000000, 0x7ff00000, 0x80000000ull, 0xc0000000ull, 0xfff00000ull, 0xffff0000ull};
    for (unsigned i = 0; i < sizeof addrs / sizeof *addrs; i++) try_fixed(addrs[i], 0x10000, "64K in low 4GB");
    printf("== hints without MAP_FIXED\n");
    for (uint64_t h = 0x10000; h < 0x100000000ull; h <<= 4) { void *p = mmap((void *)h, 0x10000, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0); printf("  hint %#llx -> %p%s\n", (unsigned long long)h, p, ((uint64_t)p < 0x100000000ull) ? "  (low)" : "  (HIGH: hint ignored)"); if (p != MAP_FAILED) munmap(p, 0x10000); }
    printf("== how much low memory can be reserved (PROT_NONE, 64 MiB chunks, MAP_FIXED walk from 0x10000 to 4GB)\n");
    { size_t ok = 0, tried = 0; for (uint64_t a = 0x10000000; a < 0x100000000ull; a += 0x4000000) { tried++; void *p = mmap((void *)a, 0x4000000, PROT_NONE, MAP_ANON | MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE, -1, 0); if (p != MAP_FAILED) { ok++; munmap(p, 0x4000000); } }
      printf("  %zu of %zu 64 MiB slots in [256 MiB,4 GiB) mappable with MAP_FIXED\n", ok, tried); }
    printf("== high / total address space\n");
    { void *p = mmap(NULL, 1ull << 36, PROT_NONE, MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0); printf("  64 GiB PROT_NONE NORESERVE: %p\n", p); if (p != MAP_FAILED) munmap(p, 1ull << 36);
      p = mmap(NULL, 1ull << 40, PROT_NONE, MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0); printf("  1 TiB PROT_NONE NORESERVE: %p\n", p); if (p != MAP_FAILED) munmap(p, 1ull << 40);
      void *hi = mmap((void *)0x7ffff0000000ull, 0x10000, PROT_READ, MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0); printf("  fixed @0x7ffff0000000: %p\n", hi);
      hi = mmap((void *)0x800000000000ull, 0x10000, PROT_READ, MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0); printf("  fixed @0x800000000000 (above 47-bit): %p errno=%d\n", hi, hi == MAP_FAILED ? errno : 0); }
    { int x; void *m = malloc(1); printf("  stack var %p, heap %p, main text %p\n", (void *)&x, m, (void *)main); struct rlimit s; getrlimit(RLIMIT_STACK, &s); printf("  RLIMIT_STACK cur=%llu MiB\n", (unsigned long long)s.rlim_cur >> 20); }
    { pthread_t t; pthread_create(&t, NULL, stk, NULL); pthread_join(t, NULL); }
    printf("== executable memory (JIT-like): MAP_JIT and plain RWX\n");
    { void *p = mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0); printf("  RWX anon mmap: %s\n", p == MAP_FAILED ? strerror(errno) : "ok");
      if (p != MAP_FAILED) { ((unsigned char *)p)[0] = 0xc3; int sig = PROBE_Q("rwx call", { ((void (*)(void))p)(); }); printf("  write ret(0xc3) then call RWX page: %s\n", sig ? signame(sig) : "ok"); } }
    { void *p = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0); ((unsigned char *)p)[0] = 0xc3; int r = mprotect(p, 16384, PROT_READ | PROT_EXEC); printf("  RW->RX mprotect: %s; ", r ? strerror(errno) : "ok");
      int sig = PROBE_Q("rx call", { ((void (*)(void))p)(); }); printf("call: %s\n", sig ? signame(sig) : "ok"); }
    return 0;
}
