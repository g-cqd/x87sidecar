/* Privileged / system / odd instructions: which signal, si_code, and is the reported rip the instruction's own address. */
#include "gaps.h"
#include <alloca.h>

static unsigned char dtbl[16];
int main(int argc, char **argv) {
    gaps_install();
    printf("== privileged and system instructions (rip_minus_insn 0 = rip points AT the instruction)\n");
    PROBE("in al,dx (port 0x60)", { asm volatile("lea 1f(%%rip),%%r11\n\tmovq %%r11,%0\n\tmov $0x60,%%edx\n1:\tin %%dx,%%al" : "=m"(g_expect) :: "r11","rax","rdx"); });
    PROBE("out dx,al", { asm volatile("lea 1f(%%rip),%%r11\n\tmovq %%r11,%0\n\tmov $0x80,%%edx\n1:\tout %%al,%%dx" : "=m"(g_expect) :: "r11","rax","rdx"); });
    PROBE("hlt", ASM1("hlt"));
    PROBE("cli", ASM1("cli"));
    PROBE("sti", ASM1("sti"));
    PROBE("rdmsr", ASM1("xor %%ecx,%%ecx\n\trdmsr"));
    PROBE("wrmsr", ASM1("xor %%ecx,%%ecx\n\twrmsr"));
    PROBE("mov rax,cr0", ASM1("mov %%cr0,%%rax"));
    PROBE("mov cr0,rax", ASM1("mov %%rax,%%cr0"));
    PROBE("mov rax,dr0", ASM1("mov %%dr0,%%rax"));
    PROBE("mov dr0,rax", ASM1("xor %%eax,%%eax\n\tmov %%rax,%%dr0"));
    PROBE("mov rax,dr7", ASM1("mov %%dr7,%%rax"));
    PROBE("rdpmc", ASM1("xor %%ecx,%%ecx\n\trdpmc"));
    PROBE("lgdt [mem]", asm volatile("lea 1f(%%rip),%%r11\n\tmovq %%r11,%0\n1:\tlgdt (%1)" : "=m"(g_expect) : "r"(dtbl) : "r11","memory"));
    PROBE("lidt [mem]", asm volatile("lea 1f(%%rip),%%r11\n\tmovq %%r11,%0\n1:\tlidt (%1)" : "=m"(g_expect) : "r"(dtbl) : "r11","memory"));
    PROBE("sgdt [mem]", asm volatile("lea 1f(%%rip),%%r11\n\tmovq %%r11,%0\n1:\tsgdt (%1)" : "=m"(g_expect) : "r"(dtbl) : "r11","memory"));
    PROBE("sidt [mem]", asm volatile("lea 1f(%%rip),%%r11\n\tmovq %%r11,%0\n1:\tsidt (%1)" : "=m"(g_expect) : "r"(dtbl) : "r11","memory"));
    PROBE("sldt ax", ASM1("sldt %%ax"));
    PROBE("str ax", ASM1("str %%ax"));
    PROBE("smsw ax", ASM1("smsw %%ax"));
    PROBE("lmsw ax", ASM1("xor %%eax,%%eax\n\tlmsw %%ax"));
    PROBE("invd", ASM1("invd"));
    PROBE("wbinvd", ASM1("wbinvd"));
    PROBE("invlpg", asm volatile("lea 1f(%%rip),%%r11\n\tmovq %%r11,%0\n1:\tinvlpg (%1)" : "=m"(g_expect) : "r"(dtbl) : "r11","memory"));
    PROBE("swapgs", ASM1("swapgs"));
    PROBE("sysret", ASM1("sysretq"));
    if (argc > 1 && !strcmp(argv[1], "iretq")) PROBE("iretq (garbage frame)", ASM1("iretq"));
    PROBE("ud2", ASM1("ud2"));
    PROBE("int3", ASM1("int3"));
    PROBE("int 0x80", ASM1("mov $0x20000ff,%%eax\n\tint $0x80"));
    PROBE("int 0x2e", ASM1("int $0x2e"));
    PROBE("int 1", ASM1("int $1"));
    PROBE("int 0xff", ASM1("int $0xff"));
    PROBE("into (64-bit: invalid)", ASM1(".byte 0xce"));
    PROBE("sysenter", ASM1("sysenter"));
    PROBE("syscall getpid(BSD 20)", ASM1("mov $0x2000014,%%eax\n\tsyscall"));
    PROBE("xlat", asm volatile("lea 1f(%%rip),%%r11\n\tmovq %%r11,%0\n\txor %%eax,%%eax\n\tmov %1,%%rbx\n1:\txlatb" : "=m"(g_expect) : "r"(dtbl) : "r11","rax","rbx"));
    PROBE("bound (64-bit: invalid)", ASM1(".byte 0x62,0x00"));
    PROBE("aaa (64-bit: invalid)", ASM1(".byte 0x37"));
    PROBE("pusha (64-bit: invalid)", ASM1(".byte 0x60"));
    PROBE("les/lds (64-bit: VEX/invalid)", ASM1(".byte 0xc4,0x00"));
    PROBE("lock on non-lockable (lock nop)", ASM1(".byte 0xf0,0x90"));
    PROBE("lock add reg,reg (invalid)", ASM1(".byte 0xf0,0x01,0xc0"));
    PROBE("enter 16,0 / leave", asm volatile("lea 1f(%%rip),%%r11\n\tmovq %%r11,%0\n1:\tenter $16,$0\n\tleave" : "=m"(g_expect) :: "r11","memory"));
    PROBE("enter 16,3 (nested) / leave", asm volatile("lea 1f(%%rip),%%r11\n\tmovq %%r11,%0\n1:\tenter $16,$3\n\tleave" : "=m"(g_expect) :: "r11","memory"));
    PROBE("cmpxchg8b [mem]", asm volatile("lea 1f(%%rip),%%r11\n\tmovq %%r11,%0\n\txor %%eax,%%eax\n\txor %%edx,%%edx\n\txor %%ebx,%%ebx\n\txor %%ecx,%%ecx\n1:\tcmpxchg8b (%1)" : "=m"(g_expect) : "r"(dtbl) : "r11","rax","rbx","rcx","rdx","cc","memory"));
    PROBE("verr ax", ASM1("mov %%cs,%%ax\n\tverr %%ax"));
    PROBE("lar eax,ax", ASM1("mov %%cs,%%ax\n\tlar %%ax,%%eax"));
    PROBE("lsl eax,ax", ASM1("mov %%cs,%%ax\n\tlsl %%ax,%%eax"));
    PROBE("rdfsbase (needs FSGSBASE)", ASM1("rdfsbase %%rax"));
    PROBE("rdpid", ASM1(".byte 0xf3,0x0f,0xc7,0xf8"));
    PROBE("monitor/mwait", ASM1("xor %%ecx,%%ecx\n\txor %%edx,%%edx\n\tmonitor"));
    PROBE("xgetbv (ecx=0)", ASM1("xor %%ecx,%%ecx\n\txgetbv"));
    PROBE("xsetbv", ASM1("xor %%ecx,%%ecx\n\tmov $7,%%eax\n\txor %%edx,%%edx\n\txsetbv"));
    PROBE("vzeroupper", ASM1("vzeroupper"));
    PROBE("emms", ASM1("emms"));
    PROBE("ud1 / invalid 0f ff", ASM1(".byte 0x0f,0xff,0xc0"));
    PROBE("0f 0b via prefix 66 ud2", ASM1(".byte 0x66,0x0f,0x0b"));
    PROBE("int 3 via cd 03", ASM1(".byte 0xcd,0x03"));
    PROBE("popf with IOPL/IF bits", ASM1("pushfq\n\torq $0x3200,(%%rsp)\n\tpopfq"));
    PROBE("pushf/popf with AC (bit18)", ASM1("pushfq\n\torq $0x40000,(%%rsp)\n\tpopfq\n\tpushfq\n\tandq $~0x40000,(%%rsp)\n\tpopfq"));
    PROBE("popf with ID toggle (bit21)", ASM1("pushfq\n\txorq $0x200000,(%%rsp)\n\tpopfq"));
    { uint64_t a; asm volatile("pushfq\n\tpopq %0" : "=r"(a)); printf("  rflags now = %#llx (bits: IF=%d)\n", (unsigned long long)a, !!(a & 0x200)); }
    { uint32_t s; asm volatile("mov %%cs,%0" : "=r"(s)); uint32_t d, e, f, g, ss; asm volatile("mov %%ds,%0" : "=r"(d)); asm volatile("mov %%es,%0" : "=r"(e)); asm volatile("mov %%fs,%0" : "=r"(f)); asm volatile("mov %%gs,%0" : "=r"(g)); asm volatile("mov %%ss,%0" : "=r"(ss));
      printf("  segment regs: cs=%#x ds=%#x es=%#x fs=%#x gs=%#x ss=%#x\n", s, d, e, f, g, ss); }
    return 0;
}
