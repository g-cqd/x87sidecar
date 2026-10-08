cbz x1, Lend
mov x22, #4
Lloop:
ldr w23, [x6]
str w23, [x7]
ldr w23, [x6, #4]
str w23, [x7, #4]
ldr w23, [x6, #8]
str w23, [x7, #8]
ldr w23, [x6, #12]
str w23, [x7, #12]
add x6, x6, #16
add x7, x7, #16
sub x1, x1, #4
cbnz x1, Lloop
Lend:
