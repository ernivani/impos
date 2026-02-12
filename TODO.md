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

## Tier 1.5 — Win32 Bridge → Chromium Roadmap

### Phase 1: Threading & Synchronization
_Chromium spawns 20+ threads on startup. Nothing complex runs without this._
- [x] CreateThread / ExitThread / TerminateThread — map to task_create_thread()
- [x] Thread Local Storage — TlsAlloc, TlsFree, TlsGetValue, TlsSetValue
- [x] Critical sections — InitializeCriticalSection, EnterCriticalSection, LeaveCriticalSection, DeleteCriticalSection
- [x] Events — CreateEventA, SetEvent, ResetEvent, WaitForSingleObject
- [x] Mutexes — CreateMutexA, ReleaseMutex, WaitForSingleObject
- [x] Semaphores — CreateSemaphoreA, ReleaseSemaphore
- [x] WaitForSingleObject / WaitForMultipleObjects — central dispatcher for all sync primitives
- [x] Interlocked functions — InterlockedIncrement, InterlockedDecrement, InterlockedExchange, InterlockedCompareExchange
- [x] _beginthreadex / _endthreadex (msvcrt wrappers)

### Phase 2: Memory Management
_Chromium uses ~200MB+ and relies on virtual memory protection._
- [x] VirtualAlloc with real page protection — PAGE_READWRITE, PAGE_EXECUTE_READ, PAGE_NOACCESS via paging PTE flags
- [x] VirtualProtect — change page permissions after allocation
- [x] VirtualQuery — query region info (used by allocators to introspect)
- [x] Memory-mapped files — CreateFileMappingA, MapViewOfFile, UnmapViewOfFile
- [x] Heap improvements — HeapAlloc alignment guarantees, large block support
- [x] GlobalAlloc / GlobalFree / GlobalLock / GlobalUnlock (legacy clipboard/OLE)

### Phase 3: File System & I/O
_Chromium reads configs, caches, temp files, and needs directory enumeration._
- [x] FindFirstFileA / FindNextFileA / FindClose — directory enumeration
- [x] GetFileAttributesA / GetFileSize / GetFileType
- [x] SetFilePointer / SetEndOfFile — random access
- [x] CreateDirectoryA / RemoveDirectoryA
- [x] GetTempPathA / GetTempFileNameA
- [x] DeleteFileA / MoveFileA / CopyFileA
- [x] GetModuleFileNameA — exe path query
- [x] GetCurrentDirectoryA / SetCurrentDirectoryA
- [x] GetFullPathNameA / GetLongPathNameA
- [x] Overlapped I/O stubs — ReadFile/WriteFile with OVERLAPPED (can stub as sync)

### Phase 4: Process Creation
_Chromium is multi-process: browser, renderer, GPU, utility, crashpad._
- [x] CreateProcessA — spawn child .exe with inherited handles, command line, environment
- [x] GetExitCodeProcess / TerminateProcess
- [x] WaitForSingleObject on process handle
- [x] Per-process address spaces — each PE gets its own page directory, COW or full copy
- [x] Inter-process pipe bridging — child stdout/stderr → parent via anonymous pipes
- [x] DuplicateHandle — share handles between processes
- [x] GetCurrentProcess / OpenProcess

### Phase 5: DLL Loading
_Chromium loads 50+ DLLs. Need real DLL file loading, not just hardcoded shim tables._
- [ ] LoadLibraryA / LoadLibraryExA — load PE DLL from disk, relocate, call DllMain
- [ ] FreeLibrary — unload DLL, decrement refcount
- [ ] GetProcAddress on loaded DLLs — resolve exports from actual PE export table
- [ ] DLL search order — exe directory → /apps → system shims
- [ ] DLL dependency chain — recursive import resolution on DLL load
- [ ] Delay-load imports — resolve on first call (Chromium uses these)
- [ ] Known DLL shim fallback — if real DLL not found, fall back to internal shim tables

### Phase 6: Registry Emulation
_Chromium reads ~100 registry keys on startup for system info, proxy settings, font config._
- [ ] In-memory registry tree — hierarchical key/value store backed by filesystem
- [ ] RegOpenKeyExA / RegCloseKey
- [ ] RegQueryValueExA — REG_SZ, REG_DWORD, REG_BINARY types
- [ ] RegEnumKeyExA / RegEnumValueA
- [ ] RegCreateKeyExA / RegSetValueExA (for Chromium writing prefs)
- [ ] Pre-populated keys — HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion (OS version), HKCU\Software\Google\Chrome, font registry

### Phase 7: Winsock Networking
_Chromium has its own HTTP/QUIC stack but needs Winsock for raw sockets._
- [ ] WSAStartup / WSACleanup
- [ ] socket / closesocket — map to ImposOS socket API
- [ ] connect / bind / listen / accept
- [ ] send / recv / sendto / recvfrom
- [ ] select / WSAPoll — fd_set multiplexing
- [ ] getaddrinfo / freeaddrinfo — DNS via ImposOS dns_resolve
- [ ] gethostname / gethostbyname
- [ ] setsockopt / getsockopt (SO_REUSEADDR, TCP_NODELAY, etc.)
- [ ] ioctlsocket — FIONBIO for non-blocking mode
- [ ] ws2_32.dll shim table

### Phase 8: Advanced GDI & Rendering
_Chromium uses Skia which calls into GDI/GDI+ and Direct2D/DirectWrite._
- [ ] CreateCompatibleDC / CreateCompatibleBitmap / CreateDIBSection — offscreen rendering
- [ ] StretchBlt / StretchDIBits / SetDIBitsToDevice — image blitting
- [ ] Pen/brush improvements — CreatePen, LineTo, MoveToEx, Polygon, Polyline
- [ ] CreateFontIndirectA with real font matching
- [ ] GetTextMetricsA / GetTextExtentPoint32A — text measurement
- [ ] SaveDC / RestoreDC — GDI state stack
- [ ] SetViewportOrgEx / SetWindowOrgEx — coordinate transforms
- [ ] ClipRect / SelectClipRgn — clipping regions
- [ ] GDI+ flat API stubs — GdipCreateFromHDC, GdipDrawImage, GdipCreateBitmapFromScan0
- [ ] EnumFontFamiliesExA — font enumeration

### Phase 9: COM & OLE Foundation
_Chromium uses COM for accessibility, drag-drop, shell integration, audio._
- [ ] CoInitializeEx / CoUninitialize
- [ ] CoCreateInstance — object factory with CLSID dispatch
- [ ] IUnknown vtable convention — QueryInterface/AddRef/Release
- [ ] IMalloc / CoTaskMemAlloc / CoTaskMemFree
- [ ] OLE clipboard — OleSetClipboard, OleGetClipboard
- [ ] IDropTarget / IDropSource stubs (drag-and-drop)
- [ ] Shell COM — SHGetFolderPathA, SHCreateDirectoryExA
- [ ] ole32.dll + shell32.dll shim tables

### Phase 10: Unicode & Wide String APIs
_Chromium is a Unicode application. Most real calls use W (wide) variants._
- [ ] W-suffix versions of all kernel32/user32/gdi32 APIs (CreateFileW, CreateWindowExW, etc.)
- [ ] UTF-8 ↔ UTF-16 conversion layer (MultiByteToWideChar/WideCharToMultiByte already stubbed)
- [ ] wcslen, wcscpy, wcscat, wcscmp, wprintf, swprintf in msvcrt shim
- [ ] Internal string handling: store as UTF-8, convert at API boundary

### Phase 11: Security & Crypto APIs
_Chromium verifies TLS certificates and uses Windows crypto for key storage._
- [ ] CryptAcquireContextA / CryptReleaseContext
- [ ] CryptGenRandom — map to prng_random()
- [ ] CertOpenStore / CertFindCertificateInStore / CertFreeCertificateContext
- [ ] BCryptOpenAlgorithmProvider / BCryptGenRandom (modern crypto API)
- [ ] advapi32.dll + crypt32.dll + bcrypt.dll shim tables
- [ ] Embedded root CA certificates for TLS verification

### Phase 12: Structured Exception Handling
_Chromium uses SEH for crash handling (crashpad). C++ exceptions also need this._
- [ ] _except_handler3 / _except_handler4 — SEH frame walker
- [ ] RtlUnwind — stack unwinding
- [ ] SetUnhandledExceptionFilter — top-level crash handler
- [ ] __CxxFrameHandler3 — C++ exception support (try/catch via SEH)
- [ ] Per-thread exception chain via FS:[0] (TIB)

### Phase 13: Miscellaneous Win32
_The long tail of APIs that Chromium calls on startup._
- [ ] GetSystemInfo / GetNativeSystemInfo — CPU count, page size, arch
- [ ] GetVersionExA / IsProcessorFeaturePresent — OS version queries
- [ ] GetEnvironmentVariableA / SetEnvironmentVariableA
- [ ] OutputDebugStringA — map to serial debug log
- [ ] GetUserNameA — map to user_get_current()
- [ ] FormatMessageA — error code to string
- [ ] GetLocaleInfoA / GetUserDefaultLCID — locale stubs (return en-US)
- [ ] GetTimeZoneInformation / GetLocalTime / GetSystemTime / FileTimeToSystemTime
- [ ] kernel32 thread pool stubs — QueueUserWorkItem, CreateTimerQueue
- [ ] advapi32 security stubs — GetTokenInformation, OpenProcessToken

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
- [x] HTTPS/TLS — TLS 1.2 client with RSA + ECDHE (P-256), async downloads
- [ ] ELF binary loading
- [ ] Shared libraries / dynamic linking
- [ ] Journaling filesystem
- [x] Package manager — winget with /apps directory, HTTPS downloads
- [ ] Init system / service manager
- [ ] IPv6
- [ ] AHCI/SATA
