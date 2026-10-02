.text
.globl _code
_code:
cbz x1, Lend
mov w22, #4
sbfx x24, x17, #1, #1
ubfx w23, w17, #1, #1
eor x22, x22, x24
add x22, x22, x23
Lloop:
ldr w23, [x6]
str w23, [x7]
add x6, x6, x22
add x7, x7, x22
sub x1, x1, #1
cbnz x1, Lloop
Lend:
