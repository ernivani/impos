#ifndef _KERNEL_ELF_LOADER_H
#define _KERNEL_ELF_LOADER_H

#include <stdint.h>
#include <stddef.h>

/* ── ELF32 Header ────────────────────────────────────────────── */

#define EI_NIDENT   16
#define ELFMAG0     0x7F
#define ELFMAG1     'E'
#define ELFMAG2     'L'
#define ELFMAG3     'F'
#define ELFCLASS32  1
#define ELFDATA2LSB 1
#define ET_EXEC     2
#define EM_386      3

typedef struct __attribute__((packed)) {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

/* ── Program Header ──────────────────────────────────────────── */

#define PT_NULL  0
#define PT_LOAD  1

#define PF_X  0x1
#define PF_W  0x2
#define PF_R  0x4

typedef struct __attribute__((packed)) {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

/* ── API ─────────────────────────────────────────────────────── */

/* Check if data starts with ELF magic. Returns 1 if ELF, 0 otherwise. */
int elf_detect(const uint8_t *data, size_t size);

/* Load and run a static ELF32 binary from the filesystem.
 * Returns task ID on success, <0 on error. */
int elf_run(const char *filename);

#endif
