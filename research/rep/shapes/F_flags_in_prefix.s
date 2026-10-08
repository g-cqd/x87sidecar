cbz x1, Lend
cmp x1, #8
mov x22, #4
Lloop:
ldr w23, [x6]
str w23, [x7]
add x6, x6, x22
add x7, x7, x22
sub x1, x1, #1
cbnz x1, Lloop
Lend:
