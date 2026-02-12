#ifndef _KERNEL_PE_LOADER_H
#define _KERNEL_PE_LOADER_H

#include <stdint.h>

/* ── DOS Header ─────────────────────────────────────────────── */
#define PE_DOS_MAGIC  0x5A4D   /* "MZ" */

typedef struct __attribute__((packed)) {
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;         /* offset to PE signature */
} pe_dos_header_t;

/* ── PE Signature + COFF Header ──────────────────────────────── */
#define PE_SIGNATURE    0x00004550  /* "PE\0\0" */

#define PE_MACHINE_I386 0x014C

typedef struct __attribute__((packed)) {
    uint16_t machine;
    uint16_t num_sections;
    uint32_t timestamp;
    uint32_t symbol_table_offset;
    uint32_t num_symbols;
    uint16_t optional_header_size;
    uint16_t characteristics;
} pe_coff_header_t;

/* Characteristics flags */
#define PE_CHAR_EXECUTABLE  0x0002
#define PE_CHAR_32BIT       0x0100
#define PE_CHAR_DLL         0x2000

/* ── Optional Header (PE32) ──────────────────────────────────── */
#define PE32_MAGIC  0x010B

#define PE_SUBSYSTEM_UNKNOWN    0
#define PE_SUBSYSTEM_NATIVE     1
#define PE_SUBSYSTEM_WINDOWS_GUI    2
#define PE_SUBSYSTEM_WINDOWS_CUI    3  /* console */

#define PE_NUM_DATA_DIRS  16

typedef struct __attribute__((packed)) {
    uint32_t virtual_address;
    uint32_t size;
} pe_data_directory_t;

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  linker_ver_major;
    uint8_t  linker_ver_minor;
    uint32_t code_size;
    uint32_t initialized_data_size;
    uint32_t uninitialized_data_size;
    uint32_t entry_point_rva;
    uint32_t code_base;
    uint32_t data_base;
    /* PE32 only */
    uint32_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint16_t os_ver_major;
    uint16_t os_ver_minor;
    uint16_t image_ver_major;
    uint16_t image_ver_minor;
    uint16_t subsystem_ver_major;
    uint16_t subsystem_ver_minor;
    uint32_t win32_version;
    uint32_t image_size;
    uint32_t headers_size;
    uint32_t checksum;
    uint16_t subsystem;
    uint16_t dll_characteristics;
    uint32_t stack_reserve;
    uint32_t stack_commit;
    uint32_t heap_reserve;
    uint32_t heap_commit;
    uint32_t loader_flags;
    uint32_t num_data_dirs;
    pe_data_directory_t data_dirs[PE_NUM_DATA_DIRS];
} pe_optional_header_t;

/* Data directory indices */
#define PE_DIR_EXPORT    0
#define PE_DIR_IMPORT    1
#define PE_DIR_RESOURCE  2
#define PE_DIR_EXCEPTION 3
#define PE_DIR_SECURITY  4
#define PE_DIR_BASERELOC 5
#define PE_DIR_DEBUG     6
#define PE_DIR_TLS       9
#define PE_DIR_IAT       12

/* ── Section Header ──────────────────────────────────────────── */
#define PE_SECTION_NAME_LEN  8

typedef struct __attribute__((packed)) {
    char     name[PE_SECTION_NAME_LEN];
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t raw_data_size;
    uint32_t raw_data_offset;
    uint32_t relocations_offset;
    uint32_t linenumbers_offset;
    uint16_t num_relocations;
    uint16_t num_linenumbers;
    uint32_t characteristics;
} pe_section_header_t;

/* Section characteristics */
#define PE_SEC_CODE         0x00000020
#define PE_SEC_INITIALIZED  0x00000040
#define PE_SEC_UNINITIALIZED 0x00000080
#define PE_SEC_EXECUTE      0x20000000
#define PE_SEC_READ         0x40000000
#define PE_SEC_WRITE        0x80000000

/* ── Import Directory ────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t import_lookup_table;   /* RVA to ILT (or Characteristics) */
    uint32_t timestamp;
    uint32_t forwarder_chain;
    uint32_t name_rva;              /* RVA to DLL name string */
    uint32_t import_address_table;  /* RVA to IAT */
} pe_import_descriptor_t;

/* Import lookup table entry: bit 31 = ordinal flag */
#define PE_IMPORT_ORDINAL_FLAG  0x80000000

typedef struct __attribute__((packed)) {
    uint16_t hint;
    char     name[];                /* null-terminated function name */
} pe_import_hint_name_t;

/* ── Export Directory ────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t characteristics;
    uint32_t timestamp;
    uint16_t major_version;
    uint16_t minor_version;
    uint32_t name_rva;
    uint32_t ordinal_base;
    uint32_t num_functions;
    uint32_t num_names;
    uint32_t addr_table_rva;
    uint32_t name_table_rva;
    uint32_t ordinal_table_rva;
} pe_export_directory_t;

/* ── Base Relocation ─────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t page_rva;
    uint32_t block_size;
    /* followed by uint16_t entries[] */
} pe_base_reloc_block_t;

#define PE_RELOC_ABSOLUTE   0   /* skip (padding) */
#define PE_RELOC_HIGHLOW    3   /* 32-bit full relocation */

/* ── Loaded PE image info ────────────────────────────────────── */
typedef struct {
    uint32_t image_base;       /* actual base in memory */
    uint32_t image_size;       /* total virtual size */
    uint32_t entry_point;      /* absolute address of entry point */
    uint16_t subsystem;        /* PE_SUBSYSTEM_WINDOWS_GUI or _CUI */
    int      num_sections;
    uint32_t preferred_base;   /* original ImageBase from PE header */
    uint32_t virtual_base;     /* target virtual address (0 = use image_base) */

    /* Import info for resolver */
    uint32_t import_dir_rva;
    uint32_t import_dir_size;

    /* Relocation info */
    uint32_t reloc_dir_rva;
    uint32_t reloc_dir_size;

    /* Export directory info */
    uint32_t export_dir_rva;
    uint32_t export_dir_size;
} pe_loaded_image_t;

/* ── API ─────────────────────────────────────────────────────── */

/* Load a PE file from the filesystem into memory.
 * Returns 0 on success, <0 on error. Fills out pe_loaded_image_t. */
int pe_load(const char *filename, pe_loaded_image_t *out);

/* Resolve imports in a loaded PE image against our Win32 shim tables.
 * Must be called after pe_load(). Returns 0 on success. */
int pe_resolve_imports(pe_loaded_image_t *img);

/* Apply base relocations (if image couldn't load at preferred base). */
int pe_apply_relocations(pe_loaded_image_t *img);

/* Execute a loaded PE image. Creates a task and runs the entry point. */
int pe_execute(pe_loaded_image_t *img, const char *name);

/* Convenience: load + resolve + relocate + execute in one call. */
int pe_run(const char *filename);

/* Free all memory allocated for a loaded PE image. */
void pe_unload(pe_loaded_image_t *img);

#endif
