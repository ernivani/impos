# Declare constants for the multiboot header.
.set ALIGN,    1<<0             # align loaded modules on page boundaries
.set MEMINFO,  1<<1             # provide memory map
.set VIDEO,    1<<2             # request video mode info
.set FLAGS,    ALIGN | MEMINFO | VIDEO  # this is the Multiboot 'flag' field
.set MAGIC,    0x1BADB002       # 'magic number' lets bootloader find the header
.set CHECKSUM, -(MAGIC + FLAGS) # checksum of above, to prove we are multiboot

# Declare a header as in the Multiboot Standard.
.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

# Address fields (bit 16 not set, so these are ignored but must be present)
.long 0  # header_addr
.long 0  # load_addr
.long 0  # load_end_addr
.long 0  # bss_end_addr
.long 0  # entry_addr

# Video mode fields (hints only — actual resolution from multiboot info)
.long 0     # mode_type: 0 = linear graphics
.long 1920  # width hint
.long 1080  # height hint
.long 32    # depth hint (bits per pixel)

# Reserve a stack for the initial thread.
.section .bss
.align 16
stack_bottom:
.skip 16384 # 16 KiB
stack_top:

# The kernel entry point.
.section .text
.global _start
.type _start, @function
_start:
	movl $stack_top, %esp

	/* Enable SSE2 for non-temporal framebuffer writes */
	mov %cr0, %eax
	and $0xFFFFFFFB, %eax    /* Clear CR0.EM (bit 2) */
	or  $0x00000002, %eax    /* Set CR0.MP (bit 1) */
	mov %eax, %cr0
	mov %cr4, %eax
	or  $0x00000600, %eax    /* Set CR4.OSFXSR (9) + CR4.OSXMMEXCPT (10) */
	mov %eax, %cr4

	# Save multiboot info pointer (ebx) before _init clobbers it
	pushl %ebx

	# Call the global constructors.
	call _init

	# Restore multiboot info pointer and pass to kernel_main
	popl %ebx
	pushl %ebx
	call kernel_main
	addl $4, %esp

	# Hang if kernel_main unexpectedly returns.
	cli
1:	hlt
	jmp 1b
.size _start, . - _start
