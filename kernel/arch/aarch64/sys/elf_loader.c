/* elf_loader.c — aarch64 stub
 *
 * The full ELF loader is i386-specific (32-bit ELF, x86 page tables,
 * i386 user-mode entry via IRET). A proper aarch64 ELF loader would
 * need ELF64 + aarch64 page table setup + ERET to EL0.
 * Stubbed for now so shell.c's elf_run_argv() calls compile.
 */
#include <kernel/elf_loader.h>
#include <stdio.h>

int elf_detect(const uint8_t *data, size_t size) {
    (void)data; (void)size;
    return 0;
}

int elf_run(const char *filename) {
    (void)filename;
    printf("elf_run: not implemented on aarch64\n");
    return -1;
}

int elf_run_argv(const char *filename, int argc, const char **argv) {
    (void)filename; (void)argc; (void)argv;
    printf("elf_run: not implemented on aarch64\n");
    return -1;
}

int elf_exec(int tid, const char *filename, int argc, const char **argv) {
    (void)tid; (void)filename; (void)argc; (void)argv;
    return -1;
}

uint32_t elf_load_interp(uint32_t pd, const char *path, uint32_t base,
                          void *task, void *vt) {
    (void)pd; (void)path; (void)base; (void)task; (void)vt;
    return 0;
}
