// Local analysis tool: loads a user-supplied copy of the JIT runtime image into a MAP_JIT
// region of THIS process, applies its chained fixups, and calls its exported state-recovery
// classifier on a raw AArch64 instruction buffer. Contains no Apple code or data.
// usage: classify_harness <libRosettaRuntime-copy> <code.bin> [idx|all]
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <mach-o/loader.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <unistd.h>
static uint8_t* g_base;
static void on_sig(int sig, siginfo_t* si, void* uc) {
    ucontext_t* c = uc;
    printf("TRAP sig=%d pc_off=%#llx lr_off=%#llx (assert/abort inside classifier)\n", sig,
           (unsigned long long)((uint8_t*)c->uc_mcontext->__ss.__pc - g_base),
           (unsigned long long)((uint8_t*)c->uc_mcontext->__ss.__lr - g_base));
    uint64_t* fp = (uint64_t*)c->uc_mcontext->__ss.__fp;
    for (int i = 0; i < 6 && fp; i++) { printf("  frame ret_off=%#llx\n", (unsigned long long)(fp[1] - (uint64_t)g_base)); fp = (uint64_t*)fp[0]; }
    fflush(stdout); _exit(3);
}

typedef struct { uint8_t kind; uint64_t count; uint8_t flag; } Result;
typedef Result (*classify_fn)(const uint32_t*, uint32_t, uint32_t);
#define CLASSIFIER_OFFSET 0x7224  // determine_state_recovery_action in the 27.2 image; relocate via anchors for other builds

static uint8_t* slurp(const char* p, size_t* n) {
    FILE* f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* b = malloc(*n); if (fread(b, 1, *n, f) != *n) exit(1); fclose(f); return b;
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage\n"); return 2; }
    size_t n; uint8_t* img = slurp(argv[1], &n);
    struct mach_header_64* mh = (void*)img; uint8_t* lc = (uint8_t*)(mh + 1);
    uint64_t span = 0; uint64_t chain_off = 0;
    for (uint32_t i = 0; i < mh->ncmds; i++, lc += ((struct load_command*)lc)->cmdsize) {
        if (((struct load_command*)lc)->cmd != LC_SEGMENT_64) continue;
        struct segment_command_64* s = (void*)lc;
        if (!strcmp(s->segname, "__PAGEZERO") || !strcmp(s->segname, "__LINKEDIT")) continue;
        if (s->vmaddr + s->vmsize > span) span = s->vmaddr + s->vmsize;
        struct section_64* sec = (void*)(s + 1);
        for (uint32_t k = 0; k < s->nsects; k++)
            if (!strcmp(sec[k].sectname, "__chain_starts")) chain_off = sec[k].offset;
    }
    uint8_t* base = mmap(NULL, span, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (base == MAP_FAILED) { perror("mmap"); return 1; }
    pthread_jit_write_protect_np(0);
    lc = (uint8_t*)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++, lc += ((struct load_command*)lc)->cmdsize) {
        if (((struct load_command*)lc)->cmd != LC_SEGMENT_64) continue;
        struct segment_command_64* s = (void*)lc;
        if (!strcmp(s->segname, "__PAGEZERO") || !strcmp(s->segname, "__LINKEDIT")) continue;
        memcpy(base + s->vmaddr, img + s->fileoff, s->filesize);
    }
    if (chain_off) {  // DYLD_CHAINED_PTR_64_OFFSET, stride 4
        uint32_t ver = *(uint32_t*)(img + chain_off), cnt = *(uint32_t*)(img + chain_off + 4);
        uint64_t p = *(uint64_t*)(img + chain_off + 8);
        if (ver == 6 && cnt >= 1) for (;;) {
            uint64_t v = *(uint64_t*)(base + p);
            *(uint64_t*)(base + p) = (uint64_t)base + (v & 0xfffffffffULL);
            uint64_t nx = (v >> 51) & 0xfff; if (!nx) break; p += nx * 4;
        }
    }
    sys_icache_invalidate(base, span);
    pthread_jit_write_protect_np(1);
    g_base = base;
    struct sigaction sa = {0}; sa.sa_sigaction = on_sig; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGBUS, &sa, 0); sigaction(SIGSEGV, &sa, 0); sigaction(SIGTRAP, &sa, 0); sigaction(SIGILL, &sa, 0); sigaction(SIGABRT, &sa, 0);
    classify_fn fn = (classify_fn)(base + CLASSIFIER_OFFSET);

    size_t cn; uint32_t* code = (uint32_t*)slurp(argv[2], &cn); uint32_t nw = cn / 4;
    uint32_t* buf = malloc(cn + 64); memcpy(buf, code, cn);
    int all = argc > 3 && !strcmp(argv[3], "all");
    uint32_t lo = all ? 0 : (argc > 3 ? atoi(argv[3]) : 0), hi = all ? nw : lo + 1;
    for (uint32_t idx = lo; idx < hi; idx++) {
        Result r = fn(buf, nw, idx);
        printf("n=%u idx=%u -> kind=%u count=%llu flag=%u\n", nw, idx, r.kind, (unsigned long long)r.count, r.flag);
    }
    return 0;
}
