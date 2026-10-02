// Compiler-generated kernels (same C source for x86_64 and arm64) to cross-check the hand-written probes.
// Reports ns per kernel call (median/min/max). Build twice: default and -fno-vectorize -fno-slp-vectorize.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N 256
typedef struct { float x, y, z; } V3;
static V3 va[N], vb[N];
static float m1[16], m2[16], m3[16];
static int ints[64], tmp[64];
static uint8_t bytes[4096], b2[4096];
static float fl[1024];
volatile double sink;
static uint64_t now(void) { return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW); }

__attribute__((noinline)) static double k_vec3(void) {
    float acc = 0;
    for (int i = 0; i < N; i++) {
        V3 a = va[i], b = vb[i];
        float l = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z) + 1e-6f;
        float inv = 1.0f / l;
        V3 n = {a.x * inv, a.y * inv, a.z * inv};
        V3 c = {n.y * b.z - n.z * b.y, n.z * b.x - n.x * b.z, n.x * b.y - n.y * b.x};
        acc += n.x * b.x + n.y * b.y + n.z * b.z + c.x + c.y + c.z;
    }
    return acc;
}
__attribute__((noinline)) static double k_mat4(void) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += m1[r * 4 + k] * m2[k * 4 + c];
            m3[r * 4 + c] = s;
        }
    return m3[5];
}
__attribute__((noinline)) static double k_sort(void) {
    memcpy(tmp, ints, sizeof tmp);
    for (int i = 1; i < 64; i++) {
        int v = tmp[i], j = i - 1;
        while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; }
        tmp[j + 1] = v;
    }
    return tmp[10];
}
struct Obj { int (*fn)(struct Obj *, int); int v; };
static int f0(struct Obj *o, int a) { return o->v += a; }
static int f1(struct Obj *o, int a) { return o->v ^= a; }
static int f2(struct Obj *o, int a) { return o->v -= a * 3; }
static int f3(struct Obj *o, int a) { return o->v = o->v * 5 + a; }
static struct Obj objs[8];
__attribute__((noinline)) static double k_vcall(void) {
    int s = 0;
    for (int i = 0; i < 256; i++) s += objs[(i * 5) & 7].fn(&objs[(i * 5) & 7], i);
    return s;
}
__attribute__((noinline)) static double k_hash(void) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 4096; i++) { h ^= bytes[i]; h *= 16777619u; }
    return h;
}
__attribute__((noinline)) static double k_ftol(void) {
    int s = 0;
    for (int i = 0; i < 1024; i++) { int v = (int)fl[i]; s += v > 255 ? 255 : v; }
    return s;
}
__attribute__((noinline)) static double k_memcpy16(void) { for (int i = 0; i < 64; i++) memcpy(b2 + i * 16, bytes + i * 16 + 7, 16); return b2[3]; }
__attribute__((noinline)) static double k_memcpy256(void) { for (int i = 0; i < 8; i++) memcpy(b2 + i * 256, bytes + i * 256 + 1, 256); return b2[3]; }
__attribute__((noinline)) static double k_memcpy4k(void) { memcpy(b2, bytes, 4096); return b2[3]; }
__attribute__((noinline)) static double k_memset(void) { for (int i = 0; i < 8; i++) memset(b2 + i * 256, i, 256); return b2[3]; }
__attribute__((noinline)) static double k_i64(void) {
    uint64_t a = 0x123456789abcdefull, b = 7, s = 0;
    for (int i = 0; i < 1024; i++) { a = a * 6364136223846793005ull + 1442695040888963407ull; s += (a >> 33) * b + (a & 0xffff); b ^= s; }
    return (double)s;
}
typedef struct { const char *n; double (*f)(void); int reps_per_call; } K;
static const K ks[] = {{"vec3_norm_cross_256", k_vec3, 1}, {"mat4_mul_scalar", k_mat4, 1}, {"insertion_sort_64", k_sort, 1},
    {"vcall_256", k_vcall, 1}, {"fnv1a_4k", k_hash, 1}, {"ftol_clamp_1024", k_ftol, 1}, {"memcpy16x64", k_memcpy16, 1},
    {"memcpy256x8", k_memcpy256, 1}, {"memcpy4k", k_memcpy4k, 1}, {"memset256x8", k_memset, 1}, {"lcg_i64_1024", k_i64, 1}};
static int cmp(const void *a, const void *b) { double x = *(double *)a, y = *(double *)b; return (x > y) - (x < y); }
int main(int argc, char **argv) {
    int reps = argc > 1 ? atoi(argv[1]) : 9;
#ifdef __aarch64__
    const char *arch = "arm64";
#else
    const char *arch = "x86_64";
#endif
    uint32_t x = 12345;
    for (int i = 0; i < N; i++) { x = x * 1103515245 + 12345; va[i] = (V3){(x >> 8 & 255) / 16.f + 1, (x >> 4 & 255) / 16.f + 1, (x & 255) / 16.f + 1}; vb[i] = va[(i * 7) & 255]; }
    for (int i = 0; i < 16; i++) { m1[i] = i * 0.1f; m2[i] = 1.0f - i * 0.05f; }
    for (int i = 0; i < 64; i++) { x = x * 1103515245 + 12345; ints[i] = x >> 8; }
    for (int i = 0; i < 4096; i++) bytes[i] = i * 7;
    for (int i = 0; i < 1024; i++) fl[i] = (i % 300) * 1.7f;
    for (int i = 0; i < 8; i++) { objs[i].v = i; objs[i].fn = (int (*[])(struct Obj *, int)){f0, f1, f2, f3}[i & 3]; }
    for (size_t k = 0; k < sizeof ks / sizeof *ks; k++) {
        uint64_t n = 16;
        for (;;) { uint64_t a = now(); for (uint64_t i = 0; i < n; i++) sink += ks[k].f(); if (now() - a > 15000000) break; n *= 2; }
        double s[32];
        for (int r = -1; r < reps; r++) {
            uint64_t a = now(); for (uint64_t i = 0; i < n; i++) sink += ks[k].f(); uint64_t b = now();
            if (r >= 0) s[r] = (double)(b - a) / n;
        }
        qsort(s, reps, sizeof(double), cmp);
        printf("%s,%s,%.2f,%.2f,%.2f\n", arch, ks[k].n, s[reps / 2], s[0], s[reps - 1]);
    }
    return 0;
}
