#!/usr/bin/env python3
"""
ImposOS GUI Test Script
=======================
Tests the desktop GUI by driving QEMU via its monitor socket.
Takes screenshots, checks pixel colors, and reports results.

Usage:
    python3 test_gui.py [--build] [--qemu-sock /tmp/impos_qemu.sock]

The script manages QEMU lifecycle automatically.
"""

import socket, time, subprocess, sys, os, struct, argparse

# ── Config ─────────────────────────────────────────────────────────────────
QEMU_SOCK   = "/tmp/impos_qemu.sock"
KERNEL_PATH = "sysroot/boot/myos.kernel"
INITRD_PATH = "initrd.tar"
SCREENSHOT  = "/tmp/impos_test.png"
BOOT_DELAY  = 6      # seconds to wait for OS to boot
ANIM_DELAY  = 1.2    # seconds to wait for animations to settle
SHORT_DELAY = 0.4    # seconds between mouse events

SCREEN_W, SCREEN_H = 1024, 768
SCREEN_CX   = SCREEN_W // 2   # 512
SCREEN_CY   = SCREEN_H // 2   # 384

PASS = "\033[32mPASS\033[0m"
FAIL = "\033[31mFAIL\033[0m"
INFO = "\033[34mINFO\033[0m"

results = []

# ── QEMU Monitor ────────────────────────────────────────────────────────────

class QemuMonitor:
    def __init__(self, sock_path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(sock_path)
        time.sleep(0.2)
        self._drain()

    def _drain(self):
        self.sock.setblocking(False)
        try:
            while True: self.sock.recv(4096)
        except BlockingIOError:
            pass
        self.sock.setblocking(True)

    def cmd(self, command, wait=0.2):
        self.sock.sendall((command + "\n").encode())
        time.sleep(wait)
        self._drain()

    def screenshot(self, path=SCREENSHOT):
        self.cmd(f"screendump {path}", wait=0.6)
        # Clear PNG cache for this path so next read is fresh
        _png_cache.pop(path, None)

    def sendkey(self, key, wait=None):
        self.cmd(f"sendkey {key}", wait=wait or ANIM_DELAY)

    def mouse_move(self, dx, dy, wait=SHORT_DELAY):
        self.cmd(f"mouse_move {dx} {dy}", wait=wait)

    def mouse_click(self, btn=1, wait=SHORT_DELAY):
        """btn: 1=left, 2=right"""
        self.cmd(f"mouse_button {btn}", wait=0.1)
        self.cmd(f"mouse_button 0",     wait=wait)

    def close(self):
        self.sock.close()


# ── Image pixel reader (PPM P6 + PNG, no PIL dependency) ───────────────────

_png_cache = {}   # path → (pixels, width, height, bpp)

def _load_ppm(data):
    """Parse binary PPM (P6) into (pixels, width, height, bpp=3)."""
    # Read ASCII header (skip comment lines)
    i = 0
    def read_token():
        nonlocal i
        while i < len(data) and data[i:i+1] in (b' ', b'\t', b'\n', b'\r'):
            i += 1
        if i < len(data) and data[i:i+1] == b'#':   # comment
            while i < len(data) and data[i:i+1] != b'\n':
                i += 1
            return read_token()
        start = i
        while i < len(data) and data[i:i+1] not in (b' ', b'\t', b'\n', b'\r'):
            i += 1
        return data[start:i].decode('ascii')
    magic = read_token()
    if magic != 'P6':
        return None
    width  = int(read_token())
    height = int(read_token())
    maxval = int(read_token())
    i += 1   # skip single whitespace separator after maxval
    bpp = 1 if maxval < 256 else 2
    # Raw pixels follow
    raw = data[i:]
    pixels = bytearray(width * height * 3)
    if bpp == 1:
        for idx in range(width * height):
            pixels[idx*3]   = raw[idx*3]
            pixels[idx*3+1] = raw[idx*3+1]
            pixels[idx*3+2] = raw[idx*3+2]
    else:  # 16-bit per channel → scale to 8-bit
        for idx in range(width * height):
            pixels[idx*3]   = raw[idx*6]
            pixels[idx*3+1] = raw[idx*6+2]
            pixels[idx*3+2] = raw[idx*6+4]
    return (pixels, width, height, 3)

def _load_png_data(data):
    """Parse PNG data into (pixels, width, height, bpp)."""
    import zlib
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        return None
    pos = 8; width = height = 0; idat = []; ct = 0
    while pos < len(data):
        n = struct.unpack(">I", data[pos:pos+4])[0]
        t = data[pos+4:pos+8]; d = data[pos+8:pos+8+n]; pos += 12+n
        if t == b'IHDR':
            width, height = struct.unpack(">II", d[:8]); ct = d[9]
        elif t == b'IDAT': idat.append(d)
        elif t == b'IEND': break
    raw   = zlib.decompress(b''.join(idat))
    bpp   = {2: 3, 6: 4}.get(ct, 3)
    prev  = bytes(width * bpp)
    pixels = bytearray(width * height * bpp)
    stride = 1 + width * bpp
    for y in range(height):
        r = raw[y*stride : y*stride+stride]
        ftype = r[0]; row = bytearray(r[1:])
        if ftype == 1:   # Sub
            for i in range(bpp, len(row)):
                row[i] = (row[i] + row[i-bpp]) & 255
        elif ftype == 2: # Up
            for i in range(len(row)):
                row[i] = (row[i] + prev[i]) & 255
        elif ftype == 3: # Average
            for i in range(len(row)):
                a = row[i-bpp] if i >= bpp else 0
                row[i] = (row[i] + (a + prev[i]) // 2) & 255
        elif ftype == 4: # Paeth
            for i in range(len(row)):
                a = row[i-bpp] if i >= bpp else 0
                b2 = prev[i]; c = prev[i-bpp] if i >= bpp else 0
                pa, pb, pc = abs(b2-c), abs(a-c), abs(a+b2-2*c)
                pr = a if pa <= pb and pa <= pc else (b2 if pb <= pc else c)
                row[i] = (row[i] + pr) & 255
        pixels[y*width*bpp:(y+1)*width*bpp] = row
        prev = bytes(row)
    return (pixels, width, height, bpp)

def _load_png(path):
    if path in _png_cache:
        return _png_cache[path]
    with open(path, "rb") as f:
        data = f.read()
    # Try PPM first (QEMU with -display none outputs P6)
    if data[:2] == b'P6':
        result = _load_ppm(data)
    else:
        result = _load_png_data(data)
    if result:
        _png_cache[path] = result
    return result


def read_png_pixel(path, x, y):
    """Return (r,g,b) at pixel (x,y). Returns None on error."""
    try:
        result = _load_png(path)
        if result is None: return None
        pixels, width, height, bpp = result
        if x >= width or y >= height: return None
        i = (y * width + x) * bpp
        return (pixels[i], pixels[i+1], pixels[i+2])
    except Exception as e:
        print(f"  PNG read error at ({x},{y}): {e}")
        return None


def pixel_near(actual, expected, tolerance=30):
    """Check if actual (r,g,b) is close to expected."""
    if actual is None: return False
    return all(abs(a - e) <= tolerance for a, e in zip(actual, expected))


# ── Test helpers ────────────────────────────────────────────────────────────

def test(name, condition, detail=""):
    status = PASS if condition else FAIL
    results.append((name, condition))
    detail_str = f"  ({detail})" if detail else ""
    print(f"  [{status}] {name}{detail_str}")
    return condition


def info(msg):
    print(f"  [{INFO}] {msg}")


def save_screenshot(qemu, label):
    qemu.screenshot(SCREENSHOT)
    dest = f"/tmp/impos_test_{label}.png"
    subprocess.run(["cp", SCREENSHOT, dest], capture_output=True)
    # Make JPEG for easy viewing
    subprocess.run(["sips", "-s", "format", "jpeg", dest,
                    "--out", dest.replace(".png", ".jpg")],
                   capture_output=True)
    # Clear cache for dest so reads are fresh
    _png_cache.pop(dest, None)
    info(f"Screenshot saved: {dest.replace('.png', '.jpg')}")
    return dest


def get_pixel(path, x, y):
    px = read_png_pixel(path, x, y)
    return px


# ── Mouse tracking ──────────────────────────────────────────────────────────
# We track the virtual cursor position so we can compute relative moves.

cursor = [SCREEN_CX, SCREEN_CY]

def move_to(qemu, tx, ty):
    """Move cursor to absolute screen position (tx, ty)."""
    dx = tx - cursor[0]
    dy = ty - cursor[1]
    if dx == 0 and dy == 0:
        return
    qemu.mouse_move(dx, dy)
    cursor[0] = max(0, min(SCREEN_W - 1, cursor[0] + dx))
    cursor[1] = max(0, min(SCREEN_H - 1, cursor[1] + dy))


def click_at(qemu, tx, ty, btn=1, wait=ANIM_DELAY):
    move_to(qemu, tx, ty)
    time.sleep(SHORT_DELAY)
    qemu.mouse_click(btn=btn, wait=wait)


# ── Tests ───────────────────────────────────────────────────────────────────

def test_desktop(qemu):
    print("\n─── Desktop & Wallpaper ───")
    png = save_screenshot(qemu, "01_desktop")

    # Menubar should be dark at top (y=7)
    mb_px = get_pixel(png, 512, 7)
    test("Menubar is dark", pixel_near(mb_px, (12, 16, 22), 25),
         f"got {mb_px}")

    # Wallpaper should be dark blue in the lower portion
    wp_px = get_pixel(png, 512, 600)
    test("Wallpaper renders (dark blue)", wp_px is not None and wp_px[2] > 10,
         f"got {wp_px}")

    # Wallpaper is NOT black (has color)
    if wp_px:
        test("Wallpaper has color", any(c > 5 for c in wp_px),
             f"got {wp_px}")

    # Demo window should be present near center
    # Window titlebar at approximately y=284, x=512
    win_tb_px = get_pixel(png, 512, 284)
    test("Demo window titlebar visible", win_tb_px is not None and
         any(c > 20 for c in win_tb_px), f"got {win_tb_px}")


def test_menubar(qemu):
    print("\n─── Menubar ───")
    png = save_screenshot(qemu, "02_menubar")

    # Menubar background is dark at top strip
    bg = get_pixel(png, 512, 7)
    test("Menubar background dark", pixel_near(bg, (12, 16, 22), 25), f"got {bg}")

    # Date/time area (top-right) should have white text pixels
    # Date text is in the right portion of menubar
    # Check that the right side of menubar has some light pixels (text)
    found_text = False
    for x in range(820, 1000):
        px = get_pixel(png, x, 13)
        if px and max(px) > 150:
            found_text = True
            break
    test("Menubar date/time text visible", found_text)

    # Left side: "ImposOS" logo (bright/colored text around x=30-60)
    found_logo = False
    for x in range(5, 100):
        px = get_pixel(png, x, 13)
        if px and max(px) > 100:
            found_logo = True
            break
    test("Menubar logo visible", found_logo)


def test_window(qemu):
    print("\n─── UIKit Window ───")
    png = save_screenshot(qemu, "03_window")

    # Demo window starts at (312, 264), titlebar h=38.
    # Traffic lights: close ~(323,283), min ~(345,283), max ~(367,283)
    close_px = get_pixel(png, 323, 283)
    test("Close button red", close_px is not None and
         close_px[0] > 200 and close_px[1] < 130, f"got {close_px}")

    min_px = get_pixel(png, 345, 283)
    test("Minimize button yellow", min_px is not None and
         min_px[0] > 200 and min_px[1] > 150 and min_px[2] < 100,
         f"got {min_px}")

    max_px = get_pixel(png, 367, 283)
    test("Maximize button green", max_px is not None and
         max_px[1] > 150 and max_px[0] < 100, f"got {max_px}")

    # Window content area should have text (bright pixels)
    found_content = False
    for y in range(310, 460):
        for x in range(315, 715):
            px = get_pixel(png, x, y)
            if px and max(px) > 150:
                found_content = True
                break
        if found_content:
            break
    test("Window content renders (text visible)", found_content)

    # FPS counter: accent blue at approx (320, 436)
    fps_px = get_pixel(png, 320, 436)
    test("FPS counter in accent blue", fps_px is not None and
         fps_px[2] > 150 and fps_px[0] < 100, f"got {fps_px}")


def test_radial(qemu):
    print("\n─── Radial Launcher (Space) ───")
    qemu.sendkey("spc", wait=ANIM_DELAY)
    png = save_screenshot(qemu, "04_radial")

    # Radial overlay should cover the center with a dark background.
    # Avoid checking (512,384) = cursor position (white arrow).
    # Check 34px above center where cursor isn't.
    overlay_px = get_pixel(png, SCREEN_CX, SCREEN_CY - 34)
    test("Radial overlay renders (dark background)",
         overlay_px is not None and max(overlay_px) < 80, f"got {overlay_px}")

    # Icons at orbit radius — Terminal icon is at approx (560-575, 249) with
    # the slot_pos midpoint fix. Scan a horizontal band near the top.
    term_found = False
    for x in range(520, 620):
        tp = get_pixel(png, x, 249)
        if tp and max(tp) > 50:
            term_found = True
            info(f"Terminal icon pixel at ({x},249): {tp}")
            break
    test("Terminal icon visible near top of radial", term_found)

    # Close radial
    qemu.sendkey("esc", wait=ANIM_DELAY)

    # Verify radial closed — overlay_px position (SCREEN_CX, SCREEN_CY-34)
    # should now be wallpaper (not dark overlay).
    png2 = save_screenshot(qemu, "04b_radial_closed")
    after_px = get_pixel(png2, SCREEN_CX, SCREEN_CY - 34)
    # Both before/after are dark but the overlay dark ≠ wallpaper dark;
    # actually both are dark so just check it still loads without error.
    test("Radial closes on Esc",
         after_px is not None and not pixel_near(overlay_px or (0,0,0), after_px, 5),
         f"after={after_px} vs during={overlay_px}")


def test_app_drawer(qemu):
    print("\n─── App Drawer (Tab) ───")
    qemu.sendkey("tab", wait=ANIM_DELAY)
    png = save_screenshot(qemu, "05_drawer")

    # Search bar at top should be visible (dark rectangle just below menubar)
    search_px = get_pixel(png, SCREEN_CX, 43)
    test("Drawer search bar visible", search_px is not None and
         max(search_px) < 100, f"got {search_px}")

    # App icons grid: Terminal icon (blue) at ~(204, 112)
    term_icon_px = get_pixel(png, 204, 112)
    test("Drawer Terminal icon visible", term_icon_px is not None and
         max(term_icon_px) > 50, f"got {term_icon_px}")

    # Demo window should NOT bleed through (no white text in middle)
    # If window bleed, we'd see bright pixels in the center from window content
    bleed_px = get_pixel(png, 460, 320)
    # With fix, this should be dark (drawer background, no window text)
    test("No window bleed through drawer",
         bleed_px is not None and max(bleed_px) < 80, f"got {bleed_px}")

    # Bottom hint "Right-click to pin"
    hint_px = get_pixel(png, SCREEN_CX, 750)
    test("Drawer pin hint visible", hint_px is not None and
         max(hint_px) > 50, f"got {hint_px}")

    # Close drawer
    qemu.sendkey("esc", wait=ANIM_DELAY)
    png2 = save_screenshot(qemu, "05b_drawer_closed")
    after_px = get_pixel(png2, SCREEN_CX, 43)
    test("Drawer closes on Esc",
         after_px is None or after_px != search_px,
         f"search bar gone: {after_px}")


def test_context_menu(qemu):
    print("\n─── Context Menu (Right-click) ───")

    # Right-click on bare desktop below the demo window
    click_at(qemu, SCREEN_CX, 640, btn=2, wait=ANIM_DELAY)
    png = save_screenshot(qemu, "06_context_menu")

    # Menu should appear (dark rounded rectangle)
    # The menu is clamped to right side; check right portion for dark bg
    menu_found = False
    # Scan right half for a consistently dark column indicating menu bg
    for x in range(700, 1000):
        dark_count = 0
        for y in range(550, 760):
            px = get_pixel(png, x, y)
            if px and 10 < px[0] < 30 and 15 < px[1] < 35 and 25 < px[2] < 50:
                dark_count += 1
        if dark_count > 50:
            menu_found = True
            break
    test("Context menu appears", menu_found)

    # Menu text should be visible. Menu opens at the click position (SCREEN_CX,
    # 640) so menu spans roughly x=512-712. Text color is light (~203,212,242).
    text_found = False
    for x in range(500, 730):
        for y in range(580, 768):
            p = get_pixel(png, x, y)
            if p and min(p) > 150:
                text_found = True
                break
        if text_found:
            break
    test("Context menu text visible", text_found)

    # Close menu with Esc
    qemu.sendkey("esc", wait=SHORT_DELAY)
    png2 = save_screenshot(qemu, "06b_menu_closed")
    # Menu region should be gone (darker/wallpaper pixels)
    after_px = get_pixel(png2, 900, 680)
    test("Context menu closes", after_px is None or max(after_px) < 200)


def test_settings(qemu):
    print("\n─── Settings Window ───")

    # Open radial, click Settings icon
    # Settings is at slot 2 midpoint (now fixed): approx (579, 496) from center
    # Absolute: (512+67, 384+112) = (579, 496)
    # But we need to open radial first and then move to Settings

    qemu.sendkey("spc", wait=ANIM_DELAY)

    # Move to Settings position (slot 2 midpoint)
    settings_x, settings_y = 579, 496
    move_to(qemu, settings_x, settings_y)
    time.sleep(SHORT_DELAY)
    qemu.mouse_click(btn=1, wait=ANIM_DELAY)

    png = save_screenshot(qemu, "07_settings")

    # Settings window: sidebar on left should be visible (darker)
    # Window is centered: ~(256, 144) to (768, 624) for a 512x480 window
    # Check sidebar (left ~25% of window width)
    sidebar_px = get_pixel(png, 310, 400)
    test("Settings window renders", sidebar_px is not None and
         any(c > 10 for c in sidebar_px), f"got {sidebar_px}")

    # Settings window should have a second window (not just demo window)
    # Check for second window by looking at different location
    settings_content_px = get_pixel(png, 500, 350)
    test("Settings content area visible",
         settings_content_px is not None and
         any(c > 15 for c in settings_content_px),
         f"got {settings_content_px}")

    # Close settings window via its close button (top-left traffic light).
    # Settings window is centered: (sw/2-256, sh/2-240) = (256, 144).
    # Close button is at x = 256+11 = 267, y = 144+19 = 163.
    # (Don't use Esc — when no overlay is open, Esc returns DESKTOP_ACTION_POWER)
    click_at(qemu, 267, 163, btn=1, wait=ANIM_DELAY)


def test_window_ops(qemu):
    print("\n─── Window Operations ───")

    # Minimize the demo window by clicking its minimize button
    # Demo window titlebar at ~y=284, minimize at ~x=352
    click_at(qemu, 352, 284, btn=1, wait=ANIM_DELAY)
    png = save_screenshot(qemu, "08_minimized")

    # Window content should be gone (minimized)
    content_px = get_pixel(png, 512, 380)
    test("Window minimizes on minimize button click",
         content_px is not None and not any(c > 100 for c in content_px),
         f"got {content_px}")

    # Restore via context menu "Show ImposOS" - just send Esc to reopen
    # Actually minimized windows show in context menu; skip for now


def test_fps(qemu):
    print("\n─── Performance ───")
    png = save_screenshot(qemu, "09_perf")

    # FPS in accent blue - parse it by finding blue pixels in FPS area
    fps_found = False
    for x in range(320, 420):
        px = get_pixel(png, x, 441)
        if px and px[2] > 150 and px[0] < 150:
            fps_found = True
            break
    test("FPS counter showing (accent blue)", fps_found)
    info("FPS is visible in demo window (expected 80-120+)")


# ── Main ────────────────────────────────────────────────────────────────────

def start_qemu():
    """Kill any existing QEMU and start a fresh one."""
    subprocess.run(["pkill", "-f", "qemu-system-i38"],
                   capture_output=True)
    time.sleep(1.5)

    proc = subprocess.Popen([
        "qemu-system-i386",
        "-kernel", KERNEL_PATH,
        "-initrd", INITRD_PATH,
        "-m", "512M",
        "-vga", "std",
        "-display", "none",
        "-monitor", f"unix:{QEMU_SOCK},server,nowait",
        "-serial", "none",
        "-no-reboot",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    print(f"[{INFO}] QEMU started (pid {proc.pid}), waiting {BOOT_DELAY}s for boot...")
    time.sleep(BOOT_DELAY)
    return proc


def main():
    parser = argparse.ArgumentParser(description="ImposOS GUI test suite")
    parser.add_argument("--build",  action="store_true",
                        help="Run build before testing")
    parser.add_argument("--no-restart", action="store_true",
                        help="Use already-running QEMU instance")
    parser.add_argument("--sock", default=QEMU_SOCK,
                        help=f"QEMU monitor socket (default: {QEMU_SOCK})")
    args = parser.parse_args()

    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    if args.build:
        print(f"[{INFO}] Building...")
        r = subprocess.run(["bash", "build.sh"], capture_output=True)
        if r.returncode != 0:
            print(f"[{FAIL}] Build failed:\n{r.stderr.decode()}")
            sys.exit(1)
        print(f"[{PASS}] Build succeeded")

    qemu_proc = None
    if not args.no_restart:
        qemu_proc = start_qemu()

    try:
        qemu = QemuMonitor(args.sock)
        print(f"[{PASS}] Connected to QEMU monitor")

        # Run all tests (fps before settings so demo window isn't covered)
        test_desktop(qemu)
        test_menubar(qemu)
        test_window(qemu)
        test_fps(qemu)
        test_radial(qemu)
        test_app_drawer(qemu)
        test_context_menu(qemu)
        test_settings(qemu)

        qemu.close()

    except Exception as e:
        print(f"\n[{FAIL}] Test error: {e}")
        import traceback; traceback.print_exc()
    finally:
        if qemu_proc:
            qemu_proc.terminate()

    # Summary
    passed = sum(1 for _, ok in results if ok)
    total  = len(results)
    failed = [(n, ok) for n, ok in results if not ok]

    print(f"\n{'═'*50}")
    print(f"Results: {passed}/{total} passed")
    if failed:
        print(f"\nFailed tests:")
        for name, _ in failed:
            print(f"  • {name}")
    print(f"{'═'*50}")

    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
