cbz x1, Lend
mov x22, #4
Lloop:
ldr w23, [x6]
str w23, [x7]
add x6, x6, x22
add x7, x7, x22
sub x1, x1, #1
cbnz x1, Lloop
Lend:
ldr w23, [x6]
str w23, [x7]
add x6, x6, #4
add x7, x7, #4
