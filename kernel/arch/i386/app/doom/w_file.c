//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// WAD I/O functions — ImposOS version (uses memory-mapped module)
//

#include <stdio.h>

#include "config.h"
#include "doomtype.h"
#include "m_argv.h"
#include "w_file.h"

extern wad_file_class_t impos_wad_file;

wad_file_t *W_OpenFile(char *path)
{
    return impos_wad_file.OpenFile(path);
}

void W_CloseFile(wad_file_t *wad)
{
    wad->file_class->CloseFile(wad);
}

size_t W_Read(wad_file_t *wad, unsigned int offset,
              void *buffer, size_t buffer_len)
{
    return wad->file_class->Read(wad, offset, buffer, buffer_len);
}
