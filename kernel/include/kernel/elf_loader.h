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
#define ET_DYN      3   /* Shared object / PIE executable */
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

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_PHDR    6

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

/* ── Auxiliary Vector Types ───────────────────────────────────── */

#define AT_NULL    0
#define AT_PHDR    3
#define AT_PHENT   4
#define AT_PHNUM   5
#define AT_PAGESZ  6
#define AT_BASE    7
#define AT_ENTRY   9
#define AT_UID     11
#define AT_EUID    12
#define AT_GID     13
#define AT_EGID    14
#define AT_HWCAP   16
#define AT_CLKTCK  17
#define AT_SECURE  23
#define AT_RANDOM  25

typedef struct {
    uint32_t a_type;
    uint32_t a_val;
} Elf32_auxv_t;

/* Interpreter load base address (above USER_SPACE_BASE / stack area) */
#define INTERP_BASE_ADDR  0x40100000

/* ── API ─────────────────────────────────────────────────────── */

/* Check if data starts with ELF magic. Returns 1 if ELF, 0 otherwise. */
int elf_detect(const uint8_t *data, size_t size);

/* Load and run a static ELF32 binary from the filesystem.
 * Returns task ID on success, <0 on error.
 * argc/argv are passed to the ELF process on its user stack. */
int elf_run(const char *filename);
int elf_run_argv(const char *filename, int argc, const char **argv);

/* Replace current task's image with a new ELF binary (execve semantics).
 * Keeps same PID, FD table (minus CLOEXEC), and kernel stack.
 * Returns <0 on error (caller continues). Does NOT return on success. */
int elf_exec(int tid, const char *filename, int argc, const char **argv);

/* Load an ET_DYN ELF (interpreter) at a given base address into a page
 * directory. task/vt are opaque pointers to task_info_t/vma_table_t
 * (avoids header dependency — cast in implementation). */
uint32_t elf_load_interp(uint32_t pd, const char *path, uint32_t base,
                         void *task, void *vt);

#endif
