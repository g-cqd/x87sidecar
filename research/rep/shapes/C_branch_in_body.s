cbz x1, Lend
mov x22, #4
Lloop:
tbnz x1, #3, Lbig
ldr w23, [x6]
str w23, [x7]
b Ljoin
Lbig:
ldp q24, q25, [x6]
stp q24, q25, [x7]
Ljoin:
add x6, x6, x22
add x7, x7, x22
sub x1, x1, #1
cbnz x1, Lloop
Lend:
