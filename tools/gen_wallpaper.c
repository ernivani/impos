/* gen_wallpaper.c — Generate a default wallpaper BMP for ImposOS initrd.
 * Produces a 640x400 24bpp gradient BMP (dark blue/purple, ~768KB).
 * Build: cc -o gen_wallpaper gen_wallpaper.c
 * Usage: ./gen_wallpaper > default.bmp
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static void write16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void write32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }

int main(int argc, char **argv) {
    const char *outfile = "default.bmp";
    if (argc > 1) outfile = argv[1];

    int W = 640, H = 400;
    int row_bytes = W * 3;
    int row_stride = (row_bytes + 3) & ~3;
    uint32_t pixel_size = (uint32_t)row_stride * H;
    uint32_t file_size = 54 + pixel_size;

    FILE *f = fopen(outfile, "wb");
    if (!f) { perror(outfile); return 1; }

    /* BITMAPFILEHEADER (14 bytes) */
    fputc('B', f); fputc('M', f);
    write32(f, file_size);
    write16(f, 0); write16(f, 0);  /* reserved */
    write32(f, 54);                 /* pixel data offset */

    /* BITMAPINFOHEADER (40 bytes) */
    write32(f, 40);                 /* header size */
    write32(f, (uint32_t)W);
    write32(f, (uint32_t)H);       /* positive = bottom-up */
    write16(f, 1);                  /* planes */
    write16(f, 24);                 /* bpp */
    write32(f, 0);                  /* BI_RGB */
    write32(f, pixel_size);
    write32(f, 2835); write32(f, 2835);  /* 72 DPI */
    write32(f, 0); write32(f, 0);

    /* Pixel data (bottom-up) */
    uint8_t row[row_stride];
    for (int y = H - 1; y >= 0; y--) {
        memset(row, 0, row_stride);
        for (int x = 0; x < W; x++) {
            /* Dark gradient: top-left purple to bottom-right dark blue */
            int t_y = y * 255 / (H - 1);
            int t_x = x * 255 / (W - 1);

            /* Sky-like gradient with subtle hue shift */
            int r = 10 + t_y * 20 / 255 + t_x * 8 / 255;
            int g = 12 + t_y * 15 / 255 + t_x * 12 / 255;
            int b = 30 + t_y * 40 / 255 + t_x * 25 / 255;

            /* Add soft radial glow at center */
            int dx = x - W / 2, dy = y - H / 3;
            int d2 = dx * dx + dy * dy;
            int max_d2 = (W * W / 4 + H * H / 9);
            if (d2 < max_d2) {
                int glow = 60 * (max_d2 - d2) / max_d2;
                r += glow * 40 / 100;
                g += glow * 20 / 100;
                b += glow * 70 / 100;
            }

            /* Add subtle star dots using deterministic hash */
            unsigned seed = (unsigned)(x * 7919 + y * 104729);
            seed = (seed ^ (seed >> 13)) * 0x5BD1E995;
            seed = seed ^ (seed >> 15);
            if ((seed & 0x3FF) == 0) {
                int star_b = 120 + (int)(seed >> 10) % 136;
                r = r + (star_b - r) * 80 / 100;
                g = g + (star_b - g) * 80 / 100;
                b = b + (star_b + 20 - b) * 80 / 100;
            }

            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;

            /* BMP is BGR */
            row[x * 3 + 0] = (uint8_t)b;
            row[x * 3 + 1] = (uint8_t)g;
            row[x * 3 + 2] = (uint8_t)r;
        }
        fwrite(row, 1, row_stride, f);
    }

    fclose(f);
    return 0;
}
