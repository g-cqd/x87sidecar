/* Shared helpers for the gaps_a probes: run one risky snippet under signal guards and report the outcome. */
#pragma once
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include <errno.h>

static sigjmp_buf g_jb;
static volatile int g_sig, g_code, g_armed;
static volatile uint64_t g_addr, g_rip, g_expect;
static volatile uint64_t g_rax, g_rsp, g_rflags;

static void gaps_handler(int s, siginfo_t *si, void *ucv) {
    ucontext_t *u = (ucontext_t *)ucv;
    g_sig = s; g_code = si->si_code; g_addr = (uint64_t)si->si_addr;
    g_rip = u->uc_mcontext->__ss.__rip; g_rax = u->uc_mcontext->__ss.__rax;
    g_rsp = u->uc_mcontext->__ss.__rsp; g_rflags = u->uc_mcontext->__ss.__rflags;
    if (g_armed) siglongjmp(g_jb, 1);
    _exit(99);
}

static void gaps_install(void) {
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = gaps_handler; sa.sa_flags = SA_SIGINFO | SA_NODEFER; sigemptyset(&sa.sa_mask);
    int sigs[] = {SIGILL, SIGSEGV, SIGBUS, SIGFPE, SIGTRAP};
    for (unsigned i = 0; i < sizeof sigs / sizeof *sigs; i++) sigaction(sigs[i], &sa, NULL);
    setvbuf(stdout, NULL, _IOLBF, 0);
}

static const char *signame(int s) {
    switch (s) { case SIGILL: return "SIGILL"; case SIGSEGV: return "SIGSEGV"; case SIGBUS: return "SIGBUS";
                 case SIGFPE: return "SIGFPE"; case SIGTRAP: return "SIGTRAP"; default: return "?"; }
}

/* Returns 0 when body completed, else the signal number. Report line printed either way. */
#define PROBE_Q(name, body) ({ int _r = 0; g_sig = 0; g_expect = 0; \
    if (!sigsetjmp(g_jb, 1)) { g_armed = 1; body; g_armed = 0; } else { g_armed = 0; _r = g_sig; } _r; })

#define PROBE(name, body) ({ int _r = PROBE_Q(name, body); \
    if (_r) printf("  %-34s -> %s code=%d addr=%#llx rip_minus_insn=%lld\n", name, signame(_r), g_code, \
                   (unsigned long long)g_addr, g_expect ? (long long)(g_rip - g_expect) : -1LL); \
    else printf("  %-34s -> executed\n", name); _r; })

/* Single instruction with the address recorded in g_expect first (r11 is scratch). */
#define ASM1(insn) asm volatile("lea 1f(%%rip), %%r11\n\tmovq %%r11, %0\n1:\t" insn : "=m"(g_expect) :: "r11", "rax", "rcx", "rdx", "memory")
