/* pe_loader.c — aarch64 stub
 *
 * PE32 is an i386-only format. Stub out the API so code that
 * conditionally calls pe_run() can link.
 */
#include <kernel/pe_loader.h>
#include <stdio.h>

int pe_load(const char *filename, pe_loaded_image_t *out) {
    (void)filename; (void)out;
    return -1;
}

int pe_resolve_imports(pe_loaded_image_t *img) {
    (void)img;
    return -1;
}

int pe_apply_relocations(pe_loaded_image_t *img) {
    (void)img;
    return -1;
}

int pe_execute(pe_loaded_image_t *img, const char *name) {
    (void)img; (void)name;
    return -1;
}

int pe_run(const char *filename) {
    (void)filename;
    return -1;
}

void pe_unload(pe_loaded_image_t *img) {
    (void)img;
}

const char *pe_get_command_line(int tid) {
    (void)tid;
    return "";
}
