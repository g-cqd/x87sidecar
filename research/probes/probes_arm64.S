// Native arm64 equivalents. x0=n, x1=buffer on entry. x19=loop counter, x20=buffer (callee-saved),
// x1=rax-analogue, x2=rdx-analogue, x3=rcx-analogue (shift count), x9=memory/table base.
.text
.macro BEGIN name
.globl _p_\name
.p2align 4
_p_\name:
    stp x29, x30, [sp, #-32]!
    stp x19, x20, [sp, #16]
    mov x19, x0
    mov x20, x1
.endm
.macro FIN
    subs x19, x19, #1
    b.ne 1b
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #32
    ret
.endm
.macro INITINT
    mov x1, #1
    movz x2, #0x7c15
    movk x2, #0x7f4a, lsl #16
    movk x2, #0x79b9, lsl #32
    movk x2, #0x9e37, lsl #48
    mov x3, #3
    mov x6, #0
.endm
.macro INITSS
    ldr s0, [x20, #64]
    ldr s1, [x20, #68]
    ldr s2, [x20, #72]
    ldr s3, [x20, #76]
    ldr s4, [x20, #80]
    mov w1, #1000
.endm
.macro INITSD
    ldr d0, [x20, #0]
    ldr d1, [x20, #8]
    ldr d2, [x20, #16]
    ldr d3, [x20, #24]
    ldr d4, [x20, #32]
    mov w1, #1000
.endm
.macro INITPS
    ldr q0, [x20, #128]
    ldr q1, [x20, #96]
    ldr q2, [x20, #112]
    ldr q4, [x20, #144]
    mov w1, #1
    mov x2, #0
.endm
.macro INITMEM off
    add x9, x20, #\off
    mov x1, #1
    mov x2, #5
    mov x6, #0
    mov x7, #0
.endm
.macro XORSHIFT
    eor x1, x1, x1, lsl #13
    eor x1, x1, x1, lsr #7
    eor x1, x1, x1, lsl #17
.endm

BEGIN empty
1:
FIN
BEGIN alu_chain
    INITINT
1:  .rept 2
    add x1, x1, #3
    eor x1, x1, x2
    sub x1, x1, #1
    and x1, x1, #0xfffffffffffffffe
    .endr
FIN
BEGIN alu32_chain
    INITINT
1:  .rept 2
    add w1, w1, #3
    eor w1, w1, w2
    sub w1, w1, #1
    and w1, w1, #0xfffffffe
    .endr
FIN
BEGIN alu_adc
    INITINT
1:  .rept 8
    adcs x1, x1, x2
    .endr
FIN
BEGIN alu_setcc
    INITINT
1:  .rept 4
    cmp x1, x2
    cset x6, lt
    add x1, x1, x6
    .endr
FIN
BEGIN alu_jcc_flags
    INITINT
1:  .rept 8
    adds x1, x1, x2
    b.vs 2f
2:
    .endr
FIN
BEGIN br_pred
    movz x1, #0x6789
    movk x1, #0x2345, lsl #16
    mov x9, #0
    mov x6, #0
1:  XORSHIFT
    cbnz x9, 2f
    add x6, x6, #1
2:
FIN
BEGIN br_unpred
    movz x1, #0x6789
    movk x1, #0x2345, lsl #16
    mov x9, #0
    mov x6, #0
1:  XORSHIFT
    tbz x1, #0, 2f
    add x6, x6, #1
2:
FIN

.p2align 4
_leaf_a:
    add x1, x1, #1
    ret
.p2align 4
_rec_a:
    cbz x12, 2f
    sub x12, x12, #1
    stp x29, x30, [sp, #-16]!
    bl _rec_a
    ldp x29, x30, [sp], #16
    add x1, x1, #1
2:  ret
BEGIN call_ret
    mov x1, #0
1:  .rept 8
    bl _leaf_a
    .endr
FIN
BEGIN rec64
    mov x1, #0
1:  mov x12, #64
    bl _rec_a
FIN
BEGIN rec1024
    mov x1, #0
1:  mov x12, #1024
    bl _rec_a
FIN

.macro IDX mode
 .if \mode == 0
    mov x4, #0
 .elseif \mode == 1
    and x4, x19, #7
 .else
    eor x6, x6, x6, lsl #13
    eor x6, x6, x6, lsr #7
    eor x6, x6, x6, lsl #17
    lsr x4, x6, #20
    and x4, x4, #7
 .endif
.endm
.macro IJMP name, mode
BEGIN \name
    adr x9, 5f
    movz x6, #0x6789
    movk x6, #0x2345, lsl #16
    mov x1, #0
1:  IDX \mode
    ldrsw x5, [x9, x4, lsl #2]
    add x5, x9, x5
    br x5
    .irp i,0,1,2,3,4,5,6,7
    .p2align 2
10\i:
    add x1, x1, #1
    b 4f
    .endr
4:  FIN
    .p2align 2
5:
    .long 100b-5b, 101b-5b, 102b-5b, 103b-5b, 104b-5b, 105b-5b, 106b-5b, 107b-5b
.endm
IJMP ijmp_mono, 0
IJMP ijmp_seq8, 1
IJMP ijmp_rand8, 2
.macro ICALL name, mode
BEGIN \name
    adr x9, 5f
    movz x6, #0x6789
    movk x6, #0x2345, lsl #16
    mov x1, #0
1:  IDX \mode
    ldrsw x5, [x9, x4, lsl #2]
    add x5, x9, x5
    blr x5
    FIN
    .irp i,0,1,2,3,4,5,6,7
    .p2align 2
10\i:
    add x1, x1, #1
    ret
    .endr
    .p2align 2
5:
    .long 100b-5b, 101b-5b, 102b-5b, 103b-5b, 104b-5b, 105b-5b, 106b-5b, 107b-5b
.endm
ICALL icall_mono, 0
ICALL icall_seq8, 1
ICALL icall_rand8, 2

BEGIN sse_ss_addmul
    INITSS
1:  .rept 4
    fadd s0, s0, s1
    fmul s0, s0, s2
    .endr
FIN
BEGIN sse_ss_sqrt
    INITSS
    fmov s0, s3
1:  .rept 8
    fsqrt s0, s0
    .endr
FIN
BEGIN sse_ss_div
    INITSS
1:  .rept 4
    fdiv s0, s0, s4
    .endr
FIN
BEGIN sse_ss_cvt
    INITSS
1:  .rept 4
    scvtf s0, w1
    fcvtzs w1, s0
    add w1, w1, #1
    .endr
FIN
BEGIN sse_ss_comiss
    INITSS
1:  .rept 8
    fcmp s0, s1
    b.hi 2f
2:
    .endr
FIN
BEGIN sse_sd_addmul
    INITSD
1:  .rept 4
    fadd d0, d0, d1
    fmul d0, d0, d2
    .endr
FIN
BEGIN sse_sd_sqrt
    INITSD
    fmov d0, d3
1:  .rept 8
    fsqrt d0, d0
    .endr
FIN
BEGIN sse_sd_div
    INITSD
1:  .rept 4
    fdiv d0, d0, d4
    .endr
FIN
BEGIN sse_sd_cvt
    INITSD
1:  .rept 4
    scvtf d0, w1
    fcvtzs w1, d0
    add w1, w1, #1
    .endr
FIN
BEGIN sse_sd_comisd
    INITSD
1:  .rept 8
    fcmp d0, d1
    b.hi 2f
2:
    .endr
FIN
BEGIN ps_addmul
    INITPS
1:  .rept 4
    fadd v0.4s, v0.4s, v1.4s
    fmul v0.4s, v0.4s, v2.4s
    .endr
FIN
BEGIN ps_shufps
    INITPS
1:  .rept 8
    rev64 v0.4s, v0.4s
    ext v0.16b, v0.16b, v0.16b, #8
    .endr
FIN
BEGIN pi_pshufd
    INITPS
1:  .rept 8
    ext v0.16b, v0.16b, v0.16b, #8
    .endr
FIN
BEGIN pi_paddd
    INITPS
1:  .rept 8
    add v0.4s, v0.4s, v1.4s
    .endr
FIN
BEGIN ps_movaps
    INITPS
1:  .rept 4
    mov v1.16b, v0.16b
    mov v0.16b, v1.16b
    .endr
FIN
BEGIN ps_movmsk
    INITPS
1:  .rept 4
    ushr v3.4s, v0.4s, #31
    ushl v3.4s, v3.4s, v4.4s
    addv s3, v3.4s
    fmov w6, s3
    add w2, w2, w6
    .endr
FIN

BEGIN mem_load
    INITMEM 4096
1:  .rept 2
    ldr x1, [x9, #0]
    ldr x2, [x9, #8]
    ldr x6, [x9, #16]
    ldr x7, [x9, #24]
    .endr
FIN
BEGIN mem_store
    INITMEM 4096
1:  .rept 2
    str x1, [x9, #0]
    str x2, [x9, #8]
    str x1, [x9, #16]
    str x2, [x9, #24]
    .endr
FIN
BEGIN mem_load_unal
    INITMEM 4096
1:  .rept 2
    ldur x1, [x9, #1]
    ldur x2, [x9, #9]
    ldur x6, [x9, #17]
    ldur x7, [x9, #57]
    .endr
FIN
BEGIN mem_ldop
    INITMEM 4096
1:  .rept 8
    ldr x6, [x9, #0]
    add x1, x1, x6
    .endr
FIN
BEGIN mem_rmw
    INITMEM 4096
1:  .rept 4
    ldr x6, [x9]
    add x6, x6, x2
    str x6, [x9]
    .endr
FIN
BEGIN mem_fwd_ok
    INITMEM 4096
1:  .rept 4
    str x1, [x9]
    ldr x1, [x9]
    add x1, x1, #1
    .endr
FIN
BEGIN mem_fwd_bad
    INITMEM 4096
1:  .rept 4
    strb w1, [x9]
    ldr w1, [x9]
    add w1, w1, #1
    .endr
FIN
BEGIN mem_stride
    INITMEM 8192
    mov x2, #0
1:  .rept 4
    ldr x1, [x9, x2]
    add w2, w2, #64
    and w2, w2, #0x3ffc0
    .endr
FIN

.macro RP name, cnt, fn, zero
BEGIN \name
1:  add x0, x20, #8192
    add x1, x20, #4096
    .if \zero
    mov w1, #0
    .endif
    mov x2, #\cnt
    bl \fn
FIN
.endm
RP rep_movsb8, 8, _memcpy, 0
RP rep_movsb64, 64, _memcpy, 0
RP rep_movsb1k, 1024, _memcpy, 0
RP rep_movsd64, 64, _memcpy, 0
RP rep_stosd64, 64, _memset, 1
RP rep_stosb1k, 1024, _memset, 1

BEGIN rdtsc
1:  mrs x1, cntvct_el0
FIN

BEGIN part_al
    INITINT
1:  .rept 4
    bfxil x1, x2, #0, #8
    add x2, x2, x1
    .endr
FIN
BEGIN part_ah
    INITINT
1:  .rept 4
    bfi x1, x2, #8, #8
    add x2, x2, x1
    .endr
FIN
BEGIN part_ax
    INITINT
1:  .rept 4
    bfxil x1, x2, #0, #16
    add x2, x2, x1
    .endr
FIN
BEGIN movzx_sx
    INITINT
1:  .rept 4
    uxtb w2, w1
    sxtb w1, w2
    uxth w2, w1
    sxth w1, w2
    .endr
FIN
BEGIN lea_simple
    INITINT
1:  .rept 8
    add x1, x1, #8
    .endr
FIN
BEGIN lea_scaled
    INITINT
1:  .rept 8
    add x1, x1, x2, lsl #2
    .endr
FIN
BEGIN lea_complex
    INITINT
1:  .rept 8
    add x1, x1, x2, lsl #1
    add x1, x1, #4
    .endr
FIN
BEGIN lea32
    INITINT
1:  .rept 8
    add w1, w1, w2, lsl #1
    add w1, w1, #4
    .endr
FIN

BEGIN shl_cl_dead
    INITINT
1:  .rept 4
    lsl x1, x1, x3
    orr x1, x1, x2
    .endr
FIN
BEGIN shl_cl_flag
    INITINT
1:  .rept 4
    neg x4, x3
    lsr x5, x1, x4
    and x5, x5, #1
    lsl x1, x1, x3
    add x1, x1, x2
    add x1, x1, x5
    .endr
FIN
BEGIN rol_ror
    INITINT
1:  .rept 4
    ror x1, x1, #59
    ror x2, x2, x3
    .endr
FIN
BEGIN shld
    INITINT
1:  .rept 8
    extr x1, x1, x2, #59
    .endr
FIN
BEGIN bts_jc
    INITINT
    mov x7, #1
1:  .rept 4
    lsl x4, x7, x3
    tst x1, x4
    orr x1, x1, x4
    b.ne 2f
2:
    .endr
FIN
BEGIN imul
    INITINT
    orr x2, x2, #1
1:  .rept 8
    mul x1, x1, x2
    .endr
FIN
BEGIN mul128
    INITINT
    movz x3, #0x7c15
    movk x3, #0x7f4a, lsl #16
    movk x3, #0x79b9, lsl #32
    movk x3, #0x9e37, lsl #48
1:  .rept 4
    umulh x2, x1, x3
    mul x1, x1, x3
    .endr
FIN
BEGIN div32
    mov x3, #7
1:  mov w1, #0x5678
    movk w1, #0x1234, lsl #16
    udiv w6, w1, w3
FIN
BEGIN div64
    mov x3, #7
1:  movz x1, #0xcdef
    movk x1, #0x89ab, lsl #16
    movk x1, #0x4567, lsl #32
    movk x1, #0x0123, lsl #48
    udiv x6, x1, x3
FIN
BEGIN idiv64
    mov x3, #7
1:  movz x1, #0xcdef
    movk x1, #0x89ab, lsl #16
    movk x1, #0x4567, lsl #32
    movk x1, #0x0123, lsl #48
    neg x1, x1
    sdiv x6, x1, x3
FIN
BEGIN cmov
    INITINT
1:  .rept 4
    cmp x1, x2
    csel x1, x2, x1, lt
    sub x1, x1, #5
    .endr
FIN
BEGIN bsr
    INITINT
    mov x7, #63
1:  .rept 4
    clz x4, x1
    sub x2, x7, x4
    add x1, x1, x2
    orr x1, x1, #1
    .endr
FIN
BEGIN popcnt
    INITINT
1:  .rept 4
    fmov d0, x1
    cnt v0.8b, v0.8b
    addv b0, v0.8b
    fmov w2, s0
    add x1, x1, x2
    .endr
FIN

BEGIN x87_chain
1:  .rept 4
    ldr d0, [x20]
    ldr d1, [x20, #8]
    fadd d0, d0, d1
    ldr d1, [x20, #16]
    fmul d0, d0, d1
    str d0, [x20]
    .endr
FIN
BEGIN x87_fcomi
1:  .rept 4
    ldr d0, [x20]
    ldr d1, [x20, #8]
    fcmp d1, d0
    b.lt 2f
2:
    .endr
FIN

BEGIN lock_xadd
    INITMEM 4096
1:  .rept 4
    ldaddal x1, x6, [x9]
    .endr
FIN
BEGIN lock_cmpxchg
    INITMEM 4096
1:  .rept 4
    casal x1, x2, [x9]
    .endr
FIN
BEGIN xchg_mem
    INITMEM 4096
1:  .rept 4
    swpal x1, x1, [x9]
    .endr
FIN
BEGIN mfence
1:  .rept 4
    dmb ish
    .endr
FIN

BEGIN push_pop
    INITINT
1:  .rept 4
    stp x1, x2, [sp, #-16]!
    ldp x1, x2, [sp], #16
    .endr
FIN
BEGIN frame
    INITINT
1:  .rept 4
    stp x29, x30, [sp, #-48]!
    mov x29, sp
    str x1, [x29, #16]
    ldr x1, [x29, #16]
    ldp x29, x30, [sp], #48
    .endr
FIN
BEGIN pushf_popf
    INITINT
1:  .rept 4
    mrs x4, nzcv
    msr nzcv, x4
    .endr
FIN

BEGIN sse_ss_minmax
    INITSS
1:  .rept 4
    fcmp s0, s1
    fcsel s0, s0, s1, mi
    fcmp s0, s2
    fcsel s0, s0, s2, gt
    .endr
FIN
BEGIN ps_minmax
    INITPS
1:  .rept 4
    fcmgt v3.4s, v1.4s, v0.4s
    bsl v3.16b, v0.16b, v1.16b
    mov v0.16b, v3.16b
    fcmgt v3.4s, v0.4s, v2.4s
    bsl v3.16b, v0.16b, v2.16b
    mov v0.16b, v3.16b
    .endr
FIN
BEGIN ps_cvt_i2f
    INITPS
    ldr q0, [x20, #144]
1:  .rept 4
    scvtf v0.4s, v0.4s
    fcvtzs v0.4s, v0.4s
    .endr
FIN
BEGIN ps_cvt_pspd
    INITPS
1:  .rept 4
    fcvtl v0.2d, v0.2s
    fcvtn v0.2s, v0.2d
    .endr
FIN
BEGIN ps_cmp_sel
    INITPS
1:  .rept 4
    fcmgt v3.4s, v1.4s, v0.4s
    and v0.16b, v0.16b, v3.16b
    .endr
FIN
BEGIN ps_rcpps
    INITPS
1:  .rept 4
    frecpe v3.4s, v0.4s
    fadd v0.4s, v0.4s, v3.4s
    .endr
FIN
BEGIN ps_rsqrtps
    INITPS
1:  .rept 4
    frsqrte v3.4s, v0.4s
    fadd v0.4s, v0.4s, v3.4s
    .endr
FIN
BEGIN ps_sqrtps
    INITPS
1:  .rept 4
    fsqrt v0.4s, v0.4s
    .endr
FIN
BEGIN ps_divps
    INITPS
1:  .rept 4
    fdiv v0.4s, v0.4s, v2.4s
    .endr
FIN
BEGIN ps_unpcklps
    INITPS
1:  .rept 8
    zip1 v0.4s, v0.4s, v1.4s
    .endr
FIN
BEGIN ps_movlhps
    INITPS
1:  .rept 8
    ins v0.d[1], v1.d[0]
    .endr
FIN
BEGIN ps_movshdup
    INITPS
1:  .rept 8
    trn2 v0.4s, v0.4s, v0.4s
    .endr
FIN
BEGIN ps_insertps
    INITPS
1:  .rept 8
    ins v0.s[1], v1.s[0]
    .endr
FIN
BEGIN ps_haddps
    INITPS
1:  .rept 8
    faddp v0.4s, v0.4s, v1.4s
    .endr
FIN
BEGIN ps_dpps
    INITPS
1:  .rept 4
    fmul v3.4s, v0.4s, v1.4s
    faddp v3.4s, v3.4s, v3.4s
    faddp s3, v3.2s
    mov v0.s[0], v3.s[0]
    .endr
FIN
BEGIN pi_pmulld
    INITPS
1:  .rept 8
    mul v0.4s, v0.4s, v1.4s
    .endr
FIN
BEGIN pi_pshufb
    INITPS
1:  .rept 8
    tbl v0.16b, {v0.16b}, v1.16b
    .endr
FIN
BEGIN pi_pcmpeq_movmsk
    INITPS
    movz x6, #0x0201
    movk x6, #0x0804, lsl #16
    movk x6, #0x2010, lsl #32
    movk x6, #0x8040, lsl #48
    dup v5.2d, x6
1:  .rept 4
    cmeq v3.16b, v0.16b, v1.16b
    and v3.16b, v3.16b, v5.16b
    ext v6.16b, v3.16b, v3.16b, #8
    zip1 v3.16b, v3.16b, v6.16b
    addv h3, v3.8h
    fmov w6, s3
    add w2, w2, w6
    .endr
FIN
BEGIN pi_punpcklbw
    INITPS
1:  .rept 8
    zip1 v0.16b, v0.16b, v1.16b
    .endr
FIN
BEGIN sse_movd_gpr
    INITPS
1:  .rept 4
    fmov s0, w1
    fmov w1, s0
    add w1, w1, #1
    .endr
FIN
BEGIN sse_ucomiss_jp
    INITSS
1:  .rept 8
    fcmp s0, s1
    b.vs 2f
    b.ne 2f
2:
    .endr
FIN
BEGIN div64_dep
    INITINT
    movz x9, #0xcdef
    movk x9, #0x89ab, lsl #16
    movk x9, #0x4567, lsl #32
    movk x9, #0x0123, lsl #48
    mov x3, #7
1:  .rept 4
    udiv x1, x1, x3
    add x1, x1, x9
    .endr
FIN
BEGIN idiv64_dep
    INITINT
    movz x9, #0xcdef
    movk x9, #0x89ab, lsl #16
    movk x9, #0x4567, lsl #32
    movk x9, #0x0123, lsl #48
    mov x3, #7
1:  .rept 4
    sdiv x1, x1, x3
    add x1, x1, x9
    .endr
FIN
BEGIN div32w
    mov x3, #0x7fffffff
1:  mov x1, #0x6789
    movk x1, #0xabcd, lsl #16
    mov x6, #0x2345
    movk x6, #0x1, lsl #16
    orr x1, x1, x6, lsl #32
    udiv x4, x1, x3
FIN
BEGIN idiv32w
    mov x3, #0x7fffffff
1:  mov w1, #0xdef1
    movk w1, #0x9abc, lsl #16
    sxtw x1, w1
    sdiv x4, x1, x3
FIN
BEGIN mul32w
    INITINT
    mov w3, #0x79b1
    movk w3, #0x9e37, lsl #16
1:  .rept 4
    umull x4, w1, w3
    mov w1, w4
    lsr x2, x4, #32
    .endr
FIN
.p2align 4
_leaf_s:
    add x1, x1, #1
    ret
BEGIN call_stdcall
    mov x1, #0
1:  .rept 8
    mov x0, #1
    bl _leaf_s
    .endr
FIN
BEGIN icall_mem
    mov x1, #0
    adr x5, _leaf_a
    add x9, x20, #4096
    str x5, [x9]
1:  .rept 8
    ldr x5, [x9]
    blr x5
    .endr
FIN
