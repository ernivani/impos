# ImposOS — Missing Features TODO

## Tier 1 — High Impact & Visual
- [x] Clipboard (copy/paste) — Ctrl+C/V system-wide
- [x] Right-click context menus — desktop, file manager, everywhere
- [x] Desktop icons — files/folders/shortcuts on desktop background
- [x] Alt-Tab visual switcher — Ctrl+Tab overlay showing open windows
- [x] Notifications/toasts — system-wide popup messages with swipe-to-dismiss
- [x] Tab completion in shell — filenames and commands
- [x] File timestamps — created/modified/accessed times in inode, shown in ls -l
- [x] Sound driver (PC speaker) — beeps
- [x] RTC (real-time clock) — read time from CMOS, persist across reboots
- [x] Implentation of the command winget - a package manager for Windows applications
- [x] PE executable loader — parse PE32 headers, load .exe sections into memory, set up entry point
- [x] Win32 API shim: kernel32 — CreateFile, ReadFile, WriteFile, CloseHandle, ExitProcess, VirtualAlloc, GetStdHandle, GetCommandLine, heap functions
- [x] Win32 API shim: user32 — CreateWindowEx, ShowWindow, GetMessage, DispatchMessage, DefWindowProc, MessageBox, SetWindowText, GetClientRect
- [x] Win32 API shim: gdi32 — GetDC, BitBlt, TextOut, SetPixel, CreateSolidBrush, FillRect, CreateFont, SelectObject, BeginPaint/EndPaint
- [x] Win32 API shim: msvcrt — printf, scanf, malloc, free, fopen, fclose, fread, fwrite, strlen, memcpy (C runtime bridge)
- [x] PE import table resolver — walk import directory, match DLL names to internal shim tables, patch IAT with function pointers
- [x] Win32 message loop integration — translate ImposOS ui_event (mouse/keyboard) into Win32 MSG structs (WM_PAINT, WM_KEYDOWN, WM_LBUTTONDOWN, etc.)
- [x] Win32 window ↔ WM bridge — each CreateWindowEx maps to a wm_create_window, GDI paints render to window canvas, window events route back as Win32 messages
- [x] .exe file association — double-click .exe in file manager or type name in shell to load and run via PE loader
- [x] Console subsystem for Win32 — detect PE subsystem type; console apps get a terminal window, GUI apps get a WM window

## Tier 2 — Desktop UX (ship-critical)
- [ ] Wallpaper picker — 6-8 built-in gradients/patterns selectable from Settings > Display
- [ ] Window snapping — drag to left/right edge = half-screen, top = maximize, Super+Arrow keys
- [ ] Calendar dropdown — click menubar clock to show month view with today highlighted
- [ ] Lock screen — Super+L or power menu "Lock", password required to unlock
- [ ] Persistent settings — save user preferences (wallpaper, layout, hostname) to disk, reload on boot
- [ ] Terminal as a windowed app — shell runs inside a WM window instead of fullscreen VGA
- [ ] Shutdown/restart confirmation dialog — "Are you sure?" modal before power actions
- [ ] System tray dropdowns — click WiFi icon = network info, click username = logout/lock/about

## Tier 3 — Shell & CLI
- [ ] Shell output redirection (>, >>)
- [ ] Background jobs (&, fg/bg)
- [ ] Wildcards/globbing (*.txt)
- [ ] System logging (dmesg, /var/log)
- [ ] fork/exec — proper process creation

## Tier 4 — Virtual Desktops & Animations
- [ ] Virtual desktops/workspaces — Ctrl+Left/Right to switch, indicator in menubar
- [ ] Window open/close animations — scale-up on open, fade on close
- [ ] Smooth scrolling — pixel-level scroll in file manager, editor, lists
- [ ] Drag-and-drop between apps — drag file from Files to desktop or Editor

## Tier 5 — Advanced / Kernel
- [ ] USB stack
- [ ] Demand paging + swap
- [ ] HTTPS/TLS
- [ ] ELF binary loading
- [ ] Shared libraries / dynamic linking
- [ ] Journaling filesystem
- [ ] Package manager
- [ ] Init system / service manager
- [ ] IPv6
- [ ] AHCI/SATA
