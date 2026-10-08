#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { uint8_t b[10]; } f80;
static inline f80 mk(uint16_t se, uint64_t m) { f80 r; memcpy(r.b, &m, 8); memcpy(r.b + 8, &se, 2); return r; }
static inline uint16_t f80se(f80 v) { uint16_t s; memcpy(&s, v.b + 8, 2); return s; }
static inline uint64_t f80m(f80 v) { uint64_t m; memcpy(&m, v.b, 8); return m; }
static inline void p80(f80 v) { printf("%04x:%016llx", f80se(v), (unsigned long long)f80m(v)); }
static inline uint16_t rd_sw(void) { uint16_t s; __asm__ volatile("fnstsw %0" : "=a"(s)); return s; }
static inline uint16_t rd_cw(void) { uint16_t s; __asm__ volatile("fnstcw %0" : "=m"(s)); return s; }
static inline void set_cw(uint16_t c) { __asm__ volatile("fldcw %0" ::"m"(c)); }
static inline void fpinit(void) { __asm__ volatile("fninit"); }
static inline void fpclex(void) { __asm__ volatile("fnclex"); }
static uint64_t rng_s = 0x9E3779B97F4A7C15ull;
static inline uint64_t rng(void) { rng_s ^= rng_s << 13; rng_s ^= rng_s >> 7; rng_s ^= rng_s << 17; return rng_s; }
