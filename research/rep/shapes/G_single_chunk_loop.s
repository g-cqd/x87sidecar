cbz x1, Lend
Lloop:
ldp q24, q25, [x6]
stp q24, q25, [x7]
add x6, x6, #32
add x7, x7, #32
sub x1, x1, #8
cbnz x1, Lloop
Lend:
