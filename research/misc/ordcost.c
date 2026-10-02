// cost of x86-TSO-compatible ordering on arm64 without hardware TSO
#include <stdio.h>
#include <stdint.h>
#include <time.h>
static uint64_t now(){return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);}
#define N 20000000
static uint64_t buf[64] __attribute__((aligned(128)));
#define BODY(LD,ST) \
 for (long i=0;i<N;i++){ uint64_t a,b,c; uint64_t *p=buf+(i&15); \
  asm volatile(LD " %0,[%3]\n" LD " %1,[%4]\n" "add %0,%0,%1\n" ST " %0,[%5]\n" LD " %2,[%5]\n" "add %2,%2,%0\n" ST " %2,[%3]\n" \
   : "=&r"(a),"=&r"(b),"=&r"(c) : "r"(p),"r"(p+1),"r"(p+32) : "memory"); }
double run(int m){ uint64_t best=~0ull; for(int r=0;r<9;r++){ uint64_t t=now();
  if(m==0){ BODY("ldr","str") } else if(m==1){ BODY("ldapr","stlr") } else { BODY("ldar","stlr") }
  uint64_t e=now()-t; if(e<best)best=e;} return (double)best/N; }
int main(){ printf("plain ldr/str   : %.3f ns/iter (3 ld + 2 st)\nldapr/stlr(RCpc): %.3f\nldar/stlr       : %.3f\n",run(0),run(1),run(2)); }
