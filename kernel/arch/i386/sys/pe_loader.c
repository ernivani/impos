#include <kernel/pe_loader.h>
#include <kernel/win32_types.h>
#include <kernel/fs.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/task.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── PE Load Address ───────────────────────────────────────────
 * We load PE images starting at 0x10000000 (256MB).
 * Each loaded image gets its own contiguous region.
 * Since we're identity-mapped for the first 256MB and have
 * PMM pages available, we allocate physical frames and map them.
 */
#define PE_LOAD_BASE  0x02000000   /* 32MB — safely above kernel heap (reserved up to ~19MB) */

static uint32_t pe_next_load_addr = PE_LOAD_BASE;

/* ── Helpers ─────────────────────────────────────────────────── */

static uint32_t align_up(uint32_t val, uint32_t align) {
    return (val + align - 1) & ~(align - 1);
}

/* Read an entire file into a malloc'd buffer. Caller must free(). */
static uint8_t *read_file_to_buffer(const char *filename, size_t *out_size) {
    uint32_t parent;
    char name[28];
    int ino = fs_resolve_path(filename, &parent, name);
    if (ino < 0) return NULL;

    inode_t node;
    fs_read_inode(ino, &node);
    if (node.type != 1) return NULL;  /* not a regular file */
    if (node.size == 0) return NULL;

    uint8_t *buf = malloc(node.size);
    if (!buf) return NULL;

    uint32_t offset = 0;
    while (offset < node.size) {
        int n = fs_read_at(ino, buf + offset, offset, node.size - offset);
        if (n <= 0) { free(buf); return NULL; }
        offset += n;
    }
    *out_size = node.size;
    return buf;
}

/* ── PE Loader ───────────────────────────────────────────────── */

int pe_load(const char *filename, pe_loaded_image_t *out) {
    size_t file_size;
    uint8_t *file_data = read_file_to_buffer(filename, &file_size);
    if (!file_data) {
        DBG("pe_load: file not found '%s'", filename);
        return -1;
    }

    /* Parse DOS header */
    pe_dos_header_t *dos = (pe_dos_header_t *)file_data;
    if (file_size < sizeof(pe_dos_header_t) || dos->e_magic != PE_DOS_MAGIC) {
        printf("pe: invalid DOS header\n");
        free(file_data);
        return -2;
    }

    /* Parse PE signature */
    uint32_t *pe_sig = (uint32_t *)(file_data + dos->e_lfanew);
    if (*pe_sig != PE_SIGNATURE) {
        printf("pe: invalid PE signature\n");
        free(file_data);
        return -3;
    }

    /* Parse COFF + Optional headers */
    pe_coff_header_t *coff = (pe_coff_header_t *)(file_data + dos->e_lfanew + 4);
    if (coff->machine != PE_MACHINE_I386 || !(coff->characteristics & PE_CHAR_EXECUTABLE)) {
        printf("pe: not an i386 executable\n");
        free(file_data);
        return -4;
    }

    pe_optional_header_t *opt = (pe_optional_header_t *)(
        file_data + dos->e_lfanew + 4 + sizeof(pe_coff_header_t));
    if (opt->magic != PE32_MAGIC) {
        printf("pe: not PE32 format\n");
        free(file_data);
        return -5;
    }

    /* Map image into memory */
    uint32_t image_size = align_up(opt->image_size, 4096);
    uint32_t load_base = pe_next_load_addr;

    DBG("pe_load: '%s' file_size=%u image_size=0x%x load_base=0x%x preferred=0x%x",
        filename, (unsigned)file_size, image_size, load_base, opt->image_base);

    memset((void *)load_base, 0, image_size);

    uint32_t headers_size = opt->headers_size;
    if (headers_size > file_size) headers_size = file_size;
    memcpy((void *)load_base, file_data, headers_size);

    /* Copy sections */
    pe_section_header_t *sections = (pe_section_header_t *)(
        (uint8_t *)opt + coff->optional_header_size);

    for (int i = 0; i < coff->num_sections; i++) {
        pe_section_header_t *sec = &sections[i];
        uint32_t dest = load_base + sec->virtual_address;
        uint32_t copy_size = sec->raw_data_size;
        if (sec->raw_data_offset + copy_size > file_size)
            copy_size = file_size - sec->raw_data_offset;
        if (copy_size > 0 && sec->raw_data_offset > 0)
            memcpy((void *)dest, file_data + sec->raw_data_offset, copy_size);
        DBG("pe_load: section %d → va=0x%x raw_sz=0x%x dest=0x%x",
            i, sec->virtual_address, copy_size, dest);
    }

    /* Fill output structure */
    memset(out, 0, sizeof(*out));
    out->image_base = load_base;
    out->image_size = image_size;
    out->entry_point = load_base + opt->entry_point_rva;
    out->subsystem = opt->subsystem;
    out->num_sections = coff->num_sections;
    out->preferred_base = opt->image_base;
    if (opt->num_data_dirs > PE_DIR_IMPORT) {
        out->import_dir_rva = opt->data_dirs[PE_DIR_IMPORT].virtual_address;
        out->import_dir_size = opt->data_dirs[PE_DIR_IMPORT].size;
    }
    if (opt->num_data_dirs > PE_DIR_BASERELOC) {
        out->reloc_dir_rva = opt->data_dirs[PE_DIR_BASERELOC].virtual_address;
        out->reloc_dir_size = opt->data_dirs[PE_DIR_BASERELOC].size;
    }
    if (opt->num_data_dirs > PE_DIR_EXPORT) {
        out->export_dir_rva = opt->data_dirs[PE_DIR_EXPORT].virtual_address;
        out->export_dir_size = opt->data_dirs[PE_DIR_EXPORT].size;
    }
    pe_next_load_addr = align_up(load_base + image_size, 4096);

    DBG("pe_load: entry=0x%x subsystem=%u import_rva=0x%x reloc_rva=0x%x",
        out->entry_point, out->subsystem, out->import_dir_rva, out->reloc_dir_rva);

    free(file_data);
    return 0;
}

/* ── Import Resolver ─────────────────────────────────────────── */

/* Master lookup table of all DLL shims */
extern const win32_dll_shim_t win32_ucrtbase;

static const win32_dll_shim_t *shim_table[] = {
    &win32_kernel32,
    &win32_user32,
    &win32_gdi32,
    &win32_msvcrt,
    &win32_ucrtbase,
    &win32_advapi32,
    &win32_ws2_32,
    &win32_gdiplus,
    &win32_ole32,
    &win32_shell32,
    &win32_bcrypt,
    &win32_crypt32,
    NULL
};

static int dll_name_match(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

void *win32_resolve_import(const char *dll_name, const char *func_name) {
    /* First: try the specific DLL requested */
    for (int d = 0; shim_table[d] != NULL; d++) {
        if (!dll_name_match(shim_table[d]->dll_name, dll_name))
            continue;
        for (int e = 0; e < shim_table[d]->num_exports; e++) {
            if (strcmp(shim_table[d]->exports[e].name, func_name) == 0)
                return shim_table[d]->exports[e].func;
        }
    }
    /* Fallback: some Win32 functions live in a different DLL than expected
     * (e.g. BeginPaint/EndPaint/FillRect are user32 on Windows but we
     *  implement them in gdi32). Search all DLLs as a fallback. */
    for (int d = 0; shim_table[d] != NULL; d++) {
        if (dll_name_match(shim_table[d]->dll_name, dll_name))
            continue;  /* already searched */
        for (int e = 0; e < shim_table[d]->num_exports; e++) {
            if (strcmp(shim_table[d]->exports[e].name, func_name) == 0)
                return shim_table[d]->exports[e].func;
        }
    }
    return NULL;
}

int pe_resolve_imports(pe_loaded_image_t *img) {
    if (img->import_dir_rva == 0 || img->import_dir_size == 0) {
        DBG("pe_resolve_imports: no import directory");
        return 0;  /* No imports — that's OK */
    }

    pe_import_descriptor_t *imp = (pe_import_descriptor_t *)(
        img->image_base + img->import_dir_rva);

    int resolved = 0, unresolved = 0;

    /* Iterate import descriptors (null-terminated) */
    while (imp->name_rva != 0) {
        const char *dll_name = (const char *)(img->image_base + imp->name_rva);
        DBG("pe_resolve_imports: importing from '%s'", dll_name);

        /* Walk the import lookup table (ILT) and patch the IAT */
        uint32_t *ilt = (uint32_t *)(img->image_base +
            (imp->import_lookup_table ? imp->import_lookup_table : imp->import_address_table));
        uint32_t *iat = (uint32_t *)(img->image_base + imp->import_address_table);

        for (int i = 0; ilt[i] != 0; i++) {
            const char *func_name;
            uint32_t ordinal = 0;

            if (ilt[i] & PE_IMPORT_ORDINAL_FLAG) {
                ordinal = ilt[i] & 0xFFFF;
                DBG("pe_resolve_imports: ordinal #%u not supported", ordinal);
                unresolved++;
                continue;
            } else {
                pe_import_hint_name_t *hint = (pe_import_hint_name_t *)(
                    img->image_base + (ilt[i] & 0x7FFFFFFF));
                func_name = hint->name;
            }

            void *shim_func = win32_resolve_import(dll_name, func_name);
            if (shim_func) {
                iat[i] = (uint32_t)shim_func;
                DBG("pe_resolve_imports:   %s → 0x%x", func_name, (unsigned)shim_func);
                resolved++;
            } else {
                printf("pe: unresolved import %s!%s\n", dll_name, func_name);
                iat[i] = 0;
                unresolved++;
            }
        }

        imp++;
    }

    DBG("pe_resolve_imports: %d resolved, %d unresolved", resolved, unresolved);
    return 0;
}

/* ── Base Relocations ────────────────────────────────────────── */

int pe_apply_relocations(pe_loaded_image_t *img) {
    if (img->reloc_dir_rva == 0 || img->reloc_dir_size == 0) {
        if (img->image_base != img->preferred_base) {
            printf("pe: no relocations but loaded at different base\n");
            return -1;
        }
        return 0;
    }

    uint32_t effective_base = img->virtual_base ? img->virtual_base : img->image_base;
    int32_t delta = (int32_t)(effective_base - img->preferred_base);
    if (delta == 0) return 0;  /* No fixup needed */

    DBG("pe_apply_relocations: effective_base=0x%x preferred=0x%x staging=0x%x delta=0x%x",
        effective_base, img->preferred_base, img->image_base, delta);

    uint8_t *reloc = (uint8_t *)(img->image_base + img->reloc_dir_rva);
    uint8_t *reloc_end = reloc + img->reloc_dir_size;
    int count = 0;

    while (reloc < reloc_end) {
        pe_base_reloc_block_t *block = (pe_base_reloc_block_t *)reloc;
        if (block->block_size == 0) break;

        uint16_t *entries = (uint16_t *)(reloc + sizeof(pe_base_reloc_block_t));
        int num_entries = (block->block_size - sizeof(pe_base_reloc_block_t)) / 2;

        for (int i = 0; i < num_entries; i++) {
            uint16_t entry = entries[i];
            uint8_t type = entry >> 12;
            uint16_t offset = entry & 0x0FFF;

            if (type == PE_RELOC_HIGHLOW) {
                uint32_t *patch_addr = (uint32_t *)(
                    img->image_base + block->page_rva + offset);
                *patch_addr += delta;
                count++;
            } else if (type == PE_RELOC_ABSOLUTE) {
                /* Padding — skip */
            }
        }

        reloc += block->block_size;
    }

    DBG("pe_apply_relocations: applied %d relocations", count);
    return 0;
}

/* ── PE Execution ────────────────────────────────────────────── */

/* Thread entry wrapper for PE execution — per-task context array */
typedef struct {
    uint32_t entry_point;
    uint16_t subsystem;
    char     cmd_line[128];   /* stored command line for GetCommandLineA */
} pe_exec_ctx_t;

static pe_exec_ctx_t pe_ctxs[TASK_MAX];  /* indexed by tid — thread-safe */

/* Accessor for GetCommandLineA */
const char *pe_get_command_line(int tid) {
    if (tid < 0 || tid >= TASK_MAX) return "";
    return pe_ctxs[tid].cmd_line;
}

static void pe_thread_entry(void) {
    int ret;
    int tid = task_get_current();
    pe_exec_ctx_t *ctx = &pe_ctxs[tid];

    DBG("pe_thread_entry: subsystem=%u entry=0x%x tid=%d",
        ctx->subsystem, ctx->entry_point, tid);

    if (ctx->subsystem == PE_SUBSYSTEM_WINDOWS_GUI) {
        typedef int (__attribute__((stdcall)) *pe_winmain_t)(
            uint32_t hInstance, uint32_t hPrevInstance,
            char *lpCmdLine, int nCmdShow);
        pe_winmain_t entry = (pe_winmain_t)ctx->entry_point;
        DBG("pe_thread_entry: calling WinMain at 0x%x", ctx->entry_point);
        ret = entry(0x00400000, 0, ctx->cmd_line, 5 /* SW_SHOW */);
    } else {
        typedef int (*pe_main_t)(void);
        pe_main_t entry = (pe_main_t)ctx->entry_point;
        DBG("pe_thread_entry: calling main at 0x%x", ctx->entry_point);
        ret = entry();
    }

    DBG("pe_thread_entry: entry returned %d, calling task_exit()", ret);
    (void)ret;
    task_exit();
}

int pe_execute(pe_loaded_image_t *img, const char *name) {
    char task_name[32];
    strncpy(task_name, name, 27);
    task_name[27] = '\0';

    int tid = task_create_thread(task_name, pe_thread_entry, 1 /* killable */);
    if (tid < 0) {
        printf("pe: failed to create thread\n");
        return -1;
    }

    /* Store context in per-task slot AFTER tid is known — no race */
    pe_ctxs[tid].entry_point = img->entry_point;
    pe_ctxs[tid].subsystem = img->subsystem;
    strncpy(pe_ctxs[tid].cmd_line, name, sizeof(pe_ctxs[tid].cmd_line) - 1);
    pe_ctxs[tid].cmd_line[sizeof(pe_ctxs[tid].cmd_line) - 1] = '\0';

    /* Allocate and initialize a WIN32_TEB for this PE task */
    task_info_t *t = task_get(tid);
    if (t) {
        WIN32_TEB *teb = (WIN32_TEB *)calloc(1, sizeof(WIN32_TEB));
        if (teb) {
            teb->tib.ExceptionList = SEH_CHAIN_END;
            teb->tib.StackBase = t->stack_base ?
                (uint32_t)t->stack_base + t->stack_size : 0;
            teb->tib.StackLimit = t->stack_base ?
                (uint32_t)t->stack_base : 0;
            teb->tib.Self = (uint32_t)&teb->tib;
            teb->ClientId[0] = t->pid;  /* ProcessId */
            teb->ClientId[1] = tid;     /* ThreadId */
            teb->LastError = 0;

            t->tib = (uint32_t)teb;
            t->is_pe = 1;

            DBG("pe_execute: TEB at 0x%x for task %d", (unsigned)teb, tid);
        }
    }

    DBG("pe_execute: started '%s' as task %d", task_name, tid);
    return tid;
}

/* ── Convenience: full load-and-run pipeline ─────────────────── */

int pe_run(const char *filename) {
    pe_loaded_image_t img;

    int ret = pe_load(filename, &img);
    if (ret < 0) return ret;

    ret = pe_apply_relocations(&img);
    if (ret < 0) {
        pe_unload(&img);
        return ret;
    }

    ret = pe_resolve_imports(&img);
    if (ret < 0) {
        pe_unload(&img);
        return ret;
    }

    return pe_execute(&img, filename);
}

/* ── Cleanup ─────────────────────────────────────────────────── */

/* Simple free-list for address reclamation */
#define PE_FREE_LIST_SIZE 4
static struct { uint32_t addr; uint32_t size; } pe_free_list[PE_FREE_LIST_SIZE];

void pe_unload(pe_loaded_image_t *img) {
    if (img->image_base == 0 || img->image_size == 0) {
        memset(img, 0, sizeof(*img));
        return;
    }

    /* If this was the last loaded image, reclaim the address space */
    uint32_t img_end = align_up(img->image_base + img->image_size, 4096);
    if (img_end == pe_next_load_addr) {
        pe_next_load_addr = img->image_base;
    } else {
        /* Not the last — add to free list for reuse */
        for (int i = 0; i < PE_FREE_LIST_SIZE; i++) {
            if (pe_free_list[i].addr == 0) {
                pe_free_list[i].addr = img->image_base;
                pe_free_list[i].size = align_up(img->image_size, 4096);
                break;
            }
        }
    }

    memset(img, 0, sizeof(*img));
}
