// x86_64 probe loops (cpp-preprocessed; Darwin asm macros mangle $imm, so all macros are cpp).
// rdi=n, rsi=buffer. r11=loop counter, r8=buffer, rbx saved. Probe: setup; 1: body; dec r11; jnz 1b.
#define R2(...) __VA_ARGS__ ; __VA_ARGS__
#define R4(...) R2(__VA_ARGS__) ; R2(__VA_ARGS__)
#define R8(...) R4(__VA_ARGS__) ; R4(__VA_ARGS__)
#define BEGIN(name) .globl _p_##name ; .p2align 4 ; _p_##name: push %rbx ; mov %rdi, %r11 ; mov %rsi, %r8
#define FIN dec %r11 ; jnz 1b ; pop %rbx ; ret
#define INITINT mov $1, %eax ; movabsq $0x9e3779b97f4a7c15, %rdx ; mov $3, %ecx ; xor %r9d, %r9d
#define INITSS movss 64(%r8), %xmm0 ; movss 68(%r8), %xmm1 ; movss 72(%r8), %xmm2 ; movss 76(%r8), %xmm3 ; movss 80(%r8), %xmm4 ; mov $1000, %eax
#define INITSD movsd 0(%r8), %xmm0 ; movsd 8(%r8), %xmm1 ; movsd 16(%r8), %xmm2 ; movsd 24(%r8), %xmm3 ; movsd 32(%r8), %xmm4 ; mov $1000, %eax
#define INITPS movups 128(%r8), %xmm0 ; movups 96(%r8), %xmm1 ; movups 112(%r8), %xmm2 ; mov $1, %eax ; xor %edx, %edx
#define INITMEM(off) lea off(%r8), %rsi ; mov $1, %eax ; mov $5, %edx ; xor %r9d, %r9d ; xor %r10d, %r10d
#define XORSHIFT mov %rax, %rdx ; shl $13, %rdx ; xor %rdx, %rax ; mov %rax, %rdx ; shr $7, %rdx ; xor %rdx, %rax ; mov %rax, %rdx ; shl $17, %rdx ; xor %rdx, %rax
#define IDX0 xor %ecx, %ecx
#define IDX1 mov %r11, %rcx ; and $7, %ecx
#define IDX2 mov %rsi, %rdx ; shl $13, %rdx ; xor %rdx, %rsi ; mov %rsi, %rdx ; shr $7, %rdx ; xor %rdx, %rsi ; mov %rsi, %rdx ; shl $17, %rdx ; xor %rdx, %rsi ; mov %rsi, %rcx ; shr $20, %rcx ; and $7, %ecx
#define JT(n) .p2align 2 ; n: add $1, %rax ; jmp 4f
#define CT(n) .p2align 2 ; n: add $1, %rax ; ret
#define TBL .p2align 2 ; 5: .long 100b-5b, 101b-5b, 102b-5b, 103b-5b, 104b-5b, 105b-5b, 106b-5b, 107b-5b
#define IJMP(name, idx) BEGIN(name) ; lea 5f(%rip), %r9 ; movabsq $0x123456789, %rsi ; xor %eax, %eax ; 1: idx ; movslq (%r9,%rcx,4), %rdx ; add %r9, %rdx ; jmp *%rdx ; JT(100) ; JT(101) ; JT(102) ; JT(103) ; JT(104) ; JT(105) ; JT(106) ; JT(107) ; 4: FIN ; TBL
#define ICALL(name, idx) BEGIN(name) ; lea 5f(%rip), %r9 ; movabsq $0x123456789, %rsi ; xor %eax, %eax ; 1: idx ; movslq (%r9,%rcx,4), %rdx ; add %r9, %rdx ; call *%rdx ; FIN ; CT(100) ; CT(101) ; CT(102) ; CT(103) ; CT(104) ; CT(105) ; CT(106) ; CT(107) ; TBL
#define RP(name, cnt, insn) BEGIN(name) ; 1: lea 4096(%r8), %rsi ; lea 8192(%r8), %rdi ; mov $cnt, %ecx ; xor %eax, %eax ; insn ; FIN

.text
// x86_64 probe loops. rdi=n (iterations), rsi=buffer. r11=loop counter, r8=buffer,
// rbx saved. Each probe: setup; 1: body (.rept K groups); dec r11; jnz 1b.
.text







IJMP(ijmp_mono, IDX0)
IJMP(ijmp_seq8, IDX1)
IJMP(ijmp_rand8, IDX2)
ICALL(icall_mono, IDX0)
ICALL(icall_seq8, IDX1)
ICALL(icall_rand8, IDX2)
RP(rep_movsb8, 8, rep movsb)
RP(rep_movsb64, 64, rep movsb)
RP(rep_movsb1k, 1024, rep movsb)
RP(rep_movsd64, 16, rep movsl)
RP(rep_stosd64, 16, rep stosl)
RP(rep_stosb1k, 1024, rep stosb)
BEGIN(empty)
1:
FIN
BEGIN(alu_chain)
    INITINT
1:  R2(add $3, %rax ; xor %rdx, %rax ; sub $1, %rax ; and $-2, %rax)
FIN
BEGIN(alu32_chain)
    INITINT
1:  R2(add $3, %eax ; xor %edx, %eax ; sub $1, %eax ; and $-2, %eax)
FIN
BEGIN(alu_adc)
    INITINT
1:  R8(adc %rdx, %rax)
FIN
BEGIN(alu_setcc)
    INITINT
1:  R4(cmp %rdx, %rax ; setl %r9b ; add %r9, %rax)
FIN
BEGIN(alu_jcc_flags)
    INITINT
1:  R8(add %rdx, %rax ; jo 2f ; 2:)
FIN

BEGIN(br_pred)
    movabsq $0x123456789, %rax
    xor %r9d, %r9d
    xor %esi, %esi
1:  XORSHIFT
    test %r9, %r9
    jnz 2f
    inc %rsi
2:
FIN
BEGIN(br_unpred)
    movabsq $0x123456789, %rax
    xor %r9d, %r9d
    xor %esi, %esi
1:  XORSHIFT
    test $1, %al
    jz 2f
    inc %rsi
2:
FIN

.p2align 4
_leaf_x:
    add $1, %rax
    ret
.p2align 4
_rec_x:
    test %r10, %r10
    jz 2f
    dec %r10
    call _rec_x
    inc %rax
2:  ret
BEGIN(call_ret)
    xor %eax, %eax
1:  R8(call _leaf_x)
FIN
BEGIN(rec64)
    xor %eax, %eax
1:  mov $64, %r10d
    call _rec_x
FIN
BEGIN(rec1024)
    xor %eax, %eax
1:  mov $1024, %r10d
    call _rec_x
FIN

// indirect jump / call through a PIC-style relative table of 8 targets




BEGIN(sse_ss_addmul)
    INITSS
1:  R4(addss %xmm1, %xmm0 ; mulss %xmm2, %xmm0)
FIN
BEGIN(sse_ss_sqrt)
    INITSS
    movaps %xmm3, %xmm0
1:  R8(sqrtss %xmm0, %xmm0)
FIN
BEGIN(sse_ss_div)
    INITSS
1:  R4(divss %xmm4, %xmm0)
FIN
BEGIN(sse_ss_cvt)
    INITSS
1:  R4(cvtsi2ssl %eax, %xmm0 ; cvttss2si %xmm0, %eax ; inc %eax)
FIN
BEGIN(sse_ss_comiss)
    INITSS
1:  R8(comiss %xmm1, %xmm0 ; ja 2f ; 2:)
FIN
BEGIN(sse_sd_addmul)
    INITSD
1:  R4(addsd %xmm1, %xmm0 ; mulsd %xmm2, %xmm0)
FIN
BEGIN(sse_sd_sqrt)
    INITSD
    movaps %xmm3, %xmm0
1:  R8(sqrtsd %xmm0, %xmm0)
FIN
BEGIN(sse_sd_div)
    INITSD
1:  R4(divsd %xmm4, %xmm0)
FIN
BEGIN(sse_sd_cvt)
    INITSD
1:  R4(cvtsi2sdl %eax, %xmm0 ; cvttsd2si %xmm0, %eax ; inc %eax)
FIN
BEGIN(sse_sd_comisd)
    INITSD
1:  R8(comisd %xmm1, %xmm0 ; ja 2f ; 2:)
FIN
BEGIN(ps_addmul)
    INITPS
1:  R4(addps %xmm1, %xmm0 ; mulps %xmm2, %xmm0)
FIN
BEGIN(ps_shufps)
    INITPS
1:  R8(shufps $0x1b, %xmm0, %xmm0)
FIN
BEGIN(pi_pshufd)
    INITPS
1:  R8(pshufd $0x4e, %xmm0, %xmm0)
FIN
BEGIN(pi_paddd)
    INITPS
1:  R8(paddd %xmm1, %xmm0)
FIN
BEGIN(ps_movaps)
    INITPS
1:  R4(movaps %xmm0, %xmm1 ; movaps %xmm1, %xmm0)
FIN
BEGIN(ps_movmsk)
    INITPS
1:  R4(movmskps %xmm0, %eax ; add %eax, %edx)
FIN


BEGIN(mem_load)
    INITMEM(4096)
1:  R2(mov 0(%rsi), %rax ; mov 8(%rsi), %rdx ; mov 16(%rsi), %r9 ; mov 24(%rsi), %r10)
FIN
BEGIN(mem_store)
    INITMEM(4096)
1:  R2(mov %rax, 0(%rsi) ; mov %rdx, 8(%rsi) ; mov %rax, 16(%rsi) ; mov %rdx, 24(%rsi))
FIN
BEGIN(mem_load_unal)
    INITMEM(4096)
1:  R2(mov 1(%rsi), %rax ; mov 9(%rsi), %rdx ; mov 17(%rsi), %r9 ; mov 57(%rsi), %r10)
FIN
BEGIN(mem_ldop)
    INITMEM(4096)
1:  R8(add 0(%rsi), %rax)
FIN
BEGIN(mem_rmw)
    INITMEM(4096)
1:  R4(add %rdx, (%rsi))
FIN
BEGIN(mem_fwd_ok)
    INITMEM(4096)
1:  R4(mov %rax, (%rsi) ; mov (%rsi), %rax ; add $1, %rax)
FIN
BEGIN(mem_fwd_bad)
    INITMEM(4096)
1:  R4(mov %al, (%rsi) ; mov (%rsi), %eax ; add $1, %eax)
FIN
BEGIN(mem_stride)
    INITMEM(8192)
    xor %edx, %edx
1:  R4(mov (%rsi,%rdx), %rax ; add $64, %edx ; and $0x3ffc0, %edx)
FIN




BEGIN(cpuid)
1:  mov $1, %eax
    cpuid
FIN
BEGIN(rdtsc)
1:  rdtsc
FIN

BEGIN(part_al)
    INITINT
1:  R4(movb %dl, %al ; add %rax, %rdx)
FIN
BEGIN(part_ah)
    INITINT
1:  R4(movb %dl, %ah ; add %rax, %rdx)
FIN
BEGIN(part_ax)
    INITINT
1:  R4(movw %dx, %ax ; add %rax, %rdx)
FIN
BEGIN(movzx_sx)
    INITINT
1:  R4(movzbl %al, %edx ; movsbl %dl, %eax ; movzwl %ax, %edx ; movswl %dx, %eax)
FIN
BEGIN(lea_simple)
    INITINT
1:  R8(lea 8(%rax), %rax)
FIN
BEGIN(lea_scaled)
    INITINT
1:  R8(lea (%rax,%rdx,4), %rax)
FIN
BEGIN(lea_complex)
    INITINT
1:  R8(lea 4(%rax,%rdx,2), %rax)
FIN
BEGIN(lea32)
    INITINT
1:  R8(leal 4(%rax,%rdx,2), %eax)
FIN

BEGIN(shl_cl_dead)
    INITINT
1:  R4(shl %cl, %rax ; or %rdx, %rax)
FIN
BEGIN(shl_cl_flag)
    INITINT
1:  R4(shl %cl, %rax ; adc %rdx, %rax)
FIN
BEGIN(rol_ror)
    INITINT
1:  R4(rol $5, %rax ; ror %cl, %rdx)
FIN
BEGIN(shld)
    INITINT
1:  R8(shld $5, %rdx, %rax)
FIN
BEGIN(bts_jc)
    INITINT
1:  R4(btsq %rcx, %rax ; jc 2f ; 2:)
FIN
BEGIN(imul)
    INITINT
    or $1, %rdx
1:  R8(imul %rdx, %rax)
FIN
BEGIN(mul128)
    INITINT
    movabsq $0x9e3779b97f4a7c15, %rcx
1:  R4(mul %rcx)
FIN
BEGIN(div32)
    mov $7, %ecx
1:  mov $0x12345678, %eax
    xor %edx, %edx
    div %ecx
FIN
BEGIN(div64)
    mov $7, %ecx
1:  movabsq $0x123456789abcdef, %rax
    xor %edx, %edx
    div %rcx
FIN
BEGIN(idiv64)
    mov $7, %ecx
1:  movabsq $-0x123456789abcdef, %rax
    cqto
    idiv %rcx
FIN
BEGIN(cmov)
    INITINT
1:  R4(cmp %rdx, %rax ; cmovl %rdx, %rax ; sub $5, %rax)
FIN
BEGIN(bsr)
    INITINT
1:  R4(bsrq %rax, %rdx ; add %rdx, %rax ; or $1, %rax)
FIN
BEGIN(popcnt)
    INITINT
1:  R4(popcntq %rax, %rdx ; add %rdx, %rax)
FIN

BEGIN(x87_chain)
1:  R4(fldl (%r8) ; faddl 8(%r8) ; fmull 16(%r8) ; fstpl (%r8))
FIN
BEGIN(x87_fcomi)
1:  R4(fldl (%r8) ; fldl 8(%r8) ; fcomip %st(1), %st ; fstp %st(0) ; jb 2f ; 2:)
FIN

BEGIN(lock_xadd)
    INITMEM(4096)
1:  R4(lock xaddq %rax, (%rsi))
FIN
BEGIN(lock_cmpxchg)
    INITMEM(4096)
1:  R4(lock cmpxchgq %rdx, (%rsi))
FIN
BEGIN(xchg_mem)
    INITMEM(4096)
1:  R4(xchgq %rax, (%rsi))
FIN
BEGIN(mfence)
1:  R4(mfence)
FIN

BEGIN(push_pop)
    INITINT
1:  R4(push %rax ; push %rdx ; pop %rax ; pop %rdx)
FIN
BEGIN(frame)
    INITINT
1:  R4(push %rbp ; mov %rsp, %rbp ; sub $32, %rsp ; mov %rax, -8(%rbp) ; mov -8(%rbp), %rax ; leave)
FIN
BEGIN(pushf_popf)
    INITINT
1:  R4(pushfq ; popfq)
FIN

BEGIN(sse_ss_minmax)
    INITSS
1:  R4(minss %xmm1, %xmm0 ; maxss %xmm2, %xmm0)
FIN
BEGIN(ps_minmax)
    INITPS
1:  R4(minps %xmm1, %xmm0 ; maxps %xmm2, %xmm0)
FIN
BEGIN(ps_cvt_i2f)
    INITPS
    movups 144(%r8), %xmm0
1:  R4(cvtdq2ps %xmm0, %xmm0 ; cvttps2dq %xmm0, %xmm0)
FIN
BEGIN(ps_cvt_pspd)
    INITPS
1:  R4(cvtps2pd %xmm0, %xmm0 ; cvtpd2ps %xmm0, %xmm0)
FIN
BEGIN(ps_cmp_sel)
    INITPS
1:  R4(movaps %xmm0, %xmm3 ; cmpltps %xmm1, %xmm3 ; andps %xmm3, %xmm0)
FIN
BEGIN(ps_rcpps)
    INITPS
1:  R4(rcpps %xmm0, %xmm3 ; addps %xmm3, %xmm0)
FIN
BEGIN(ps_rsqrtps)
    INITPS
1:  R4(rsqrtps %xmm0, %xmm3 ; addps %xmm3, %xmm0)
FIN
BEGIN(ps_sqrtps)
    INITPS
1:  R4(sqrtps %xmm0, %xmm0)
FIN
BEGIN(ps_divps)
    INITPS
1:  R4(divps %xmm2, %xmm0)
FIN
BEGIN(ps_unpcklps)
    INITPS
1:  R8(unpcklps %xmm1, %xmm0)
FIN
BEGIN(ps_movlhps)
    INITPS
1:  R8(movlhps %xmm1, %xmm0)
FIN
BEGIN(ps_movshdup)
    INITPS
1:  R8(movshdup %xmm0, %xmm0)
FIN
BEGIN(ps_insertps)
    INITPS
1:  R8(insertps $0x10, %xmm1, %xmm0)
FIN
BEGIN(ps_haddps)
    INITPS
1:  R8(haddps %xmm1, %xmm0)
FIN
BEGIN(ps_dpps)
    INITPS
1:  R4(dpps $0x71, %xmm1, %xmm0)
FIN
BEGIN(pi_pmulld)
    INITPS
1:  R8(pmulld %xmm1, %xmm0)
FIN
BEGIN(pi_pshufb)
    INITPS
1:  R8(pshufb %xmm1, %xmm0)
FIN
BEGIN(pi_pcmpeq_movmsk)
    INITPS
1:  R4(pcmpeqb %xmm1, %xmm0 ; pmovmskb %xmm0, %eax ; add %eax, %edx)
FIN
BEGIN(pi_punpcklbw)
    INITPS
1:  R8(punpcklbw %xmm1, %xmm0)
FIN
BEGIN(sse_movd_gpr)
    INITPS
1:  R4(movd %eax, %xmm0 ; movd %xmm0, %eax ; inc %eax)
FIN
BEGIN(sse_ucomiss_jp)
    INITSS
1:  R8(ucomiss %xmm1, %xmm0 ; jp 2f ; jne 2f ; 2:)
FIN
BEGIN(div64_dep)
    INITINT
    movabsq $0x123456789abcdef, %r9
    mov $7, %ecx
1:  R4(xor %edx, %edx ; div %rcx ; add %r9, %rax)
FIN
BEGIN(idiv64_dep)
    INITINT
    movabsq $0x123456789abcdef, %r9
    mov $7, %ecx
1:  R4(cqto ; idiv %rcx ; add %r9, %rax)
FIN
BEGIN(div128_64)
    movabsq $0x1000000000000000, %rcx
1:  mov $1, %edx
    movabsq $0x123456789abcdef, %rax
    div %rcx
FIN
BEGIN(div32w)
    mov $0x7fffffff, %ecx
1:  mov $0x12345, %edx
    mov $0x6789abcd, %eax
    div %ecx
FIN
BEGIN(idiv32w)
    mov $0x7fffffff, %ecx
1:  mov $0x9abcdef1, %eax
    cltd
    idiv %ecx
FIN
BEGIN(mul32w)
    INITINT
    mov $0x9e3779b1, %ecx
1:  R4(mul %ecx)
FIN
.p2align 4
_leaf_std:
    add $1, %rax
    ret $8
BEGIN(call_stdcall)
    xor %eax, %eax
1:  R8(push $1 ; call _leaf_std)
FIN
BEGIN(icall_mem)
    xor %eax, %eax
    lea _leaf_x(%rip), %rdx
    mov %rdx, 4096(%r8)
    lea 4096(%r8), %r9
1:  R8(call *(%r9))
FIN
