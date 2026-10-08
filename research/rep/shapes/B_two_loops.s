cbz x1, Lend
mov x22, #4
Lchunk:
ldp q24, q25, [x6]
stp q24, q25, [x7]
add x6, x6, #32
add x7, x7, #32
sub x1, x1, #8
cbnz x1, Lchunk
Ltail:
ldr w23, [x6]
str w23, [x7]
add x6, x6, x22
add x7, x7, x22
sub x1, x1, #1
cbnz x1, Ltail
Lend:
