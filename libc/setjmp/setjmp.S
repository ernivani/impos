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
