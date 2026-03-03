/*
 * aarch64 linker script for ImposOS on QEMU virt.
 *
 * QEMU virt loads -kernel images at 0x40000000 (start of RAM).
 * The .text.boot section must come first — it contains _start.
 */
ENTRY(_start)

SECTIONS
{
    . = 0x40000000;

    /* Boot entry must be at the very start of the image */
    .text.boot BLOCK(4K) : ALIGN(4K)
    {
        *(.text.boot)
    }

    .text BLOCK(4K) : ALIGN(4K)
    {
        *(.text .text.*)
    }

    .rodata BLOCK(4K) : ALIGN(4K)
    {
        *(.rodata .rodata.*)
    }

    /* Global constructor/destructor tables */
    .init_array BLOCK(4K) : ALIGN(4K)
    {
        __init_array_start = .;
        *(.init_array)
        __init_array_end = .;
    }

    .fini_array BLOCK(4K) : ALIGN(4K)
    {
        __fini_array_start = .;
        *(.fini_array)
        __fini_array_end = .;
    }

    .data BLOCK(4K) : ALIGN(4K)
    {
        *(.data .data.*)
    }

    .bss BLOCK(4K) : ALIGN(4K)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        __bss_end = .;
    }

    /* Heap starts right after BSS, page-aligned */
    . = ALIGN(4K);
    _heap_start = .;
}
