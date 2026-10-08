// Micro-benchmark driver: ns per loop iteration, median/min/max over reps.
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define P(name, k) extern void p_##name(uint64_t, void *);
#ifdef __aarch64__
#define PX(name, k)
#else
#define PX(name, k) extern void p_##name(uint64_t, void *);
#endif
#include "probes.def"
#undef P
#undef PX

typedef struct { const char *name; int k; void (*fn)(uint64_t, void *); } Probe;
#define P(name, k) {#name, k, p_##name},
#ifdef __aarch64__
#define PX(name, k)
#else
#define PX(name, k) {#name, k, p_##name},
#endif
static const Probe probes[] = {
#include "probes.def"
};

static uint8_t *buf;

static void init_buf(void) {
    double d[5] = {1.0, 1e-7, 0.99999, 2.0, 1.0};
    memcpy(buf, d, sizeof d);
    float f4[4] = {1.0f, 1e-7f, 0.99999f, 2.0f};
    memcpy(buf + 64, f4, 16);
    float one = 1.0f;
    memcpy(buf + 80, &one, 4);
    float v1[4] = {1e-7f, 1e-7f, 1e-7f, 1e-7f}, v2[4] = {0.99999f, 0.99999f, 0.99999f, 0.99999f};
    float v3[4] = {1, 2, 3, 4};
    int w[4] = {0, 1, 2, 3};
    memcpy(buf + 96, v1, 16);
    memcpy(buf + 112, v2, 16);
    memcpy(buf + 128, v3, 16);
    memcpy(buf + 144, w, 16);
    uint64_t x = 0x9e3779b97f4a7c15ull;
    for (int i = 160; i < 8192; i += 8) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; memcpy(buf + i, &x, 8); }
}

static uint64_t now(void) { return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW); }
static int cmp(const void *a, const void *b) { double x = *(double *)a, y = *(double *)b; return (x > y) - (x < y); }

int main(int argc, char **argv) {
    const char *filter = argc > 1 ? argv[1] : "";
    int reps = argc > 2 ? atoi(argv[2]) : 9;
    double target_ms = argc > 3 ? atof(argv[3]) : 30.0;
#ifdef __aarch64__
    const char *arch = "arm64";
#else
    const char *arch = "x86_64";
#endif
    buf = aligned_alloc(4096, 1 << 20);
    memset(buf, 0, 1 << 20);
    for (size_t i = 0; i < sizeof probes / sizeof *probes; i++) {
        const Probe *p = &probes[i];
        if (*filter && strcmp(filter, "all")) {
            char tmp[2048]; snprintf(tmp, sizeof tmp, ",%s,", filter);
            char nm[128]; snprintf(nm, sizeof nm, ",%s,", p->name);
            if (!strstr(tmp, nm)) continue;
        }
        uint64_t n = 256;
        for (;;) {
            init_buf();
            uint64_t t0 = now(); p->fn(n, buf); uint64_t t1 = now();
            if ((t1 - t0) >= target_ms * 1e6 * 0.5 || n > (1ull << 33)) break;
            n *= 2;
        }
        double want = target_ms * 1e6;
        init_buf();
        uint64_t t0 = now(); p->fn(n, buf); uint64_t t1 = now();
        n = (uint64_t)((double)n * want / (double)(t1 - t0 ? t1 - t0 : 1));
        if (n < 256) n = 256;
        double s[64];
        if (reps > 64) reps = 64;
        for (int r = 0; r < reps + 1; r++) {
            init_buf();
            uint64_t a = now(); p->fn(n, buf); uint64_t b = now();
            if (r == 0) continue;  // warm-up
            s[r - 1] = (double)(b - a) / (double)n;
        }
        qsort(s, reps, sizeof(double), cmp);
        printf("%s,%s,%d,%llu,%.4f,%.4f,%.4f\n", arch, p->name, p->k, (unsigned long long)n, s[0], s[reps / 2], s[reps - 1]);
        fflush(stdout);
    }
    return 0;
}
