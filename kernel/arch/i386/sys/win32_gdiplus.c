#include <kernel/win32_types.h>
#include <string.h>

/* ── GDI+ Flat API Stubs ─────────────────────────────────────── */
/* Chromium/Skia calls these via gdiplus.dll. We provide minimal stubs
 * that return success (Ok=0) so that initialization proceeds. */

/* GDI+ status codes */
#define GdipOk  0

/* GdiplusStartup token */
static uint32_t gdiplus_token = 0;

typedef struct {
    uint32_t GdiplusVersion;
    void *DebugEventCallback;
    int SuppressBackgroundThread;
    int SuppressExternalCodecs;
} GdiplusStartupInput;

typedef struct {
    void *NotificationHook;
    void *NotificationUnhook;
} GdiplusStartupOutput;

static int WINAPI shim_GdiplusStartup(uint32_t *token, const GdiplusStartupInput *input,
                                        GdiplusStartupOutput *output)
{
    (void)input;
    if (output) memset(output, 0, sizeof(GdiplusStartupOutput));
    gdiplus_token = 1;
    if (token) *token = gdiplus_token;
    return GdipOk;
}

static void WINAPI shim_GdiplusShutdown(uint32_t token) {
    (void)token;
    gdiplus_token = 0;
}

static int WINAPI shim_GdipCreateFromHDC(uint32_t hdc, uint32_t *graphics) {
    (void)hdc;
    if (graphics) *graphics = 0xD1000001;
    return GdipOk;
}

static int WINAPI shim_GdipDeleteGraphics(uint32_t graphics) {
    (void)graphics;
    return GdipOk;
}

static int WINAPI shim_GdipCreateBitmapFromScan0(
    int width, int height, int stride, int format, void *scan0, uint32_t *bitmap)
{
    (void)width; (void)height; (void)stride; (void)format; (void)scan0;
    if (bitmap) *bitmap = 0xD2000001;
    return GdipOk;
}

static int WINAPI shim_GdipDisposeImage(uint32_t image) {
    (void)image;
    return GdipOk;
}

static int WINAPI shim_GdipDrawImageI(uint32_t graphics, uint32_t image, int x, int y) {
    (void)graphics; (void)image; (void)x; (void)y;
    return GdipOk;
}

static int WINAPI shim_GdipSetSmoothingMode(uint32_t graphics, int mode) {
    (void)graphics; (void)mode;
    return GdipOk;
}

static int WINAPI shim_GdipSetTextRenderingHint(uint32_t graphics, int hint) {
    (void)graphics; (void)hint;
    return GdipOk;
}

/* ── Export Table ─────────────────────────────────────────────── */

static const win32_export_entry_t gdiplus_exports[] = {
    { "GdiplusStartup",            (void *)shim_GdiplusStartup },
    { "GdiplusShutdown",           (void *)shim_GdiplusShutdown },
    { "GdipCreateFromHDC",         (void *)shim_GdipCreateFromHDC },
    { "GdipDeleteGraphics",        (void *)shim_GdipDeleteGraphics },
    { "GdipCreateBitmapFromScan0", (void *)shim_GdipCreateBitmapFromScan0 },
    { "GdipDisposeImage",          (void *)shim_GdipDisposeImage },
    { "GdipDrawImageI",            (void *)shim_GdipDrawImageI },
    { "GdipSetSmoothingMode",      (void *)shim_GdipSetSmoothingMode },
    { "GdipSetTextRenderingHint",  (void *)shim_GdipSetTextRenderingHint },
};

const win32_dll_shim_t win32_gdiplus = {
    .dll_name = "gdiplus.dll",
    .exports = gdiplus_exports,
    .num_exports = sizeof(gdiplus_exports) / sizeof(gdiplus_exports[0]),
};
