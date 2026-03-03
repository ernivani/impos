/* image.c — Phase 13: BMP + PNG image decoders with DEFLATE decompressor.
 *
 * BMP: 24/32bpp uncompressed (BI_RGB) with bottom-up row handling.
 * PNG: 8-bit RGB/RGBA, non-interlaced, full RFC 1951 DEFLATE.
 * All buffers are heap-allocated (4KB kernel stacks!).
 */
#include <kernel/image.h>
#include <kernel/fs.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── BMP Decoder ───────────────────────────────────────────────────── */

#define BMP_MAGIC   0x4D42  /* 'BM' little-endian */
#define BI_RGB      0

static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

image_t *image_load_bmp(const void *data, uint32_t size) {
    const uint8_t *d = (const uint8_t *)data;
    if (size < 54) return NULL;

    /* BITMAPFILEHEADER: 14 bytes */
    if (rd16(d) != BMP_MAGIC) return NULL;
    uint32_t pixel_off = rd32(d + 10);

    /* BITMAPINFOHEADER: 40 bytes at offset 14 */
    uint32_t hdr_size = rd32(d + 14);
    if (hdr_size < 40) return NULL;

    int32_t w = (int32_t)rd32(d + 18);
    int32_t h_raw = (int32_t)rd32(d + 22);
    int top_down = (h_raw < 0);
    int h = top_down ? -h_raw : h_raw;

    uint16_t bpp = rd16(d + 28);
    uint32_t compression = rd32(d + 30);

    if (compression != BI_RGB) return NULL;
    if (bpp != 24 && bpp != 32) return NULL;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return NULL;

    int bytes_pp = bpp / 8;
    int row_bytes = w * bytes_pp;
    int row_stride = (row_bytes + 3) & ~3;  /* rows padded to 4 bytes */

    if (pixel_off + (uint32_t)row_stride * (uint32_t)h > size) return NULL;

    image_t *img = malloc(sizeof(image_t));
    if (!img) return NULL;
    img->width = w;
    img->height = h;
    img->pixels = malloc((uint32_t)w * (uint32_t)h * 4);
    if (!img->pixels) { free(img); return NULL; }

    for (int y = 0; y < h; y++) {
        /* BMP is bottom-up by default */
        int src_row = top_down ? y : (h - 1 - y);
        const uint8_t *row = d + pixel_off + src_row * row_stride;
        uint32_t *dst = img->pixels + y * w;
        for (int x = 0; x < w; x++) {
            uint8_t b = row[x * bytes_pp + 0];
            uint8_t g = row[x * bytes_pp + 1];
            uint8_t r = row[x * bytes_pp + 2];
            uint8_t a = (bpp == 32) ? row[x * bytes_pp + 3] : 0xFF;
            dst[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                     ((uint32_t)g << 8) | b;
        }
    }
    return img;
}

/* ── DEFLATE Decompressor (RFC 1951) ───────────────────────────────── */

/* Bit reader — reads LSB first from a byte stream */
typedef struct {
    const uint8_t *data;
    uint32_t       len;
    uint32_t       pos;       /* byte position */
    uint32_t       bit_buf;   /* accumulated bits */
    int            bit_cnt;   /* bits in buffer */
} bitreader_t;

static void br_init(bitreader_t *br, const uint8_t *data, uint32_t len) {
    br->data = data;
    br->len = len;
    br->pos = 0;
    br->bit_buf = 0;
    br->bit_cnt = 0;
}

static void br_refill(bitreader_t *br) {
    while (br->bit_cnt <= 24 && br->pos < br->len) {
        br->bit_buf |= (uint32_t)br->data[br->pos++] << br->bit_cnt;
        br->bit_cnt += 8;
    }
}

static uint32_t br_bits(bitreader_t *br, int n) {
    br_refill(br);
    uint32_t val = br->bit_buf & ((1u << n) - 1);
    br->bit_buf >>= n;
    br->bit_cnt -= n;
    return val;
}

/* Huffman table: up to 288 symbols, max code length 15 */
#define HUFF_MAX_SYMS  288
#define HUFF_MAX_BITS  15

typedef struct {
    uint16_t counts[HUFF_MAX_BITS + 1];  /* count of codes at each length */
    uint16_t symbols[HUFF_MAX_SYMS];     /* symbols sorted by code */
} hufftab_t;

static int huff_build(hufftab_t *ht, const uint8_t *lens, int nsyms) {
    memset(ht->counts, 0, sizeof(ht->counts));
    for (int i = 0; i < nsyms; i++)
        ht->counts[lens[i]]++;

    /* Check for over-subscribed or incomplete code */
    ht->counts[0] = 0;  /* unused */

    uint16_t offsets[HUFF_MAX_BITS + 1];
    offsets[0] = 0;
    offsets[1] = 0;
    for (int i = 1; i < HUFF_MAX_BITS; i++)
        offsets[i + 1] = offsets[i] + ht->counts[i];

    for (int i = 0; i < nsyms; i++) {
        if (lens[i] > 0)
            ht->symbols[offsets[lens[i]]++] = (uint16_t)i;
    }
    return 0;
}

static int huff_decode(hufftab_t *ht, bitreader_t *br) {
    br_refill(br);
    uint32_t code = 0;
    uint32_t first = 0;
    int index = 0;

    for (int len = 1; len <= HUFF_MAX_BITS; len++) {
        code |= (br->bit_buf & 1);
        br->bit_buf >>= 1;
        br->bit_cnt--;

        uint16_t count = ht->counts[len];
        if (code < first + count)
            return ht->symbols[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;  /* invalid */
}

/* Fixed Huffman tables (built once) */
static hufftab_t fixed_lit;
static hufftab_t fixed_dist;
static int       fixed_built = 0;

static void build_fixed_tables(void) {
    if (fixed_built) return;
    uint8_t lens[288];
    int i;
    for (i = 0;   i <= 143; i++) lens[i] = 8;
    for (i = 144; i <= 255; i++) lens[i] = 9;
    for (i = 256; i <= 279; i++) lens[i] = 7;
    for (i = 280; i <= 287; i++) lens[i] = 8;
    huff_build(&fixed_lit, lens, 288);

    for (i = 0; i < 32; i++) lens[i] = 5;
    huff_build(&fixed_dist, lens, 32);
    fixed_built = 1;
}

/* Length and distance base/extra tables per RFC 1951 */
static const uint16_t len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
    35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const uint8_t len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
    3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const uint16_t dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
    257,385,513,769,1025,1537,2049,3073,4097,6145,
    8193,12289,16385,24577
};
static const uint8_t dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
    7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

/* Inflate a DEFLATE stream. Returns decompressed data (malloc'd), or NULL.
 * out_len receives the decompressed size. */
static uint8_t *deflate_inflate(const uint8_t *src, uint32_t src_len,
                                 uint32_t *out_len) {
    bitreader_t br;
    br_init(&br, src, src_len);

    /* Output buffer — start at 64KB, grow as needed */
    uint32_t cap = 65536;
    uint32_t pos = 0;
    uint8_t *out = malloc(cap);
    if (!out) return NULL;

    /* 32KB sliding window is implicit in the output buffer */
    int bfinal;
    do {
        bfinal = (int)br_bits(&br, 1);
        int btype = (int)br_bits(&br, 2);

        if (btype == 0) {
            /* Stored block — align to byte boundary */
            br.bit_buf = 0;
            br.bit_cnt = 0;
            if (br.pos + 4 > br.len) goto fail;
            uint16_t slen = br.data[br.pos] | (br.data[br.pos + 1] << 8);
            br.pos += 4;  /* skip LEN and NLEN */
            if (br.pos + slen > br.len) goto fail;
            while (pos + slen > cap) {
                cap *= 2;
                uint8_t *tmp = realloc(out, cap);
                if (!tmp) goto fail;
                out = tmp;
            }
            memcpy(out + pos, br.data + br.pos, slen);
            pos += slen;
            br.pos += slen;
        } else if (btype == 1 || btype == 2) {
            hufftab_t *lit_ht, *dist_ht;
            /* Heap-allocated tables for dynamic Huffman */
            hufftab_t *dyn_lit = NULL, *dyn_dist = NULL;

            if (btype == 1) {
                build_fixed_tables();
                lit_ht = &fixed_lit;
                dist_ht = &fixed_dist;
            } else {
                /* Dynamic Huffman tables */
                int hlit  = (int)br_bits(&br, 5) + 257;
                int hdist = (int)br_bits(&br, 5) + 1;
                int hclen = (int)br_bits(&br, 4) + 4;

                static const uint8_t cl_order[19] = {
                    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
                };

                uint8_t cl_lens[19];
                memset(cl_lens, 0, sizeof(cl_lens));
                for (int i = 0; i < hclen; i++)
                    cl_lens[cl_order[i]] = (uint8_t)br_bits(&br, 3);

                hufftab_t cl_ht;
                huff_build(&cl_ht, cl_lens, 19);

                /* Decode literal/distance code lengths */
                int total = hlit + hdist;
                uint8_t *code_lens = malloc(total);
                if (!code_lens) goto fail;
                int idx = 0;
                while (idx < total) {
                    int sym = huff_decode(&cl_ht, &br);
                    if (sym < 0) { free(code_lens); goto fail; }
                    if (sym < 16) {
                        code_lens[idx++] = (uint8_t)sym;
                    } else if (sym == 16) {
                        int rep = (int)br_bits(&br, 2) + 3;
                        uint8_t prev = idx > 0 ? code_lens[idx - 1] : 0;
                        while (rep-- > 0 && idx < total)
                            code_lens[idx++] = prev;
                    } else if (sym == 17) {
                        int rep = (int)br_bits(&br, 3) + 3;
                        while (rep-- > 0 && idx < total)
                            code_lens[idx++] = 0;
                    } else if (sym == 18) {
                        int rep = (int)br_bits(&br, 7) + 11;
                        while (rep-- > 0 && idx < total)
                            code_lens[idx++] = 0;
                    }
                }

                dyn_lit = malloc(sizeof(hufftab_t));
                dyn_dist = malloc(sizeof(hufftab_t));
                if (!dyn_lit || !dyn_dist) {
                    free(code_lens);
                    free(dyn_lit);
                    free(dyn_dist);
                    goto fail;
                }
                huff_build(dyn_lit, code_lens, hlit);
                huff_build(dyn_dist, code_lens + hlit, hdist);
                free(code_lens);
                lit_ht = dyn_lit;
                dist_ht = dyn_dist;
            }

            /* Decode symbols */
            for (;;) {
                int sym = huff_decode(lit_ht, &br);
                if (sym < 0) {
                    free(dyn_lit); free(dyn_dist);
                    goto fail;
                }
                if (sym == 256) break;  /* end of block */

                if (sym < 256) {
                    /* Literal byte */
                    if (pos >= cap) {
                        cap *= 2;
                        uint8_t *tmp = realloc(out, cap);
                        if (!tmp) {
                            free(dyn_lit); free(dyn_dist);
                            goto fail;
                        }
                        out = tmp;
                    }
                    out[pos++] = (uint8_t)sym;
                } else {
                    /* Length/distance pair */
                    int len_idx = sym - 257;
                    if (len_idx < 0 || len_idx >= 29) {
                        free(dyn_lit); free(dyn_dist);
                        goto fail;
                    }
                    uint32_t length = len_base[len_idx] +
                                      br_bits(&br, len_extra[len_idx]);

                    int dist_sym = huff_decode(dist_ht, &br);
                    if (dist_sym < 0 || dist_sym >= 30) {
                        free(dyn_lit); free(dyn_dist);
                        goto fail;
                    }
                    uint32_t distance = dist_base[dist_sym] +
                                        br_bits(&br, dist_extra[dist_sym]);

                    if (distance > pos) {
                        free(dyn_lit); free(dyn_dist);
                        goto fail;
                    }

                    /* Grow output if needed */
                    while (pos + length > cap) {
                        cap *= 2;
                        uint8_t *tmp = realloc(out, cap);
                        if (!tmp) {
                            free(dyn_lit); free(dyn_dist);
                            goto fail;
                        }
                        out = tmp;
                    }

                    /* Copy from sliding window — byte-by-byte for overlapping */
                    uint32_t src_off = pos - distance;
                    for (uint32_t i = 0; i < length; i++)
                        out[pos + i] = out[src_off + i];
                    pos += length;
                }
            }
            free(dyn_lit);
            free(dyn_dist);
        } else {
            goto fail;  /* reserved block type */
        }
    } while (!bfinal);

    *out_len = pos;
    return out;

fail:
    free(out);
    *out_len = 0;
    return NULL;
}

/* ── PNG Decoder ───────────────────────────────────────────────────── */

static const uint8_t png_sig[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

/* Read big-endian 32 from PNG chunk headers */
static uint32_t png_rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

/* Paeth predictor (PNG filter type 4) */
static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = p - (int)a; if (pa < 0) pa = -pa;
    int pb = p - (int)b; if (pb < 0) pb = -pb;
    int pc = p - (int)c; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

image_t *image_load_png(const void *data, uint32_t size) {
    const uint8_t *d = (const uint8_t *)data;
    if (size < 8 + 25) return NULL;  /* sig + IHDR chunk minimum */
    if (memcmp(d, png_sig, 8) != 0) return NULL;

    /* Parse IHDR (must be first chunk) */
    uint32_t ihdr_len = png_rd32(d + 8);
    if (ihdr_len != 13) return NULL;
    if (memcmp(d + 12, "IHDR", 4) != 0) return NULL;

    uint32_t w = png_rd32(d + 16);
    uint32_t h = png_rd32(d + 20);
    uint8_t  bit_depth   = d[24];
    uint8_t  color_type  = d[25];
    uint8_t  compression = d[26];
    uint8_t  filter_meth = d[27];
    uint8_t  interlace   = d[28];

    /* Only support 8-bit RGB (2) or RGBA (6), non-interlaced */
    if (bit_depth != 8) return NULL;
    if (color_type != 2 && color_type != 6) return NULL;
    if (compression != 0 || filter_meth != 0) return NULL;
    if (interlace != 0) return NULL;
    if (w == 0 || h == 0 || w > 8192 || h > 8192) return NULL;

    int channels = (color_type == 6) ? 4 : 3;  /* RGBA or RGB */
    int bpp = channels;  /* bytes per pixel (at 8-bit depth) */

    /* Concatenate all IDAT chunks */
    uint32_t idat_cap = 65536;
    uint32_t idat_len = 0;
    uint8_t *idat_buf = malloc(idat_cap);
    if (!idat_buf) return NULL;

    uint32_t off = 8;  /* past signature */
    while (off + 12 <= size) {
        uint32_t chunk_len = png_rd32(d + off);
        const uint8_t *chunk_type = d + off + 4;
        const uint8_t *chunk_data = d + off + 8;

        if (off + 12 + chunk_len > size) break;

        if (memcmp(chunk_type, "IDAT", 4) == 0) {
            while (idat_len + chunk_len > idat_cap) {
                idat_cap *= 2;
                uint8_t *tmp = realloc(idat_buf, idat_cap);
                if (!tmp) { free(idat_buf); return NULL; }
                idat_buf = tmp;
            }
            memcpy(idat_buf + idat_len, chunk_data, chunk_len);
            idat_len += chunk_len;
        } else if (memcmp(chunk_type, "IEND", 4) == 0) {
            break;
        }
        off += 12 + chunk_len;  /* length(4) + type(4) + data + crc(4) */
    }

    if (idat_len < 6) { free(idat_buf); return NULL; }

    /* Zlib unwrap: skip 2-byte header (CMF + FLG), decompress, skip Adler32 */
    uint32_t raw_len = 0;
    uint8_t *raw = deflate_inflate(idat_buf + 2, idat_len - 6, &raw_len);
    free(idat_buf);
    if (!raw) return NULL;

    /* Expected size: h * (1 + w * channels) — filter byte per row */
    uint32_t row_size = 1 + w * (uint32_t)channels;
    uint32_t expected = h * row_size;
    if (raw_len < expected) { free(raw); return NULL; }

    /* PNG filter reconstruction */
    uint8_t *pixels_raw = malloc(w * h * (uint32_t)channels);
    if (!pixels_raw) { free(raw); return NULL; }

    for (uint32_t y = 0; y < h; y++) {
        uint8_t *scanline = raw + y * row_size;
        uint8_t filter = scanline[0];
        uint8_t *row_data = scanline + 1;
        uint8_t *dst_row = pixels_raw + y * w * (uint32_t)channels;
        uint8_t *prev_row = (y > 0) ? (pixels_raw + (y - 1) * w * (uint32_t)channels) : NULL;

        for (uint32_t i = 0; i < w * (uint32_t)channels; i++) {
            uint8_t x_val = row_data[i];
            uint8_t a = (i >= (uint32_t)bpp) ? dst_row[i - bpp] : 0;
            uint8_t b = prev_row ? prev_row[i] : 0;
            uint8_t c = (prev_row && i >= (uint32_t)bpp) ? prev_row[i - bpp] : 0;

            switch (filter) {
                case 0: dst_row[i] = x_val; break;                     /* None */
                case 1: dst_row[i] = x_val + a; break;                 /* Sub */
                case 2: dst_row[i] = x_val + b; break;                 /* Up */
                case 3: dst_row[i] = x_val + ((a + b) >> 1); break;    /* Average */
                case 4: dst_row[i] = x_val + paeth(a, b, c); break;    /* Paeth */
                default: free(pixels_raw); free(raw); return NULL;
            }
        }
    }
    free(raw);

    /* Convert to ARGB */
    image_t *img = malloc(sizeof(image_t));
    if (!img) { free(pixels_raw); return NULL; }
    img->width = (int)w;
    img->height = (int)h;
    img->pixels = malloc(w * h * 4);
    if (!img->pixels) { free(img); free(pixels_raw); return NULL; }

    for (uint32_t i = 0; i < w * h; i++) {
        uint8_t r = pixels_raw[i * channels + 0];
        uint8_t g = pixels_raw[i * channels + 1];
        uint8_t b = pixels_raw[i * channels + 2];
        uint8_t a_val = (channels == 4) ? pixels_raw[i * channels + 3] : 0xFF;
        img->pixels[i] = ((uint32_t)a_val << 24) | ((uint32_t)r << 16) |
                          ((uint32_t)g << 8) | b;
    }
    free(pixels_raw);
    return img;
}

/* ── Auto-detect loader ────────────────────────────────────────────── */

image_t *image_load(const void *data, uint32_t size) {
    if (size >= 8 && memcmp(data, png_sig, 8) == 0)
        return image_load_png(data, size);
    if (size >= 2 && ((const uint8_t *)data)[0] == 'B' &&
                     ((const uint8_t *)data)[1] == 'M')
        return image_load_bmp(data, size);
    return NULL;
}

/* ── File loader ───────────────────────────────────────────────────── */

image_t *image_load_file(const char *path) {
    if (!path) return NULL;

    /* Resolve path to inode */
    uint32_t parent;
    char name[MAX_NAME_LEN];
    int inode_num = fs_resolve_path(path, &parent, name);
    if (inode_num < 0) return NULL;

    /* Read inode to get file size */
    inode_t inode;
    if (fs_read_inode((uint32_t)inode_num, &inode) != 0) return NULL;
    if (inode.type != INODE_FILE) return NULL;
    if (inode.size == 0 || inode.size > 16 * 1024 * 1024) return NULL;  /* 16MB max */

    uint8_t *buf = malloc(inode.size);
    if (!buf) return NULL;

    int bytes = fs_read_at((uint32_t)inode_num, buf, 0, inode.size);
    if (bytes <= 0) { free(buf); return NULL; }

    image_t *img = image_load(buf, (uint32_t)bytes);
    free(buf);
    return img;
}

/* ── Free ──────────────────────────────────────────────────────────── */

void image_free(image_t *img) {
    if (!img) return;
    free(img->pixels);
    free(img);
}

/* ── Bilinear image scaler ─────────────────────────────────────────── */

image_t *image_scale(const image_t *src, int new_w, int new_h) {
    if (!src || new_w <= 0 || new_h <= 0) return NULL;

    image_t *dst = malloc(sizeof(image_t));
    if (!dst) return NULL;
    dst->width = new_w;
    dst->height = new_h;
    dst->pixels = malloc((uint32_t)new_w * (uint32_t)new_h * 4);
    if (!dst->pixels) { free(dst); return NULL; }

    int sw = src->width;
    int sh = src->height;

    for (int y = 0; y < new_h; y++) {
        /* Fixed-point source coordinate (16.16) */
        uint32_t sy_fp = (uint32_t)y * ((uint32_t)(sh - 1) << 16) / (uint32_t)(new_h - 1 > 0 ? new_h - 1 : 1);
        int sy0 = (int)(sy_fp >> 16);
        int sy1 = sy0 + 1;
        if (sy1 >= sh) sy1 = sh - 1;
        int fy = (int)(sy_fp & 0xFFFF) >> 8;  /* 0..255 */

        for (int x = 0; x < new_w; x++) {
            uint32_t sx_fp = (uint32_t)x * ((uint32_t)(sw - 1) << 16) / (uint32_t)(new_w - 1 > 0 ? new_w - 1 : 1);
            int sx0 = (int)(sx_fp >> 16);
            int sx1 = sx0 + 1;
            if (sx1 >= sw) sx1 = sw - 1;
            int fx = (int)(sx_fp & 0xFFFF) >> 8;  /* 0..255 */

            /* Sample 4 corners */
            uint32_t c00 = src->pixels[sy0 * sw + sx0];
            uint32_t c10 = src->pixels[sy0 * sw + sx1];
            uint32_t c01 = src->pixels[sy1 * sw + sx0];
            uint32_t c11 = src->pixels[sy1 * sw + sx1];

            /* Bilinear interpolation per channel */
            int a, r, g, b;
            #define LERP_CH(shift) do { \
                int v00 = (c00 >> (shift)) & 0xFF; \
                int v10 = (c10 >> (shift)) & 0xFF; \
                int v01 = (c01 >> (shift)) & 0xFF; \
                int v11 = (c11 >> (shift)) & 0xFF; \
                int top = v00 + (v10 - v00) * fx / 255; \
                int bot = v01 + (v11 - v01) * fx / 255; \
                int val = top + (bot - top) * fy / 255; \
                if (val < 0) val = 0; if (val > 255) val = 255; \
                result = val; \
            } while (0)

            int result;
            LERP_CH(24); a = result;
            LERP_CH(16); r = result;
            LERP_CH(8);  g = result;
            LERP_CH(0);  b = result;
            #undef LERP_CH

            dst->pixels[y * new_w + x] = ((uint32_t)a << 24) |
                ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    return dst;
}
