#include <stdio.h>
#include <stdint.h>
#include <time.h>
static uint64_t now(){return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);}
#define N 20000000
static uint64_t buf[256] __attribute__((aligned(128)));
#define LDLOOP(LD) for(long i=0;i<N;i++){ uint64_t *p=buf+(i&63); uint64_t a,b,c,d; asm volatile(LD " %0,[%4]\n" LD " %1,[%5]\n" LD " %2,[%6]\n" LD " %3,[%7]\n" :"=&r"(a),"=&r"(b),"=&r"(c),"=&r"(d):"r"(p),"r"(p+64),"r"(p+128),"r"(p+192):"memory"); }
#define STLOOP(ST) for(long i=0;i<N;i++){ uint64_t *p=buf+(i&63); asm volatile(ST " %0,[%1]\n" ST " %0,[%2]\n" ST " %0,[%3]\n" ST " %0,[%4]\n" ::"r"(i),"r"(p),"r"(p+64),"r"(p+128),"r"(p+192):"memory"); }
double run(int m){ uint64_t best=~0ull; for(int r=0;r<9;r++){ uint64_t t=now();
 switch(m){case 0:LDLOOP("ldr")break;case 1:LDLOOP("ldapr")break;case 2:LDLOOP("ldar")break;case 3:STLOOP("str")break;case 4:STLOOP("stlr")break;}
 uint64_t e=now()-t; if(e<best)best=e;} return (double)best/N/4; }
int main(){ const char*n[]={"ldr","ldapr","ldar","str","stlr"}; for(int m=0;m<5;m++) printf("%-6s %.3f ns/op (independent, 4 per iter)\n",n[m],run(m)); }
