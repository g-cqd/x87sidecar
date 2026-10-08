cbz x1, Lend
Lloop:
ldr w23, [x6], #4
str w23, [x7]
add x7, x7, #4
sub x1, x1, #1
cbnz x1, Lloop
Lend:
