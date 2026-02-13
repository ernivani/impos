#ifndef _KERNEL_MULTIBOOT_H
#define _KERNEL_MULTIBOOT_H

#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    /* VBE fields (flags bit 11) */
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    /* Framebuffer fields (flags bit 12) */
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
} multiboot_info_t;

/* Multiboot memory map entry (used by PMM) */
typedef struct __attribute__((packed)) {
    uint32_t size;   /* size of this entry (excluding this field) */
    uint64_t addr;   /* base address */
    uint64_t len;    /* length in bytes */
    uint32_t type;   /* 1 = available, other = reserved */
} multiboot_mmap_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t attributes;
    uint8_t  win_a;
    uint8_t  win_b;
    uint16_t granularity;
    uint16_t winsize;
    uint16_t seg_a;
    uint16_t seg_b;
    uint32_t real_fct_ptr;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t  w_char;
    uint8_t  y_char;
    uint8_t  planes;
    uint8_t  bpp;
    uint8_t  banks;
    uint8_t  memory_model;
    uint8_t  bank_size;
    uint8_t  image_pages;
    uint8_t  reserved0;
    uint8_t  red_mask;
    uint8_t  red_position;
    uint8_t  green_mask;
    uint8_t  green_position;
    uint8_t  blue_mask;
    uint8_t  blue_position;
    uint8_t  rsv_mask;
    uint8_t  rsv_position;
    uint8_t  directcolor_attributes;
    uint32_t physbase;
} vbe_mode_info_t;

/* Multiboot module entry (one per module loaded by GRUB) */
typedef struct __attribute__((packed)) {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t reserved;
} multiboot_module_t;

/* Globals for DOOM WAD loaded as multiboot module */
extern uint8_t *doom_wad_data;
extern uint32_t doom_wad_size;

#endif
