// WAD file reader for ImposOS — reads from multiboot module in memory

#include <string.h>
#include <stdint.h>

#include "w_file.h"
#include "z_zone.h"

extern uint8_t *doom_wad_data;
extern uint32_t doom_wad_size;

typedef struct
{
    wad_file_t wad;
} impos_wad_file_t;

static wad_file_t *W_Impos_OpenFile(char *path)
{
    (void)path;

    if (!doom_wad_data || doom_wad_size == 0)
        return NULL;

    printf("W_Impos_OpenFile: data=%p size=%u header=[%02x %02x %02x %02x] '%c%c%c%c'\n",
           (void*)doom_wad_data, doom_wad_size,
           doom_wad_data[0], doom_wad_data[1], doom_wad_data[2], doom_wad_data[3],
           doom_wad_data[0], doom_wad_data[1], doom_wad_data[2], doom_wad_data[3]);

    impos_wad_file_t *result = Z_Malloc(sizeof(impos_wad_file_t), PU_STATIC, 0);

    extern wad_file_class_t impos_wad_file;
    result->wad.file_class = &impos_wad_file;
    result->wad.mapped = doom_wad_data;
    result->wad.length = doom_wad_size;

    return &result->wad;
}

static void W_Impos_CloseFile(wad_file_t *wad)
{
    Z_Free(wad);
}

static size_t W_Impos_Read(wad_file_t *wad, unsigned int offset,
                           void *buffer, size_t buffer_len)
{
    (void)wad;

    if (offset >= doom_wad_size)
        return 0;

    size_t avail = doom_wad_size - offset;
    if (buffer_len > avail)
        buffer_len = avail;

    memcpy(buffer, doom_wad_data + offset, buffer_len);
    return buffer_len;
}

wad_file_class_t impos_wad_file =
{
    W_Impos_OpenFile,
    W_Impos_CloseFile,
    W_Impos_Read,
};
