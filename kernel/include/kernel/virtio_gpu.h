#ifndef _KERNEL_VIRTIO_GPU_H
#define _KERNEL_VIRTIO_GPU_H

#include <stdint.h>

/* Initialize VirtIO GPU — returns 1 if found and initialized */
int virtio_gpu_init(void);

/* Check if VirtIO GPU is active */
int virtio_gpu_is_active(void);

/* Set up display scanout with a RAM backbuffer.
   After this, updates go through virtio_gpu_transfer + virtio_gpu_flush
   instead of writing to the MMIO framebuffer. */
int virtio_gpu_setup_scanout(uint32_t *backbuf, int width, int height, int pitch);

/* Transfer dirty rectangle from guest RAM to GPU resource */
void virtio_gpu_transfer_2d(int x, int y, int w, int h);

/* Flush (display) a rectangle on screen */
void virtio_gpu_flush(int x, int y, int w, int h);

/* Transfer + flush a list of dirty rects (batched) */
void virtio_gpu_flip_rects(int *rects, int count);

/* Set hardware cursor image (32x32 ARGB).
   pixels=NULL disables the hardware cursor. */
void virtio_gpu_set_cursor(uint32_t *pixels, int w, int h, int hot_x, int hot_y);

/* Move hardware cursor position */
void virtio_gpu_move_cursor(int x, int y);

/* Query VRAM size in KB (from device config or BGA) */
uint32_t virtio_gpu_vram_kb(void);

/* Query display info from VirtIO GPU device.
   Returns the number of enabled scanouts (0 on failure).
   For each enabled scanout, writes width/height into the output arrays. */
int virtio_gpu_get_display_info(uint32_t *widths, uint32_t *heights, int max_scanouts);

/* Check if VIRTIO_GPU_F_VIRGL was negotiated */
int virtio_gpu_has_virgl(void);

/* Allocate a unique resource ID (shared across 2D and 3D) */
uint32_t virtio_gpu_alloc_resource_id(void);

/* Public control queue submission helpers (used by virtio_gpu_3d.c) */
int virtio_gpu_submit_ctrl_cmd(void *cmd, uint32_t cmd_len,
                                void *resp, uint32_t resp_len);
int virtio_gpu_submit_ctrl_cmd_data(void *cmd, uint32_t cmd_len,
                                     void *data, uint32_t data_len,
                                     void *resp, uint32_t resp_len);

/* Attach backing storage to any resource */
int virtio_gpu_attach_resource_backing(uint32_t res_id,
                                        uint32_t *buf, uint32_t size_bytes);

/* Set scanout to a specific resource */
int virtio_gpu_set_scanout_resource(uint32_t res_id, uint32_t scanout_id,
                                     uint32_t x, uint32_t y,
                                     uint32_t w, uint32_t h);

/* Flush a resource to display */
int virtio_gpu_flush_resource(uint32_t res_id,
                               uint32_t x, uint32_t y,
                               uint32_t w, uint32_t h);

/* ═══ Bochs VGA BGA register access ═══════════════════════════ */

/* Detect Bochs VGA adapter — returns 1 if BGA registers respond */
int bga_detect(void);

/* Read/write BGA dispi registers (index 0x00-0x0A) */
uint16_t bga_read(uint16_t index);
void bga_write(uint16_t index, uint16_t value);

/* Get video memory size in 64KB blocks */
uint16_t bga_get_vram_64k(void);

/* Set a linear framebuffer mode — returns 1 on success */
int bga_set_mode(int width, int height, int bpp);

/* Get the LFB physical address from PCI BAR0 (vendor 0x1234, device 0x1111).
   Returns 0 if the BGA PCI device is not found. */
uint32_t bga_get_lfb_addr(void);

/* BGA register indices */
#define BGA_REG_ID              0x00
#define BGA_REG_XRES            0x01
#define BGA_REG_YRES            0x02
#define BGA_REG_BPP             0x03
#define BGA_REG_ENABLE          0x04
#define BGA_REG_BANK            0x05
#define BGA_REG_VIRT_WIDTH      0x06
#define BGA_REG_VIRT_HEIGHT     0x07
#define BGA_REG_X_OFFSET        0x08
#define BGA_REG_Y_OFFSET        0x09
#define BGA_REG_VIDEO_MEMORY_64K 0x0A

/* BGA enable flags */
#define BGA_ENABLED             0x01
#define BGA_LFB_ENABLED         0x40
#define BGA_NOCLEARMEM          0x80

#endif
