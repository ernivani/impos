/*
 * gen_test_exe.c — Generate a minimal PE32 .exe for testing the ImposOS PE loader.
 *
 * Creates a tiny Windows console executable that:
 *   - Imports puts() from msvcrt.dll
 *   - Prints "Hello from Win32!"
 *   - Calls ExitProcess(0) from kernel32.dll
 *   - Includes base relocation table so it can load at any address
 *
 * Build: gcc -o gen_test_exe gen_test_exe.c
 * Run:   ./gen_test_exe > hello.exe
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint8_t image[16384];

static void w8(int off, uint8_t v)  { image[off] = v; }
static void w16(int off, uint16_t v) { image[off] = v & 0xFF; image[off+1] = (v>>8) & 0xFF; }
static void w32(int off, uint32_t v) { image[off] = v & 0xFF; image[off+1] = (v>>8) & 0xFF; image[off+2] = (v>>16) & 0xFF; image[off+3] = (v>>24) & 0xFF; }
static void wstr(int off, const char *s) { while (*s) image[off++] = *s++; }

/*
 * Layout (3 sections: .text, .rdata, .reloc):
 *   0x0000 - DOS header (64 bytes)
 *   0x0040 - PE signature + COFF + Optional header
 *   0x0160 - Section headers (3 sections * 40 bytes = 120 bytes)
 *   0x0200 - .text  (code)
 *   0x0400 - .rdata (imports, strings)
 *   0x0600 - .reloc (base relocations)
 *
 * Virtual layout (ImageBase = 0x00400000):
 *   0x1000 - .text
 *   0x2000 - .rdata
 *   0x3000 - .reloc
 */

#define IMAGE_BASE    0x00400000
#define SECT_ALIGN    0x1000
#define FILE_ALIGN    0x0200

#define TEXT_RVA      0x1000
#define TEXT_FOFF     0x0200
#define RDATA_RVA     0x2000
#define RDATA_FOFF    0x0400
#define RELOC_RVA     0x3000
#define RELOC_FOFF    0x0600

/* Import table layout within .rdata (same as before) */
#define IMP_DESC_OFF      0
#define MSVCRT_NAME_OFF   64
#define KERNEL32_NAME_OFF 80
#define ILT_MSVCRT_OFF    96
#define ILT_KERNEL32_OFF  104
#define IAT_MSVCRT_OFF    112
#define IAT_KERNEL32_OFF  120
#define HN_PUTS_OFF       128
#define HN_EXIT_OFF       136
#define HELLO_STR_OFF     152

int main(void) {
    memset(image, 0, sizeof(image));

    /* ── DOS Header ── */
    w16(0x00, 0x5A4D);       /* "MZ" */
    w32(0x3C, 0x40);         /* e_lfanew */

    /* ── PE Signature ── */
    w32(0x40, 0x00004550);   /* "PE\0\0" */

    /* ── COFF Header (at 0x44) ── */
    int coff = 0x44;
    w16(coff+0,  0x014C);    /* Machine = i386 */
    w16(coff+2,  3);         /* NumberOfSections = 3 */
    w16(coff+16, 0xE0);      /* SizeOfOptionalHeader */
    w16(coff+18, 0x0102);    /* Characteristics: EXECUTABLE | 32BIT */

    /* ── Optional Header PE32 (at 0x58) ── */
    int opt = 0x58;
    w16(opt+0,  0x010B);     /* Magic = PE32 */
    w8 (opt+2,  1);          /* Linker major */
    w32(opt+4,  0x200);      /* SizeOfCode */
    w32(opt+8,  0x400);      /* SizeOfInitializedData */
    w32(opt+16, TEXT_RVA);   /* AddressOfEntryPoint */
    w32(opt+20, TEXT_RVA);   /* BaseOfCode */
    w32(opt+24, RDATA_RVA);  /* BaseOfData */
    w32(opt+28, IMAGE_BASE); /* ImageBase */
    w32(opt+32, SECT_ALIGN); /* SectionAlignment */
    w32(opt+36, FILE_ALIGN); /* FileAlignment */
    w16(opt+40, 4);          /* OS version major */
    w16(opt+48, 4);          /* Subsystem version */
    w32(opt+56, 0x5000);     /* SizeOfImage (5 pages) */
    w32(opt+60, 0x200);      /* SizeOfHeaders */
    w16(opt+68, 3);          /* Subsystem = WINDOWS_CUI (console) */
    w32(opt+72, 0x100000);   /* SizeOfStackReserve */
    w32(opt+76, 0x1000);     /* SizeOfStackCommit */
    w32(opt+80, 0x100000);   /* SizeOfHeapReserve */
    w32(opt+84, 0x1000);     /* SizeOfHeapCommit */
    w32(opt+92, 16);         /* NumberOfRvaAndSizes */

    /* Data directories */
    /* [1] Import directory */
    w32(opt+96+1*8,   RDATA_RVA + IMP_DESC_OFF);
    w32(opt+96+1*8+4, 60);  /* 3 descriptors * 20 bytes */
    /* [5] Base relocation */
    w32(opt+96+5*8,   RELOC_RVA);
    w32(opt+96+5*8+4, 16);  /* block header(8) + 3 entries(6) + padding(2) = 16 */
    /* [12] IAT */
    w32(opt+96+12*8,  RDATA_RVA + IAT_MSVCRT_OFF);
    w32(opt+96+12*8+4, 16);

    /* ── Section Headers (at 0x138) ── */
    /* .text */
    int sh1 = 0x138;
    wstr(sh1, ".text");
    w32(sh1+8,  0x200);       /* VirtualSize */
    w32(sh1+12, TEXT_RVA);
    w32(sh1+16, 0x200);       /* SizeOfRawData */
    w32(sh1+20, TEXT_FOFF);
    w32(sh1+36, 0x60000020);  /* CODE|EXECUTE|READ */

    /* .rdata */
    int sh2 = sh1 + 40;
    wstr(sh2, ".rdata");
    w32(sh2+8,  0x200);
    w32(sh2+12, RDATA_RVA);
    w32(sh2+16, 0x200);
    w32(sh2+20, RDATA_FOFF);
    w32(sh2+36, 0x40000040);  /* INITIALIZED|READ */

    /* .reloc */
    int sh3 = sh2 + 40;
    wstr(sh3, ".reloc");
    w32(sh3+8,  0x200);
    w32(sh3+12, RELOC_RVA);
    w32(sh3+16, 0x200);
    w32(sh3+20, RELOC_FOFF);
    w32(sh3+36, 0x42000040);  /* INITIALIZED|READ|DISCARDABLE */

    /* ── .text section (code at 0x200) ── */
    int code = TEXT_FOFF;
    uint32_t abs_hello    = IMAGE_BASE + RDATA_RVA + HELLO_STR_OFF;
    uint32_t abs_iat_puts = IMAGE_BASE + RDATA_RVA + IAT_MSVCRT_OFF;
    uint32_t abs_iat_exit = IMAGE_BASE + RDATA_RVA + IAT_KERNEL32_OFF;

    /* Track relocation offsets within .text (RVA-relative to page 0x1000) */
    int reloc_offsets[3];
    int nrelocs = 0;

    /* push <hello_string_addr> */
    image[code++] = 0x68;  /* push imm32 */
    reloc_offsets[nrelocs++] = (code - TEXT_FOFF);  /* offset within .text of the imm32 */
    w32(code, abs_hello); code += 4;

    /* call dword ptr [IAT_puts] */
    image[code++] = 0xFF;
    image[code++] = 0x15;
    reloc_offsets[nrelocs++] = (code - TEXT_FOFF);
    w32(code, abs_iat_puts); code += 4;

    /* add esp, 4 */
    image[code++] = 0x83;
    image[code++] = 0xC4;
    image[code++] = 0x04;

    /* push 0 */
    image[code++] = 0x6A;
    image[code++] = 0x00;

    /* call dword ptr [IAT_ExitProcess] */
    image[code++] = 0xFF;
    image[code++] = 0x15;
    reloc_offsets[nrelocs++] = (code - TEXT_FOFF);
    w32(code, abs_iat_exit); code += 4;

    /* int3 (safety) */
    image[code++] = 0xCC;

    /* ── .rdata section (at 0x400) ── */
    int rdata = RDATA_FOFF;

    /* Import descriptors */
    w32(rdata + IMP_DESC_OFF + 0,  RDATA_RVA + ILT_MSVCRT_OFF);
    w32(rdata + IMP_DESC_OFF + 12, RDATA_RVA + MSVCRT_NAME_OFF);
    w32(rdata + IMP_DESC_OFF + 16, RDATA_RVA + IAT_MSVCRT_OFF);

    w32(rdata + IMP_DESC_OFF + 20, RDATA_RVA + ILT_KERNEL32_OFF);
    w32(rdata + IMP_DESC_OFF + 32, RDATA_RVA + KERNEL32_NAME_OFF);
    w32(rdata + IMP_DESC_OFF + 36, RDATA_RVA + IAT_KERNEL32_OFF);
    /* Null terminator descriptor at +40..+59 (already zero) */

    /* DLL names */
    wstr(rdata + MSVCRT_NAME_OFF, "msvcrt.dll");
    wstr(rdata + KERNEL32_NAME_OFF, "kernel32.dll");

    /* ILT and IAT entries */
    w32(rdata + ILT_MSVCRT_OFF,     RDATA_RVA + HN_PUTS_OFF);
    w32(rdata + ILT_KERNEL32_OFF,   RDATA_RVA + HN_EXIT_OFF);
    w32(rdata + IAT_MSVCRT_OFF,     RDATA_RVA + HN_PUTS_OFF);
    w32(rdata + IAT_KERNEL32_OFF,   RDATA_RVA + HN_EXIT_OFF);

    /* Hint/Name entries */
    w16(rdata + HN_PUTS_OFF, 0);
    wstr(rdata + HN_PUTS_OFF + 2, "puts");
    w16(rdata + HN_EXIT_OFF, 0);
    wstr(rdata + HN_EXIT_OFF + 2, "ExitProcess");

    /* Hello string */
    wstr(rdata + HELLO_STR_OFF, "Hello from Win32!");

    /* ── .reloc section (at 0x600) ── */
    /*
     * Base relocation block for .text page (RVA 0x1000):
     *   - Header: PageRVA(4) + BlockSize(4)
     *   - Entries: type(4 bits) + offset(12 bits), each 2 bytes
     *   - 3 HIGHLOW relocations + 1 ABSOLUTE padding = 8 bytes of entries
     *   - Total: 8 + 8 = 16 bytes
     */
    int reloc = RELOC_FOFF;
    w32(reloc + 0, TEXT_RVA);   /* PageRVA = 0x1000 (.text) */
    w32(reloc + 4, 16);         /* BlockSize = 16 bytes */

    for (int i = 0; i < nrelocs; i++) {
        /* Type 3 (HIGHLOW) in top 4 bits, offset in low 12 bits */
        uint16_t entry = (3 << 12) | (reloc_offsets[i] & 0xFFF);
        w16(reloc + 8 + i*2, entry);
    }
    /* Entry [3] = ABSOLUTE (padding to align to 4 bytes) — already 0 */

    /* ── Write image ── */
    int img_size = RELOC_FOFF + 0x200;  /* 0x800 = 2048 bytes */
    fwrite(image, 1, img_size, stdout);

    return 0;
}
