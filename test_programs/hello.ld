/* Linker script for minimal Linux i386 static ELF binary.
 * Load address 0x08048000 — standard Linux default for ET_EXEC. */

ENTRY(_start)

SECTIONS
{
    . = 0x08048000;

    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
