#!/usr/bin/env python3
"""REJECTED CANDIDATE (kept as evidence): generates a fast rep movs/stos shape with a size-dispatched
straight-line copy in the prefix before the stock element loop. The runtime classifier rejects it
(stores before the loop start count as architectural state modification; see shapes/A_prefix_store.s).
usage: gen_fastrep.py <movs|stos> <1|2|4|8> <a32|a64> [--stock] [--noret]"""
import sys

def gen(kind, e, a32, stock=False, capbytes=256, noret=False):
    loge = {1:0,2:1,4:2,8:3}[e]
    W = 'w' if a32 else 'x'
    ld = {1:('ldrb','w'),2:('ldrh','w'),4:('ldr','w'),8:('ldr','x')}[e]
    st = {1:('strb','w'),2:('strh','w'),4:('str','w'),8:('str','x')}[e]
    o = []
    A = o.append
    cap = capbytes // e
    k = cap.bit_length() - 1
    if not stock:
        A(f"cbz {W}1, Lend")
        A("tbnz x17, #1, Lslow")
        A(f"mov {W}22, {W}1")
        A("sub x23, x22, #1")
        A(f"lsr x23, x23, #{k}")
        A("cbnz x23, Lslow")
        A(f"lsl x24, x22, #{loge}")
        if kind == 'movs': A(f"mov {W}25, {W}6")
        A(f"mov {W}26, {W}7")
        if a32:
            regs = ['x25','x26'] if kind == 'movs' else ['x26']
            for r in regs:
                A(f"add x23, {r}, x24"); A("sub x23, x23, #1"); A("lsr x23, x23, #32"); A("cbnz x23, Lslow")
        if kind == 'movs':
            A("sub x23, x26, x25"); A("sub x27, x23, x24"); A("tbz x27, #63, Lfast")
            A("add x27, x23, x24"); A("sub x27, x27, #1"); A("tbz x27, #63, Lslow")
        A("Lfast:")
        if kind == 'stos':
            A({1:"dup v24.16b, w0",2:"dup v24.8h, w0",4:"dup v24.4s, w0",8:"dup v24.2d, x0"}[e])
        widths = [w for w in (128,64,32,16,8,4,2,1) if w >= e]
        for i, w in enumerate(widths[:-1]):
            sh = w.bit_length() - 1
            A(f"lsr x27, x24, #{sh}"); A(f"cbnz x27, C{w}")
        A(f"b C{widths[-1]}")
        for w in widths:
            A(f"C{w}:")
            if w == 128:
                A("sub x27, x24, #128")
                if kind == 'movs': A("add x28, x25, x27")
                A("add x29, x26, x27")
                if kind == 'movs':
                    for (bs,bd) in (('x25','x26'),('x28','x29')):
                        A(f"ldp q24, q25, [{bs}]"); A(f"ldp q26, q27, [{bs}, #32]"); A(f"ldp q28, q29, [{bs}, #64]"); A(f"ldp q30, q31, [{bs}, #96]")
                        A(f"stp q24, q25, [{bd}]"); A(f"stp q26, q27, [{bd}, #32]"); A(f"stp q28, q29, [{bd}, #64]"); A(f"stp q30, q31, [{bd}, #96]")
                else:
                    for bd in ('x26','x29'):
                        for off in (0,32,64,96):
                            A(f"stp q24, q24, [{bd}, #{off}]")
            elif w == 64:
                A("sub x27, x24, #64")
                if kind == 'movs':
                    A("add x28, x25, x27"); A("add x29, x26, x27")
                    A("ldp q24, q25, [x25]"); A("ldp q26, q27, [x25, #32]"); A("ldp q28, q29, [x28]"); A("ldp q30, q31, [x28, #32]")
                    A("stp q24, q25, [x26]"); A("stp q26, q27, [x26, #32]"); A("stp q28, q29, [x29]"); A("stp q30, q31, [x29, #32]")
                else:
                    A("add x29, x26, x27")
                    for bd in ('x26','x29'):
                        A(f"stp q24, q24, [{bd}]"); A(f"stp q24, q24, [{bd}, #32]")
            elif w == 32:
                A("sub x27, x24, #32")
                if kind == 'movs':
                    A("add x28, x25, x27"); A("add x29, x26, x27")
                    A("ldp q24, q25, [x25]"); A("ldp q26, q27, [x28]"); A("stp q24, q25, [x26]"); A("stp q26, q27, [x29]")
                else:
                    A("add x29, x26, x27"); A("stp q24, q24, [x26]"); A("stp q24, q24, [x29]")
            else:
                A(f"sub x27, x24, #{w}")
                if kind == 'movs':
                    if w == 16:
                        A("ldr q24, [x25]"); A("ldr q25, [x25, x27]"); A("str q24, [x26]"); A("str q25, [x26, x27]")
                    elif w == 8:
                        A("ldr x28, [x25]"); A("ldr x29, [x25, x27]"); A("str x28, [x26]"); A("str x29, [x26, x27]")
                    elif w == 4:
                        A("ldr w28, [x25]"); A("ldr w29, [x25, x27]"); A("str w28, [x26]"); A("str w29, [x26, x27]")
                    elif w == 2:
                        A("ldrh w28, [x25]"); A("ldrh w29, [x25, x27]"); A("strh w28, [x26]"); A("strh w29, [x26, x27]")
                    else:
                        A("ldrb w28, [x25]"); A("strb w28, [x26]")
                else:
                    r = {16:'q',8:'d',4:'s',2:'h',1:'b'}[w]
                    A(f"str {r}24, [x26]")
                    if w > 1: A(f"str {r}24, [x26, x27]")
            A("b Lend")
        A("Lslow:")
    else:
        A(f"cbz {W}1, Lend")
    A(f"mov w22, #{e}"); A("sbfx x24, x17, #1, #1"); A("ubfx w23, w17, #1, #1"); A("eor x22, x22, x24"); A("add x22, x22, x23")
    A("Lloop:")
    if kind == 'movs':
        if a32:
            A(f"mov w25, w6"); A(f"{ld[0]} w23, [x25]"); A("mov w25, w7"); A(f"{st[0]} w23, [x25]")
            A("add w6, w6, w22"); A("add w7, w7, w22")
        else:
            A(f"{ld[0]} {ld[1]}23, [x6]"); A(f"{st[0]} {st[1]}23, [x7]"); A("add x6, x6, x22"); A("add x7, x7, x22")
    else:
        if a32:
            A("mov w25, w7"); A(f"{st[0]} {'x' if e==8 else 'w'}0, [x25]"); A("add w7, w7, w22")
        else:
            A(f"{st[0]} {'x' if e==8 else 'w'}0, [x7]"); A("add x7, x7, x22")
    A(f"sub {W}1, {W}1, #1"); A(f"cbnz {W}1, Lloop")
    A("Lend:")
    if not stock:
        if kind == 'movs': A(f"add {W}6, {W}6, {W}1, lsl #{loge}")
        A(f"add {W}7, {W}7, {W}1, lsl #{loge}")
        A("mov x1, #0") if not a32 else A("mov w1, #0")
    if not noret: A("ret")
    return o

if __name__ == '__main__':
    kind, e, a = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    print(".text\n.globl _code\n_code:")
    print("\n".join(gen(kind, e, a == 'a32', '--stock' in sys.argv, noret='--noret' in sys.argv)))
