/*
 * aarch64 CRT init epilogue.
 * GCC inserts crtend.o between crti.S and crtn.S.
 */
.section .init
    ldp     x29, x30, [sp], #16
    ret

.section .fini
    ldp     x29, x30, [sp], #16
    ret
