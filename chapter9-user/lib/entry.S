.section .text.entry
.globl _start
_start:
    /* The loader supplies argc in a0 and argv in a1, with a 16-byte aligned sp. */
    call main
    li a7, 93
    ecall
1:  j 1b
