#if defined(__i386__)

/* ── i386 setjmp/longjmp ─────────────────────────────────────────── */
.section .text
.global setjmp
.type setjmp, @function
setjmp:
	movl 4(%esp), %edx      # Get jmp_buf pointer from stack
	movl %ebx, 0(%edx)      # Save ebx
	movl %esi, 4(%edx)      # Save esi
	movl %edi, 8(%edx)      # Save edi
	movl %ebp, 12(%edx)     # Save ebp
	leal 4(%esp), %ecx      # Get esp (before call)
	movl %ecx, 16(%edx)     # Save esp
	movl (%esp), %ecx       # Get return address
	movl %ecx, 20(%edx)     # Save eip
	xorl %eax, %eax         # Return 0
	ret

.global longjmp
.type longjmp, @function
longjmp:
	movl 4(%esp), %edx      # Get jmp_buf pointer
	movl 8(%esp), %eax      # Get return value
	testl %eax, %eax        # If val is 0
	jnz 1f
	incl %eax               # Make it 1
1:
	movl 0(%edx), %ebx      # Restore ebx
	movl 4(%edx), %esi      # Restore esi
	movl 8(%edx), %edi      # Restore edi
	movl 12(%edx), %ebp     # Restore ebp
	movl 16(%edx), %esp     # Restore esp
	movl 20(%edx), %ecx     # Get saved eip
	jmp *%ecx               # Jump to saved location

#elif defined(__aarch64__)

/* ── aarch64 setjmp/longjmp ──────────────────────────────────────── */
/* Saves callee-saved registers: x19-x30, SP, (d8-d15 omitted since
   we build with -mgeneral-regs-only).
   jmp_buf layout: x19..x30 (12 regs) + SP = 13 × 8 = 104 bytes */

.section .text
.global setjmp
.type setjmp, %function
setjmp:
	stp x19, x20, [x0, #0]
	stp x21, x22, [x0, #16]
	stp x23, x24, [x0, #32]
	stp x25, x26, [x0, #48]
	stp x27, x28, [x0, #64]
	stp x29, x30, [x0, #80]   /* x30 = LR (return address) */
	mov x1, sp
	str x1, [x0, #96]          /* Save SP */
	mov w0, #0                  /* Return 0 */
	ret

.global longjmp
.type longjmp, %function
longjmp:
	ldp x19, x20, [x0, #0]
	ldp x21, x22, [x0, #16]
	ldp x23, x24, [x0, #32]
	ldp x25, x26, [x0, #48]
	ldp x27, x28, [x0, #64]
	ldp x29, x30, [x0, #80]
	ldr x2, [x0, #96]
	mov sp, x2
	cmp w1, #0
	csinc w0, w1, wzr, ne     /* w0 = (w1 != 0) ? w1 : 1 */
	ret                        /* Returns to saved LR */

#else
#error "setjmp: unsupported architecture"
#endif
