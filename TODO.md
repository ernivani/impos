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
- [x] LoadLibraryA / LoadLibraryExA — load PE DLL from disk, relocate, call DllMain
- [x] FreeLibrary — unload DLL, decrement refcount
- [x] GetProcAddress on loaded DLLs — resolve exports from actual PE export table
- [x] DLL search order — exe directory → /apps → system shims
- [x] DLL dependency chain — recursive import resolution on DLL load
- [x] Delay-load imports — resolve on first call (Chromium uses these)
- [x] Known DLL shim fallback — if real DLL not found, fall back to internal shim tables

### Phase 6: Registry Emulation
_Chromium reads ~100 registry keys on startup for system info, proxy settings, font config._
- [x] In-memory registry tree — hierarchical key/value store backed by filesystem
- [x] RegOpenKeyExA / RegCloseKey
- [x] RegQueryValueExA — REG_SZ, REG_DWORD, REG_BINARY types
- [x] RegEnumKeyExA / RegEnumValueA
- [x] RegCreateKeyExA / RegSetValueExA (for Chromium writing prefs)
- [x] Pre-populated keys — HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion (OS version), HKCU\Software\Google\Chrome, font registry

### Phase 7: Winsock Networking
_Chromium has its own HTTP/QUIC stack but needs Winsock for raw sockets._
- [x] WSAStartup / WSACleanup
- [x] socket / closesocket — map to ImposOS socket API
- [x] connect / bind / listen / accept
- [x] send / recv / sendto / recvfrom
- [x] select / WSAPoll — fd_set multiplexing
- [x] getaddrinfo / freeaddrinfo — DNS via ImposOS dns_resolve
- [x] gethostname / gethostbyname
- [x] setsockopt / getsockopt (SO_REUSEADDR, TCP_NODELAY, etc.)
- [x] ioctlsocket — FIONBIO for non-blocking mode
- [x] ws2_32.dll shim table

### Phase 8: Advanced GDI & Rendering
_Chromium uses Skia which calls into GDI/GDI+ and Direct2D/DirectWrite._
- [x] CreateCompatibleDC / CreateCompatibleBitmap / CreateDIBSection — offscreen rendering
- [x] StretchBlt / StretchDIBits / SetDIBitsToDevice — image blitting
- [x] Pen/brush improvements — CreatePen, LineTo, MoveToEx, Polygon, Polyline
- [x] CreateFontIndirectA with real font matching
- [x] GetTextMetricsA / GetTextExtentPoint32A — text measurement
- [x] SaveDC / RestoreDC — GDI state stack
- [x] SetViewportOrgEx / SetWindowOrgEx — coordinate transforms
- [x] ClipRect / SelectClipRgn — clipping regions
- [x] GDI+ flat API stubs — GdipCreateFromHDC, GdipDrawImage, GdipCreateBitmapFromScan0
- [x] EnumFontFamiliesExA — font enumeration

### Phase 9: COM & OLE Foundation
_Chromium uses COM for accessibility, drag-drop, shell integration, audio._
- [x] CoInitializeEx / CoUninitialize
- [x] CoCreateInstance — object factory with CLSID dispatch
- [x] IUnknown vtable convention — QueryInterface/AddRef/Release
- [x] IMalloc / CoTaskMemAlloc / CoTaskMemFree
- [x] OLE clipboard — OleSetClipboard, OleGetClipboard
- [x] IDropTarget / IDropSource stubs (drag-and-drop)
- [x] Shell COM — SHGetFolderPathA, SHCreateDirectoryExA
- [x] ole32.dll + shell32.dll shim tables

### Phase 10: Unicode & Wide String APIs
_Chromium is a Unicode application. Most real calls use W (wide) variants._
- [x] W-suffix versions of all kernel32/user32/gdi32 APIs (CreateFileW, CreateWindowExW, etc.)
- [x] UTF-8 ↔ UTF-16 conversion layer (MultiByteToWideChar/WideCharToMultiByte already stubbed)
- [x] wcslen, wcscpy, wcscat, wcscmp, wprintf, swprintf in msvcrt shim
- [x] Internal string handling: store as UTF-8, convert at API boundary

### Phase 11: Security & Crypto APIs
_Chromium verifies TLS certificates and uses Windows crypto for key storage._
- [x] CryptAcquireContextA / CryptReleaseContext
- [x] CryptGenRandom — map to prng_random()
- [x] CertOpenStore / CertFindCertificateInStore / CertFreeCertificateContext
- [x] BCryptOpenAlgorithmProvider / BCryptGenRandom (modern crypto API)
- [x] advapi32.dll + crypt32.dll + bcrypt.dll shim tables
- [x] Embedded root CA certificates for TLS verification

### Phase 12: Structured Exception Handling
_Chromium uses SEH for crash handling (crashpad). C++ exceptions also need this._
- [x] _except_handler3 / _except_handler4 — SEH frame walker
- [x] RtlUnwind — stack unwinding
- [x] SetUnhandledExceptionFilter — top-level crash handler
- [x] __CxxFrameHandler3 — C++ exception support (try/catch via SEH)
- [x] Per-thread exception chain via FS:[0] (TIB)

### Phase 13: Miscellaneous Win32
_The long tail of APIs that Chromium calls on startup._
- [x] GetSystemInfo / GetNativeSystemInfo — CPU count, page size, arch
- [x] GetVersionExA / IsProcessorFeaturePresent — OS version queries
- [x] GetEnvironmentVariableA / SetEnvironmentVariableA
- [x] OutputDebugStringA — map to serial debug log
- [x] GetUserNameA — map to user_get_current()
- [x] FormatMessageA — error code to string
- [x] GetLocaleInfoA / GetUserDefaultLCID — locale stubs (return en-US)
- [x] GetTimeZoneInformation / GetLocalTime / GetSystemTime / FileTimeToSystemTime
- [x] kernel32 thread pool stubs — QueueUserWorkItem, CreateTimerQueue
- [x] advapi32 security stubs — GetTokenInformation, OpenProcessToken

## Tier 1.6 — From Demo Toy to Real Software

> **Goal:** After completing Tier 1.5 (Phases 1–13), ImposOS can run simple Win32 apps.
> Tier 1.6 takes it from "interesting demo" to "runs real-world Windows software."
> Each phase builds on the last. Estimated total: 14 phases.

### Phase 1: Robust C/C++ Runtime (msvcrt → full CRT)
_Almost every real app links against the C runtime. Your current msvcrt shim covers basics, but real software hits dozens more functions immediately._
- [x] `<stdio.h>` completeness — sprintf, snprintf, sscanf, fprintf, fseek, ftell, rewind, tmpfile, perror, setvbuf
- [x] `<stdlib.h>` completeness — atoi, atof, strtol, strtod, strtoul, qsort, bsearch, abs, labs, div, ldiv, system, getenv, putenv, _itoa, _atoi64
- [x] `<string.h>` completeness — strncpy, strncat, strncmp, strstr, strchr, strrchr, strtok, memset, memmove, memcmp, _stricmp, _strnicmp, _strdup
- [x] `<math.h>` — sin, cos, tan, sqrt, pow, log, log10, exp, floor, ceil, fabs, fmod, atan2, asin, acos (link to soft-float or FPU)
- [x] `<time.h>` — time, localtime, gmtime, mktime, strftime, difftime, clock, _ftime
- [x] `<ctype.h>` — isalpha, isdigit, isspace, toupper, tolower, isalnum, isprint, etc.
- [x] `<setjmp.h>` — setjmp / longjmp (critical for error recovery in C code)
- [x] `<signal.h>` — signal, raise (SIGSEGV, SIGABRT, SIGFPE handlers)
- [x] `<locale.h>` — setlocale, localeconv (most apps call setlocale on startup)
- [x] `<errno.h>` — per-thread errno via TLS, all standard error codes
- [x] `_beginthread` / `_endthread` (simplified thread wrappers beyond _beginthreadex)
- [x] `_open` / `_read` / `_write` / `_close` / `_lseek` — POSIX-style low-level I/O in msvcrt
- [x] `_stat` / `_fstat` / `_access` — file info queries
- [x] `_snprintf` / `_vsnprintf` — Windows-specific format variants
- [x] msvcrt global state — `_acmdln`, `_environ`, `__argc`, `__argv`, `_pgmptr` initialization
- [x] C++ operator new / delete — map to HeapAlloc/HeapFree with proper alignment
- [x] C++ RTTI stubs — `typeid`, `dynamic_cast` support structures
- [x] `atexit` / `_onexit` — shutdown callback chain

### Phase 2: Exception Handling Hardening
_Phase 12 of Tier 1.5 adds basic SEH. This phase makes it bulletproof — real apps crash without it._
- [x] Full SEH chain walking — nested __try/__except/__finally with correct unwinding order
- [x] C++ exception infrastructure — `__CxxFrameHandler3` with proper catchable type matching, `_CxxThrowException`
- [x] Exception info structures — `ThrowInfo`, `CatchableTypeArray`, `CatchableType` with type_info matching
- [x] Stack unwinding with destructors — unwind calls dtors for stack objects (C++ RAII depends on this)
- [x] Vectored Exception Handling — `AddVectoredExceptionHandler` / `RemoveVectoredExceptionHandler`
- [x] `__CppXcptFilter` — C++ exception filter used by CRT startup
- [x] `_set_se_translator` — SEH-to-C++ exception translation
- [x] Guard pages — `STATUS_GUARD_PAGE_VIOLATION` for stack growth detection
- [x] `RaiseException` with custom exception codes
- [x] Unhandled exception dialog — show crash info (address, registers, stack trace) in a WM dialog instead of silent death

### Phase 3: Full Unicode & Internationalization
_Phase 10 of Tier 1.5 adds W-suffix APIs. This phase makes Unicode actually work end-to-end._
- [x] Correct UTF-16 surrogate pair handling in all W APIs
- [x] Full `MultiByteToWideChar` / `WideCharToMultiByte` with all code pages (CP_ACP, CP_UTF8, 1252, etc.)
- [x] `CharUpperW` / `CharLowerW` / `CharNextW` / `IsCharAlphaW` — character classification
- [x] `CompareStringW` / `LStrCmpIW` — locale-aware string comparison
- [x] `GetACP` / `GetOEMCP` / `IsValidCodePage`
- [x] `wsprintfW` / `wvsprintfW`
- [x] NLS stubs — `GetLocaleInfoW`, `GetNumberFormatW`, `GetDateFormatW`, `GetTimeFormatW`
- [x] Internal kernel string handling — all paths stored as UTF-8, convert at syscall boundary
- [x] Font rendering with Unicode glyph support — render CJK, Cyrillic, Arabic (at least basic Latin + extended)
- [x] Console code page support — `SetConsoleCP`, `SetConsoleOutputCP`

### Phase 4: Window Controls & Common Controls
_Real GUI apps don't draw everything from scratch. They use built-in window controls._
- [ ] Built-in window classes — `BUTTON`, `EDIT`, `STATIC`, `LISTBOX`, `COMBOBOX`, `SCROLLBAR`
- [ ] `EDIT` control — single-line and multi-line text input with selection, copy/paste, undo
- [ ] `BUTTON` control — push buttons, checkboxes, radio buttons, group boxes
- [ ] `LISTBOX` — single/multi select, scrollable, owner-draw support
- [ ] `COMBOBOX` — dropdown list with edit field
- [ ] `STATIC` — labels, frames, images
- [ ] `SCROLLBAR` — horizontal/vertical with thumb tracking
- [ ] Common Controls (comctl32) — `InitCommonControlsEx`, version 6 manifest
- [ ] `ListView` — report/icon/list views with columns, sorting, virtual mode
- [ ] `TreeView` — hierarchical tree with expand/collapse, icons
- [ ] `TabControl` — tabbed panels
- [ ] `StatusBar` — bottom-of-window status with parts
- [ ] `Toolbar` / `Rebar` — button bars with icons
- [ ] `ProgressBar` — determinate and marquee modes
- [ ] `UpDown` (spinner) — increment/decrement buddy control
- [ ] `Tooltip` — hover text for controls
- [ ] `RichEdit` — RTF text editing (riched20.dll shim) — at least basic text with formatting
- [ ] Common Dialogs (comdlg32) — `GetOpenFileNameA/W`, `GetSaveFileNameA/W`, `ChooseColor`, `ChooseFont`, `PrintDlg`
- [ ] Dialog resources — `DialogBoxParam`, `CreateDialogParam`, `MAKEINTRESOURCE`, `IsDialogMessage`
- [ ] `WM_COMMAND` / `WM_NOTIFY` routing — control notification dispatch
- [ ] Window subclassing — `SetWindowLongPtr(GWLP_WNDPROC)` to override control behavior
- [ ] `WS_CHILD` / `WS_CLIPCHILDREN` / `WS_CLIPSIBLINGS` — correct child window clipping
- [ ] Owner-draw controls — `WM_DRAWITEM`, `WM_MEASUREITEM` for custom rendering
- [ ] Keyboard navigation — Tab order, `WM_GETDLGCODE`, `IsDialogMessage`, accelerator tables

### Phase 5: Resource Loading & PE Enhancements
_Real .exe files embed icons, dialogs, menus, strings, and version info as PE resources._
- [ ] PE resource section parser — walk `.rsrc` tree (type → name → language)
- [ ] `FindResource` / `LoadResource` / `LockResource` / `SizeofResource`
- [ ] `LoadStringA/W` — extract strings from string table resources
- [ ] `LoadIconA/W` / `LoadCursorA/W` / `LoadBitmapA/W` — extract image resources
- [ ] `LoadImageA/W` — unified loader for icons, cursors, bitmaps from file or resource
- [ ] `LoadMenuA/W` — parse `MENU` / `MENUEX` resource into menu handles
- [ ] Dialog template resources — `DIALOGEX` resource parsing for `DialogBoxParam`
- [ ] Accelerator table resources — `LoadAccelerators`, `TranslateAccelerator`
- [ ] Version info resources — `GetFileVersionInfoA`, `VerQueryValueA`
- [ ] Icon extraction for shell — display .exe icons in file manager
- [ ] Manifest parsing — XML side-by-side manifests for comctl32 v6, DPI awareness, UAC
- [ ] PE TLS directory — thread-local storage callbacks (called before entry point, many CRTs need this)
- [ ] PE bound imports — skip IAT patching for pre-bound DLLs (optimization)
- [ ] PE load config — security cookie (`__security_cookie`) and safe SEH table

### Phase 6: Comprehensive GDI & Printing Foundation
_Most Win32 apps expect full 2D rendering. Skia-based apps need even more._
- [ ] DIB (Device-Independent Bitmap) engine — 1/4/8/16/24/32-bit pixel format support
- [ ] Full ROP2/ROP3 raster operations — not just SRCCOPY, but SRCPAINT, SRCAND, SRCINVERT, PATCOPY, etc.
- [ ] Region operations — `CreateRectRgn`, `CombineRgn`, `PtInRegion`, `OffsetRgn`, complex clipping
- [ ] Path support — `BeginPath`, `EndPath`, `StrokePath`, `FillPath`, `StrokeAndFillPath`, `CloseFigure`
- [x] Arc/ellipse rendering — `Arc`, `Ellipse`, `Pie`, `Chord`, `RoundRect`
- [ ] Bitmap operations — `CreateBitmap`, `GetDIBits`, `SetDIBits`, `GetBitmapBits`
- [ ] Palette support — `CreatePalette`, `SelectPalette`, `RealizePalette` (8-bit color modes)
- [ ] Coordinate transforms — `SetWorldTransform`, `ModifyWorldTransform`, affine matrix support
- [ ] Advanced text — `GetCharWidth32`, `GetCharABCWidths`, `ExtTextOut` with clipping and opaque rect
- [ ] Metafile stubs — `CreateEnhMetaFile`, `PlayEnhMetaFile` (needed for clipboard EMF)
- [x] `GetDeviceCaps` completeness — HORZRES, VERTRES, BITSPIXEL, LOGPIXELSX/Y, TECHNOLOGY
- [ ] `BitBlt` with full ternary raster ops
- [ ] Brush patterns — `CreateHatchBrush`, `CreatePatternBrush`
- [ ] Stock objects completeness — all `GetStockObject` types (NULL_BRUSH, SYSTEM_FONT, etc.)
- [ ] GDI handle table — proper handle allocation, reference counting, leak detection

### Phase 7: Audio Subsystem
_No multimedia app works without sound. Even simple apps use MessageBeep or PlaySound._
- [ ] `winmm.dll` shim — `PlaySoundA/W`, `sndPlaySound`, `MessageBeep`
- [ ] Waveform audio — `waveOutOpen`, `waveOutWrite`, `waveOutClose`, `waveOutGetPosition`, `waveOutReset`
- [ ] Waveform input — `waveInOpen`, `waveInStart`, `waveInStop`, `waveInAddBuffer`
- [ ] MIDI stubs — `midiOutOpen`, `midiOutShortMsg`, `midiOutClose`
- [ ] Audio mixing — mix multiple waveOut streams into single PCM output
- [ ] Timer functions — `timeSetEvent`, `timeKillEvent`, `timeGetTime`, `timeBeginPeriod`
- [ ] WAV file parser — load PCM from .wav resources and files
- [ ] Audio driver backend — map PCM output to ImposOS sound driver (extend PC speaker or add AC97/HDA stub)
- [ ] `mmioOpen` / `mmioRead` / `mmioWrite` — multimedia file I/O
- [ ] DirectSound stubs (dsound.dll) — `DirectSoundCreate`, `IDirectSoundBuffer::Play/Stop/Lock/Unlock` (map to waveOut internally)

### Phase 8: Advanced Process & Job Management
_Real apps spawn helper processes, use pipes, and expect proper process lifecycle._
- [ ] Full environment block — `CreateProcessA/W` with `lpEnvironment`, proper inheritance
- [ ] Process environment — `GetEnvironmentStrings` / `FreeEnvironmentStrings`
- [ ] Handle inheritance — `SECURITY_ATTRIBUTES.bInheritHandle`, handle table cloning on CreateProcess
- [ ] Named pipes — `CreateNamedPipeA`, `ConnectNamedPipe`, `TransactNamedPipe` (Chromium IPC)
- [ ] Mailslots — `CreateMailslot`, `GetMailslotInfo`
- [ ] Job objects — `CreateJobObject`, `AssignProcessToJobObject`, `SetInformationJobObject` (process groups)
- [x] Process exit code — proper `ExitProcess` propagation, `STILL_ACTIVE` status
- [ ] Console allocation — `AllocConsole`, `FreeConsole`, `AttachConsole` for detached processes
- [ ] Console API — `ReadConsoleInput`, `WriteConsoleOutput`, `SetConsoleCursorPosition`, `SetConsoleTextAttribute`, `GetConsoleScreenBufferInfo`
- [ ] Proper command-line parsing — `CommandLineToArgvW`, quote and space handling
- [ ] Startup info — `STARTUPINFO` fields: dwFlags, wShowWindow, hStdInput/Output/Error

### Phase 9: Timer, Async & Message Queue Improvements
_GUI apps depend on timers, async operations, and a fully correct message pump._
- [ ] `SetTimer` / `KillTimer` — WM_TIMER delivery at specified intervals
- [ ] `MsgWaitForMultipleObjects` — wait for messages OR sync objects (critical for real message loops)
- [ ] `PostMessage` / `PostThreadMessage` — async message posting to queues
- [ ] `SendMessage` cross-thread — synchronous cross-thread message dispatch with marshaling
- [ ] `PeekMessage` with PM_REMOVE / PM_NOREMOVE — non-blocking message checks
- [ ] Message filtering — `GetMessage` / `PeekMessage` wMsgFilterMin/Max ranges
- [ ] `TranslateMessage` — proper WM_CHAR / WM_DEADCHAR generation from WM_KEYDOWN
- [ ] `WM_NCCALCSIZE` / `WM_NCPAINT` / `WM_NCHITTEST` — non-client area (title bar, borders, resize handles)
- [ ] Input queue serialization — proper message ordering between mouse, keyboard, timer, paint
- [ ] `WaitMessage` — block until new message arrives
- [ ] `RegisterWindowMessage` — app-defined message IDs
- [ ] `BroadcastSystemMessage` stubs
- [ ] Queued vs non-queued message distinction — SendMessage bypasses queue, PostMessage queues
- [ ] `WM_COPYDATA` — cross-process data passing via messages

### Phase 10: Shell & Desktop Integration
_Apps expect a Windows shell environment: file associations, special folders, notifications._
- [ ] `ShellExecuteA/W` — open files with associated apps, "open", "edit", "print" verbs
- [x] `SHGetSpecialFolderPathA/W` — Desktop, AppData, ProgramFiles, Temp, Fonts, etc.
- [x] `SHGetFolderPathA/W` — CSIDL_* to path resolution
- [ ] `SHFileOperation` — copy, move, delete, rename with progress (or at least stubs)
- [ ] File associations registry — `.txt` → notepad, `.html` → browser, etc.
- [ ] `DragQueryFile` / `DragFinish` — shell drag-drop support for files
- [ ] Tray notification — `Shell_NotifyIconA/W` (systray icons with tooltip and balloon)
- [ ] `SHBrowseForFolder` — folder picker dialog
- [ ] `ShellExecuteEx` — extended launch with process handle return
- [ ] `SHChangeNotify` — filesystem change notifications
- [ ] `.lnk` shortcut file stubs — `IShellLink` COM interface
- [ ] Application paths registry — `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths`
- [ ] `SHGetFileInfoA/W` — file icon and type name resolution

### Phase 11: DirectX & Multimedia Stubs
_You don't need a full GPU, but many apps probe for DirectX and fail if the DLLs are missing._
- [ ] `ddraw.dll` stub — `DirectDrawCreate` returns stub object, `IDirectDraw::SetCooperativeLevel` succeeds
- [ ] `d3d9.dll` stub — `Direct3DCreate9` returns stub, `GetAdapterDisplayMode` returns desktop resolution
- [ ] `d3d11.dll` stub — `D3D11CreateDevice` returns E_FAIL gracefully (forces software fallback)
- [ ] `dxgi.dll` stub — `CreateDXGIFactory` with `EnumAdapters` returning none
- [ ] `d3dcompiler_47.dll` stub — returns E_FAIL for shader compilation
- [ ] `DirectWrite` stub (dwrite.dll) — `DWriteCreateFactory`, `IDWriteFactory::CreateTextFormat`, text layout measurement
- [ ] `Direct2D` stub (d2d1.dll) — `D2D1CreateFactory`, basic render target stubs
- [ ] `OpenGL32.dll` stub — `wglCreateContext` / `wglMakeCurrent` with software fallback or failure
- [ ] `MFPlat.dll` / `MFReadWrite.dll` stubs — Media Foundation (Chromium uses this for video decode)
- [ ] `XAudio2` stub — `XAudio2Create` returns E_FAIL (games fall back to DirectSound)
- [ ] Version lie — `DirectDrawCreate` reports DirectX 9.0c, so apps don't refuse to start

### Phase 12: Networking & Internet APIs
_Beyond raw Winsock, most apps use higher-level HTTP/internet APIs._
- [ ] `wininet.dll` shim — `InternetOpenA/W`, `InternetOpenUrlA/W`, `InternetReadFile`, `InternetCloseHandle`
- [ ] `winhttp.dll` shim — `WinHttpOpen`, `WinHttpConnect`, `WinHttpOpenRequest`, `WinHttpSendRequest`, `WinHttpReceiveResponse`, `WinHttpReadData`
- [ ] HTTP request engine — build HTTP/1.1 requests over your existing TLS/socket stack
- [ ] `urlmon.dll` stubs — `URLDownloadToFileA` (many apps use this for simple downloads)
- [ ] Cookie jar — basic persistent cookie storage for WinInet/WinHTTP sessions
- [ ] Proxy settings — read from registry `HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings`
- [ ] `InternetGetConnectedState` — return TRUE
- [ ] SSL/TLS via your existing TLS stack — WinHTTP/WinInet calls map to ImposOS TLS engine
- [ ] `DnsQuery` / `DnsRecordListFree` (dnsapi.dll) — DNS resolution beyond getaddrinfo
- [ ] Certificate validation path — WinHTTP/WinInet verify certs against embedded root CAs (from Tier 1.5 Phase 11)

### Phase 13: Full Clipboard, Drag-Drop & Data Exchange
_Correct clipboard and OLE data transfer for real copy-paste and drag-drop._
- [ ] Multiple clipboard formats — `CF_TEXT`, `CF_UNICODETEXT`, `CF_BITMAP`, `CF_DIB`, `CF_HDROP` (file lists)
- [ ] Clipboard format conversion — auto-synthesize CF_TEXT ↔ CF_UNICODETEXT, CF_BITMAP ↔ CF_DIB
- [ ] `RegisterClipboardFormatA/W` — app-defined clipboard formats (used by Office, browsers, etc.)
- [ ] `EnumClipboardFormats` — iterate available formats
- [ ] `IsClipboardFormatAvailable` — check before pasting
- [ ] Delayed rendering — `SetClipboardData(format, NULL)`, render on `WM_RENDERFORMAT`
- [ ] `WM_DESTROYCLIPBOARD` / `WM_CLIPBOARDUPDATE` notifications
- [ ] OLE drag-drop — full `IDropTarget`/`IDropSource`/`IDataObject` with FORMATETC/STGMEDIUM
- [ ] `RegisterDragDrop` / `RevokeDragDrop` — per-window drop target registration
- [ ] `DoDragDrop` — initiate drag with visual feedback
- [ ] `TYMED_HGLOBAL` / `TYMED_ISTREAM` data transfer mediums
- [ ] `CF_HDROP` to `IDataObject` bridge — drag files from shell to apps

### Phase 14: Stability, Compatibility & Performance
_The difference between "technically works" and "actually usable."_
- [ ] API call logging — debug mode that logs every Win32 call with params and return values
- [ ] Unimplemented API stub generator — auto-generate stubs that log name + return SUCCESS/zero/NULL instead of crashing
- [ ] Handle leak detector — track GDI, kernel, user handles; warn on leak
- [ ] Per-app compatibility database — override behaviors for specific .exe names (version lie, API quirks)
- [x] Crash dialog with stack trace — on unhandled exception, show faulting address, loaded DLLs, call stack
- [ ] Page fault handler improvements — better error messages for NULL deref, stack overflow, access violations
- [ ] Memory usage tracking — per-process memory accounting, warn on exhaustion
- [ ] Process isolation hardening — ensure one app crash doesn't take down the system
- [ ] API conformance test suite — automated tests for every shimmed function against expected Win32 behavior
- [ ] Performance profiling — identify hot paths in API shims (GDI, message loop, heap)
- [ ] Lazy DLL loading — don't load shim tables until first call to reduce startup overhead
- [ ] Known-app test matrix — maintain a list of tested .exe files with pass/fail status

### Target Software by Phase Completion

| After Phase | You Can Realistically Run |
|---|---|
| 1–2 | Most C/C++ console apps, simple games compiled from source |
| 3–4 | Real Win32 GUI apps: Notepad clones, calculators, simple editors, dialogs-heavy apps |
| 5–6 | Apps with embedded resources: icons, menus, dialogs. Proper look-and-feel. Paint-like apps |
| 7 | Multimedia apps: simple audio players, apps with sound effects, Winamp (basic) |
| 8–9 | Multi-process apps, apps with complex message loops, MDI apps, console tools like PuTTY |
| 10 | Apps that integrate with the "shell": file managers, apps that open other apps, tray apps |
| 11 | Apps that probe for DirectX won't crash on startup. Software-rendered games |
| 12 | Apps that download files, update checkers, basic web browsers (not Chromium-class) |
| 13 | Full copy-paste between apps, drag-drop workflows, clipboard-heavy apps like editors |
| 14 | Everything above but stable, debuggable, and maintainable |

### Realistic "Real Software" Targets After Full Tier 1.6
- **PuTTY** — terminal emulator (Winsock + GDI + console)
- **7-Zip** (command line) — file archiver (CRT + file I/O + SEH)
- **Classic Winamp 2.x** — audio player (GDI + winmm + resource loading)
- **mIRC** — IRC client (Winsock + GDI + controls + DDE)
- **HxD** — hex editor (GDI + controls + file I/O)
- **AkelPad** — lightweight text editor (full Win32 controls)
- **FreeCell / Solitaire (XP era)** — classic Windows games (GDI + resources)
- **TCC/LE** — enhanced command processor (console API + CRT)
- **Dependency Walker** — useful meta-tool to test your own PE loader
- **Small Delphi/VB6 apps** — many shareware apps from 2000s era

### What's Still NOT Enough for Chromium
Even after all 14 phases, Chromium requires additional work beyond this tier:
1. **V8 JIT** — needs W^X page flipping (VirtualProtect between RW and RX on the same pages rapidly)
2. **Mojo IPC** — shared memory + named pipes + complex handle passing between 5+ processes simultaneously
3. **Skia rendering** — needs either real OpenGL or a complete GDI+ / Direct2D with pixel-accurate anti-aliased rendering
4. **Sandbox** — Chromium's renderer runs in a restricted process with limited API access (job objects + restricted tokens)
5. **200+ DLLs** — the sheer long tail of APIs is enormous; Chromium calls thousands of unique Win32 functions
6. **Memory pressure** — Chromium uses 500MB+ with multiple tabs; your memory manager needs to handle this without falling over
7. **Correct multi-process scheduling** — browser, renderer, GPU, and utility processes need fair CPU time

Chromium is realistically a Tier 2.0+ goal that would require dedicated phases beyond this roadmap.

---

## Strategy: Technology Leapfrogging

> **Don't build what you can skip. Don't port what you can run unchanged.**
>
> Traditional OS development: build POSIX → port libc → cross-compile each tool → run ported binaries.
> That's the path SerenityOS, Sortix, and Managarm took. It works, but it's slow.
>
> **Our leapfrog:** Implement the Linux i386 syscall ABI directly. Then pre-built static
> Linux binaries run on ImposOS WITHOUT modification, WITHOUT porting, WITHOUT cross-compilation.
> Download a binary, copy it to the disk image, run it. This is what WSL1 did — Microsoft
> didn't port Linux software, they made the NT kernel speak Linux's language.

### The Leapfrog Map

| Traditional Work | Time | Leapfrog | Time | Savings |
|---|---|---|---|---|
| Design custom POSIX syscall numbers | ~1 wk | Use Linux i386 syscall numbers | 0 days | 1 week |
| Port musl libc to ImposOS | ~2 wk | Run pre-built static musl binaries unchanged | 0 days | 2 weeks |
| Implement fork() with COW paging | ~1 wk | vfork()+exec() (skip COW entirely) | ~1 day | 6 days |
| Build a dynamic linker (ld.so) | ~2 wk | Static binaries only | 0 days | 2 weeks |
| Cross-compile each tool individually | ~3 wk | Download pre-built static i386 binaries | ~1 hr | 3 weeks |
| Port BusyBox (configure, build, debug) | ~1 wk | Download 1MB static BusyBox binary | 0 days | 1 week |
| Write custom TrueType font engine | ~2 wk | Static freetype2 (or X11 fonts later) | ~1 day | 13 days |
| Write custom HTTP client library | ~1 wk | Download static curl binary | 0 days | 1 week |
| Write custom HTML renderer | ~3 wk | NetSurf framebuffer (raw memory surface) | ~3 days | 18 days |
| Write custom display protocol | ~4 wk | Port TinyX/Xfbdev (~950KB X11 server) | ~1 wk | 3 weeks |
| Write custom widget toolkit | ~4 wk | X11 + dwm + existing X11 apps | ~3 days | 25 days |
| Write custom OpenGL renderer | ~3 wk | Port TinyGL (~4000 LOC) or skip entirely | ~2 days | 19 days |
| Port 300 tools one-by-one | ~6 wk | BusyBox = 300+ tools in 1 binary | 0 days | 6 weeks |

**Total traditional effort: ~35 weeks. Leapfrog effort: ~3 weeks. Savings: ~32 weeks.**

### Why This Works for ImposOS

ImposOS already uses `INT 0x80` with `EAX=syscall#`, `EBX/ECX/EDX=args` — that's the
**exact same calling convention as Linux i386**. The only difference is the syscall numbers.
Static musl binaries expect Linux syscall numbers in EAX. We just remap our INT 0x80
handler to dispatch Linux numbers.

### The Two-Track Parallel Strategy

**Track A: "Native"** — compile directly with i686-elf-gcc, link into kernel or run natively.
Gives the fastest, most impressive results (Doom on Day 1-2).

**Track B: "Linux Compat"** — implement ~45 Linux syscalls so pre-built static binaries
run unchanged. Gives the massive ecosystem (BusyBox, bash, curl, git, python, GCC).

Both tracks run in parallel. Track A for quick visual wins, Track B for the ecosystem.

### The 30 Syscalls That Change Everything

| # | Syscall | Linux # | What it unlocks |
|---|---------|---------|-----------------|
| 1 | `write` | 4 | Any program can print output |
| 2 | `exit_group` | 252 | Programs can exit cleanly |
| 3 | `brk` | 45 | malloc works (musl heap) |
| 4 | `mmap2` | 192 | Large allocations, stack setup |
| 5 | `set_thread_area` | 243 | TLS → musl initializes → ANY musl binary |
| 6 | `writev` | 146 | printf works (musl stdout uses writev) |
| — | — | — | **Static hello world runs** |
| 7 | `read` | 3 | Programs can read input |
| 8 | `open` | 5 | Programs can open files |
| 9 | `close` | 6 | File cleanup |
| 10 | `fstat64` | 197 | stat() on open files |
| 11 | `stat64` | 195 | ls works |
| 12 | `getdents64` | 220 | ls can list directories |
| 13 | `ioctl` | 54 | Terminal control → interactive apps |
| 14 | `getcwd` | 183 | pwd, shell prompt |
| 15 | `uname` | 122 | System identification |
| 16 | `getuid32` | 199 | User identity |
| — | — | — | **BusyBox runs (300+ tools)** |
| 17 | `clone` (vfork) | 120 | Process creation (no COW needed) |
| 18 | `execve` | 11 | Shell can launch programs |
| 19 | `wait4` | 114 | Shell waits for child processes |
| 20 | `pipe` | 42 | Shell pipelines |
| 21 | `dup2` | 63 | I/O redirection |
| 22 | `rt_sigaction` | 174 | Ctrl+C in bash |
| 23 | `rt_sigprocmask` | 175 | Signal masking |
| 24 | `kill` | 37 | Signals between processes |
| 25 | `nanosleep` | 162 | sleep command, timing |
| — | — | — | **bash + interactive shell works** |
| 26 | `socketcall` | 102 | All socket ops → networking |
| 27 | `select` | 142 | Multiplexed I/O → servers, clients |
| 28 | `chdir` | 12 | cd command |
| 29 | `access` | 33 | Permission checks |
| 30 | `mprotect` | 125 | Memory protection |
| — | — | — | **curl, wget, git, python work** |

### Things We NEVER Build (Permanent Skips)

| Never Build | Use Instead |
|---|---|
| Custom TrueType engine | Ported freetype2 (static binary or via X11) |
| Custom HTTP library | Static curl binary |
| Custom HTML/CSS renderer | NetSurf (framebuffer mode) or port Links |
| Custom OpenGL renderer | TinyGL (4000 LOC) or skip entirely |
| Custom display protocol | X11 via TinyX/Xfbdev (~950KB) |
| Custom widget toolkit for ports | GTK2/FLTK via X11, or just use terminal apps |
| Custom C library for ports | Pre-built musl (already inside static binaries) |
| Custom package manager | BusyBox wget + tar, or port opkg |
| Custom assembler/linker | Static GNU as/ld binaries |
| Individual tool porting | BusyBox (300+ tools, 1MB, one binary) |
| Dynamic linker (initially) | Static binaries only — skip PLT/GOT/ld.so |
| COW fork (initially) | vfork+exec pattern covers 90% of usage |

### What About Tier 1.6 Phases 4–14?

Tier 1.6 phases 4–14 complete the Win32 compatibility layer. They're useful for running
Windows `.exe` files, but they're **not on the critical path**. The leapfrog cascade
doesn't depend on them. They can be done in parallel or deferred.

---

## Tier 1.7 — Linux Compat & Software Leapfrog

> **Goal:** Instead of porting software TO ImposOS, make ImposOS speak Linux so software
> runs UNCHANGED. Implement ~45 Linux i386 syscalls + a static ELF loader. Then download
> pre-built binaries and run them. Total new kernel code: ~3000-5000 lines.
> Estimated total: 10 phases.

### Phase 1: Doom Day (Track A — Native) 
_The single most impressive milestone. No Linux compat needed. doomgeneric requires exactly 5 callback functions that map 1:1 to existing ImposOS APIs._

- [x] Download doomgeneric source (github.com/ozkl/doomgeneric) — ~70 `.c` files imported into `kernel/arch/i386/app/doom/`
- [x] Add to kernel build as an app (like shell.c, vi.c) — objects listed in `make.config`
- [x] Implement `DG_Init()` — integer scale (5x at 1920x1080), centered with letterbox
- [x] Implement `DG_DrawFrame()` — nearest-neighbor scale 320x200 → framebuffer via `gfx_backbuffer()` + `gfx_flip_rect()`
- [x] Implement `DG_SleepMs(ms)` — maps to `pit_sleep_ms()`
- [x] Implement `DG_GetTicksMs()` — `pit_get_ticks() * 1000 / 120`
- [x] Implement `DG_GetKey()` — raw PS/2 scancodes via `keyboard_get_raw_scancode()`, E0 prefix handling, full scancode-to-doom key mapping
- [x] Bundle shareware DOOM1.WAD as GRUB multiboot module (~4MB, exceeds FS limit) — loaded into memory at boot
- [x] Add `doom` shell command — `setjmp`/`longjmp` exit mechanism, `exit_set_restart_point` to catch `exit()` calls
- [x] libc additions: `ctype.h`, `errno.h`, `strcasecmp()`, `keyboard_get_raw_scancode()`
- [x] WAD file reader: memory-mapped `w_file_impos.c` replacing `w_file_stdc.c`
- [x] Stretch: add mouse look support via `mouse_get_delta()` — raw PS/2 delta accumulator + ev_mouse posting

### Phase 2: Filesystem Expansion & Initrd
_Current FS: 64 inodes, 128KB total — way too small. Need 32MB+ for Linux binaries._

- [ ] GRUB multiboot module support — parse multiboot info struct for module address + size
- [ ] Initrd loader — load tar/cpio archive from GRUB module into memory at boot
- [ ] Tar parser — read tar headers (512-byte blocks), extract file names, sizes, data pointers (~100 lines)
- [ ] Overlay mount — initrd files accessible alongside existing FS (read-only overlay)
- [ ] `/bin`, `/usr/bin` directories — standard Unix layout for Linux binaries
- [ ] `/dev/null`, `/dev/zero`, `/dev/tty`, `/dev/urandom` — minimal device nodes (special-cased in open/read/write)
- [ ] Expand existing FS — increase to 256+ inodes, 4KB blocks, 8192+ blocks (~32MB) for user-writable storage
- [ ] Update `make clean-disk` and disk format version

### Phase 3: Static ELF Loader + First Linux Syscalls
_Prove Linux binary compat works. Run a static musl hello world._

**ELF Loader (static only — NO dynamic linker):**
- [ ] ELF32 header validation — check magic, class (32-bit), data (little-endian), machine (EM_386)
- [ ] Program header parsing — iterate PT_LOAD segments
- [ ] Segment loading — allocate pages via PMM, map into per-process PD with correct RX/RW permissions
- [ ] BSS zeroing — zero-fill memory beyond file-backed segment data
- [ ] Entry point — jump to `e_entry` in Ring 3 with fresh user stack
- [ ] Mixed PE/ELF detection — check magic bytes (0x7F ELF vs MZ) to choose loader

**Linux i386 Syscall ABI (first 6 — enough for hello world):**
- [ ] Remap INT 0x80 handler — add Linux syscall dispatch alongside existing ImposOS syscalls
- [ ] `write` (#4) — route fd 1/2 to tty_write, other fds to pipe/file write
- [ ] `writev` (#146) — loop calling write for each iovec entry
- [ ] `exit_group` (#252) — terminate process via task_exit
- [ ] `brk` (#45) — per-process heap pointer, extend by mapping new pages
- [ ] `mmap2` (#192) — anonymous only: allocate pages, map into PD, return address
- [ ] `set_thread_area` (#243) — write `user_desc` struct to GDT entry, set %gs segment (musl TLS)

**Test:** Download static musl i386 hello world → boot ImposOS → it prints "Hello, world!"

### Phase 4: BusyBox Day (300+ Tools)
_Add ~10 more syscalls. Download static BusyBox (1MB). Run it. Instant Unix userland._

**Per-process kernel file descriptor table:**
- [ ] Expand fd_entry_t — support pipes, files, sockets, and device types
- [ ] Per-task FD table — 64 entries (up from 16), inherited on exec, closed on exit
- [ ] FD allocation — lowest-available-number semantics (POSIX requirement)
- [ ] Stdin/stdout/stderr — auto-open fd 0/1/2 for each new process

**Linux syscalls (file I/O — makes ls, cat, grep, find work):**
- [ ] `open` (#5) — map to fs_open_file, allocate kernel fd, return fd number
- [ ] `close` (#6) — release fd entry, decrement refcount
- [ ] `read` (#3) — dispatch by fd type: file → fs_read_file, pipe → pipe_read, device → handler
- [ ] `stat64` (#195) — map fs_stat → populate Linux `struct stat64` (inode, mode, size, times, uid, gid)
- [ ] `fstat64` (#197) — same via fd lookup
- [ ] `lstat64` (#196) — stat without following symlinks
- [ ] `getdents64` (#220) — fs_readdir → Linux `struct linux_dirent64` format
- [ ] `lseek` (#19) — per-fd file offset tracking (SEEK_SET, SEEK_CUR, SEEK_END)
- [ ] `ioctl` (#54) — terminal: `TIOCGWINSZ` (window size), `TCGETS`/`TCSETS` (termios)
- [ ] `fcntl64` (#221) — `F_GETFD`, `F_SETFD`, `F_GETFL`, `F_SETFL` (O_NONBLOCK)

**Misc syscalls (identity, environment):**
- [ ] `getcwd` (#183) — return current working directory path
- [ ] `uname` (#122) — return sysname="ImposOS", release, version, machine="i686"
- [ ] `getuid32` (#199) / `geteuid32` (#201) — map to user_get_current_uid()
- [ ] `getgid32` (#200) / `getegid32` (#202) — map to user_get_current_gid()
- [ ] `munmap` (#91) — free mapped pages
- [ ] `access` (#33) — check file permissions via fs_stat + mode check

**Test:** `busybox sh` launches shell. `busybox ls /bin` lists files. `busybox cat`, `busybox grep`,
`busybox vi`, `busybox wget` — 300+ tools work from a single 1MB binary.

### Phase 5: Process Management & Shell Pipelines
_Run commands from bash. Pipes. I/O redirection. Job control basics._

**Process creation (LEAPFROG: skip fork, use vfork):**
- [ ] `clone` (#120) — implement with `CLONE_VFORK | CLONE_VM` semantics (child shares parent memory until exec)
- [ ] `execve` (#11) — load new ELF into current process: reset PD, load segments, reset stack, jump to entry
- [ ] `wait4` (#114) — block parent until child exits, return status and rusage
- [ ] `exit_group` (#252) — terminate all threads in process (already stubbed in Phase 3)
- [ ] `getpid` (#20) — return current task PID (already exists internally)
- [ ] `getppid` (#64) — add parent_pid to task_info_t, return it
- [ ] Parent-child tracking — store parent_pid on clone, set child status to ZOMBIE on exit until reaped

**I/O plumbing (makes shell pipelines work):**
- [ ] `pipe` (#42) — create pipe pair, assign to two new fds in caller's fd table
- [ ] `dup2` (#63) — duplicate fd: copy fd_entry from src to dst slot
- [ ] `dup` (#41) — duplicate fd to lowest available slot
- [ ] FD inheritance on exec — preserve open fds across execve (except FD_CLOEXEC)
- [ ] `chdir` (#12) — change per-process working directory
- [ ] `fchdir` (#133) — chdir via fd

**Signals (makes Ctrl+C, job control work):**
- [ ] `rt_sigaction` (#174) — map to existing signal handler infrastructure + add sa_mask, sa_flags
- [ ] `rt_sigprocmask` (#175) — add per-task signal mask bitmask, block/unblock signals
- [ ] `rt_sigreturn` (#173) — map to existing sigreturn
- [ ] `kill` (#37) — map to existing sig_send_pid
- [ ] `nanosleep` (#162) — map to pit_sleep_ms with nanosecond struct conversion
- [ ] `getpgid` (#132) / `setpgid` (#57) — process groups (add pgid to task_info_t)

**Test:** `bash` runs interactively. `ls | grep foo` works. `cat file > output` works.
`Ctrl+C` interrupts running commands. Background jobs with `&` work.

### Phase 6: Networking (Socket Syscalls)
_Existing TCP/IP stack works. Just wrap it in Linux socket syscall ABI._

**Socket multiplexer (Linux i386 uses `socketcall` #102):**
- [ ] `socketcall` (#102) dispatcher — SYS_SOCKET, SYS_BIND, SYS_CONNECT, SYS_LISTEN, SYS_ACCEPT, SYS_SEND, SYS_RECV, SYS_SENDTO, SYS_RECVFROM, SYS_SHUTDOWN, SYS_SETSOCKOPT, SYS_GETSOCKOPT, SYS_GETPEERNAME, SYS_GETSOCKNAME
- [ ] `socket()` → map to ImposOS socket_create (AF_INET + SOCK_STREAM/SOCK_DGRAM)
- [ ] `connect()` → map to tcp_connect / set UDP peer
- [ ] `bind()` / `listen()` / `accept()` → map to tcp_listen, tcp_accept
- [ ] `send()` / `recv()` → map to tcp_send / tcp_recv
- [ ] `sendto()` / `recvfrom()` → map to udp_send / udp_recv
- [ ] `setsockopt()` / `getsockopt()` — SO_REUSEADDR, SO_RCVBUF, TCP_NODELAY
- [ ] `select` (#142) — poll across file descriptors: sockets + pipes + files
- [ ] `poll` (#168) — same as select but with pollfd interface
- [ ] Socket fds — sockets live in the per-task fd table alongside files and pipes
- [ ] `getpeername` / `getsockname` — query socket addresses

**DNS (needed for hostname resolution):**
- [ ] `gethostbyname` path — route through ImposOS dns_resolve internally
- [ ] `/etc/resolv.conf` stub — return QEMU's DNS forwarder (10.0.2.3)

**Test:** Static `curl http://example.com` downloads a page. `busybox wget` fetches files.
Static `links` browses the web in text mode.

### Phase 7: Terminal, PTY & Interactive Applications
_Make vim, htop, tmux, python3 REPL work properly._

**Pseudo-terminals:**
- [ ] PTY pair — master/slave fd pair (master = controlling process, slave = terminal for child)
- [ ] `openpty()` / `posix_openpt()` / `grantpt()` / `unlockpt()` support via ioctl
- [ ] `/dev/ptmx` — PTY multiplexer device node
- [ ] `/dev/pts/N` — individual PTY slave devices

**Terminal discipline:**
- [ ] Termios struct — full `struct termios` with c_iflag, c_oflag, c_cflag, c_lflag, c_cc
- [ ] Raw mode — `cfmakeraw()` equivalent: disable echo, line buffering, signal generation
- [ ] Cooked mode — line editing, echo, ^C/^Z signal generation
- [ ] `TCGETS` / `TCSETS` / `TCSETSW` / `TCSETSF` ioctls
- [ ] `TIOCGWINSZ` / `TIOCSWINSZ` — window size query/set (report 1920/8 × 1080/16 in chars)
- [ ] `SIGWINCH` — send to foreground process on terminal resize

**Additional syscalls for terminal apps:**
- [ ] `readlink` (#85) — resolve symlinks (vim checks `/proc/self/exe`)
- [ ] `/proc/self/exe` — return path to current executable
- [ ] `clock_gettime` (#265) — `CLOCK_REALTIME` and `CLOCK_MONOTONIC`
- [ ] `gettimeofday` (#78) — time with microsecond precision
- [ ] `mkdir` (#39) / `rmdir` (#40) / `unlink` (#10) / `rename` (#38) — filesystem mutations
- [ ] `link` (#9) / `symlink` (#83) — hard/soft links
- [ ] `chmod` (#15) / `fchmod` (#94) — permission changes
- [ ] `chown` (#182) / `fchown` (#207) — ownership changes
- [ ] `umask` (#60) — file creation mask
- [ ] `getrlimit` (#76) — resource limits (return generous defaults)

**Test:** `vim` opens files, syntax highlighting works. `htop` shows processes.
`tmux` splits terminal. `python3` REPL with line editing. `nano` edits files.

### Phase 8: NetSurf Browser (Graphical Web Browsing)
_A real web browser on ImposOS. NetSurf has a "ram" framebuffer surface that renders to raw memory._

**NetSurf ImposOS backend (~200 lines):**
- [ ] Cross-compile NetSurf with `TARGET=framebuffer` and `NETSURF_FB_FRONTEND=ram`
- [ ] ImposOS surface driver — point NetSurf's framebuffer at a WM window's pixel buffer
- [ ] Input routing — translate WM mouse/keyboard events to NetSurf's input format
- [ ] Window integration — NetSurf runs inside a WM window, draggable, resizable, closeable

**Dependencies (cross-compile as static libs):**
- [ ] libcurl (for HTTP/HTTPS) — links against ImposOS socket syscalls
- [ ] libpng (for images) — ~60KB, no OS deps
- [ ] libjpeg (for JPEG images) — optional but nice
- [ ] freetype2 (for fonts) — ~400KB, renders TrueType/OpenType
- [ ] NetSurf internal libs — libcss, hubbub, libdom, etc. (bundled in NetSurf source tree)
- [ ] Bundle .ttf fonts — Liberation Sans/Serif/Mono in initrd

**Alternative fast path:**
- [ ] Static `links -g` — graphical Links browser, simpler deps, still a real browser
- [ ] Static `w3m` — text-mode browser works right now with just terminal support

**Test:** Browse real websites. Render HTML, CSS, images. Click links. Fill forms.
View documentation, read news, use web-based tools — all on ImposOS.

### Phase 9: X11 — The Mega Leapfrog
_One port unlocks EVERY X11 application from the last 40 years._

**Port TinyX/Xfbdev (~950KB X11 server):**
- [ ] Cross-compile TinyX (github.com/idunham/tinyxserver) targeting ImposOS
- [ ] Xfbdev backend — point at ImposOS framebuffer memory, route PS/2 input
- [ ] X11 socket — Unix domain sockets (AF_UNIX) for client↔server communication
- [ ] `AF_UNIX` socket support — add to socketcall dispatcher (local IPC, no networking needed)
- [ ] SHM extension — `shmget`/`shmat`/`shmdt` for shared memory image transfer (map to existing SHM)

**Port dwm (~30KB tiling window manager):**
- [ ] Cross-compile dwm (suckless.org) against libX11
- [ ] Port libX11 (~1.5MB) — X11 client library, talks to TinyX via AF_UNIX socket
- [ ] Configuration — keybindings, colors, fonts

**Port st (~50KB terminal emulator):**
- [ ] Cross-compile st (suckless.org) against libX11
- [ ] Runs inside X11, provides terminal for all CLI apps
- [ ] All Track B applications (bash, vim, htop, python) now run inside X11 windows

**Result:** Full X11 desktop. Tile windows with dwm. Run xterm. Every X11 app from
the last 40 years is now available — just cross-compile and run.

### Phase 10: Network Hardening & Package Management
_Polish the foundation. Make it reliable and user-friendly._

**Network hardening (ported apps will stress-test the stack):**
- [ ] TCP retransmission & congestion control — Reno or Cubic
- [ ] TCP window scaling — high-bandwidth support
- [ ] DNS caching — local resolver cache with TTL expiry
- [ ] Multiple simultaneous connections — proper multiplexing for 50+ sockets
- [ ] Loopback interface — `127.0.0.1` without hitting the wire
- [ ] Connection tracking — proper FIN/RST handling, TIME_WAIT state

**Package management (use what BusyBox gives us):**
- [ ] `pkg` command — thin wrapper around BusyBox `wget` + `tar`
- [ ] Remote package index — HTTPS JSON catalog of pre-built static binaries
- [ ] Dependency tracking — simple metadata file per package
- [ ] Install/remove/update — download, extract to `/usr/bin`, register in manifest
- [ ] Pre-built packages — host static i386 binaries for all tested software
- [ ] `pkg search`, `pkg install`, `pkg remove`, `pkg update` — simple CLI interface

### Target Software by Phase Completion

| After Phase | What Runs on ImposOS |
|---|---|
| 1 | **DOOM** (native, compiled with i686-elf-gcc) |
| 2 | Filesystem holds 32MB+, initrd loads at boot |
| 3 | Static musl hello world — first Linux binary runs unchanged |
| 4 | **BusyBox** — 300+ Unix tools (ls, cat, grep, sed, awk, vi, wget, sh...) |
| 5 | **bash** + process management — shell pipelines, I/O redirection, job control |
| 6 | **curl, wget, links** — networking apps, text-mode web browsing |
| 7 | **vim, nano, htop, tmux, python3** — full interactive terminal environment |
| 8 | **NetSurf** — graphical web browser rendering real websites |
| 9 | **X11 desktop** — dwm + st + every X11 app from the last 40 years |
| 10 | Reliable networking + easy package installation |

### What This Unlocks

#### Pre-built static Linux binaries (download and run, no porting):
- **BusyBox** — 300+ tools in 1MB
- **bash, vim, nano, tmux, htop** — full terminal environment
- **curl, wget, links, w3m** — HTTP clients and text browsers
- **GCC, make, binutils, git** — compile software ON ImposOS
- **Python 3, Lua, Perl** — scripting languages
- **sqlite3** — embeddable database
- **openssh, dropbear** — SSH client/server
- **ffmpeg, mpg123** — media tools
- **redis, civetweb** — servers

#### Cross-compiled for ImposOS (Track A):
- **Doom** — native, via doomgeneric (5 callbacks)
- **NetSurf** — graphical browser via framebuffer surface
- **TinyGL** — software OpenGL (4000 LOC) for 3D demos

#### Via X11 (Phase 9):
- **dwm** — tiling window manager
- **st, xterm** — terminal emulators
- **surf** — suckless web browser
- **feh, sxiv** — image viewers
- **xfe** — file manager
- **Any X11 application ever written**

#### Win32 apps (via Tier 1.6):
- PuTTY, 7-Zip, Notepad++, XP-era games, Delphi/VB6 shareware

### After Tier 1.7: What's Next?

At this point ImposOS runs Doom, has 300+ Unix tools, a real web browser, and an X11
desktop with 40 years of application history available. Future tiers continue below.

## Tier 1.8 — GPU, Graphics & Display Infrastructure

> **Goal:** Move beyond software rendering. Build a real graphics stack from framebuffer
> basics to accelerated 2D/3D, multiple displays, and a modern compositing window manager.
> Estimated total: 10 phases.

### Phase 1: VESA VBE & Framebuffer Foundation
_Your current VGA driver is limited. VESA gives you high resolution and real color depth._
- [ ] VESA VBE 2.0 detection — call INT 0x10 / AX=0x4F00 in real mode (or via V86 monitor) to get VBE info block
- [ ] Mode enumeration — list all available modes with resolution, color depth, framebuffer address
- [ ] Mode setting — switch to linear framebuffer modes (1024x768x32, 1280x1024x32, 1920x1080x32)
- [ ] Linear framebuffer mapping — map physical framebuffer address into kernel virtual address space
- [ ] Backbuffer allocation — system RAM double buffer, flip/copy to VESA framebuffer on vsync
- [ ] Pixel format abstraction — handle RGB888, XRGB8888, BGR888 layouts (VESA modes vary)
- [ ] Framebuffer device API — `fb_open()`, `fb_get_info()`, `fb_map()`, `fb_flip()`, `fb_set_mode()`
- [ ] Mode switching at runtime — change resolution from Settings app without reboot
- [ ] EDID reading — parse monitor EDID via VBE DDC to detect native resolution
- [ ] Framebuffer console — kernel panic and early boot messages render to framebuffer (not VGA text mode)
- [ ] VGA text mode fallback — if VESA fails, gracefully fall back to 80x25 text
- [ ] Splash screen — boot logo displayed on framebuffer before desktop loads

### Phase 2: Compositing Window Manager
_Replace your current stacking WM with a compositing WM. Every window renders to its own buffer._
- [ ] Per-window framebuffers — each window renders to an offscreen bitmap, not directly to screen
- [ ] Composition pass — blend all visible window buffers onto the screen framebuffer in Z-order
- [ ] Alpha channel per window — windows can have transparency (drop shadows, rounded corners)
- [ ] Window drop shadows — render soft shadow behind each window during composition
- [ ] Damage tracking — windows report dirty rectangles, compositor only re-blends affected regions
- [ ] VSync synchronization — present composed frame on vertical blank to prevent tearing
- [ ] Smooth window dragging — compositor re-blends at 60fps during drag, no "trail" artifacts
- [ ] Window thumbnails — compositor can scale down any window buffer (for Alt-Tab, task switcher)
- [ ] Live window previews — taskbar hover shows real-time miniature of window content
- [ ] Blur behind — gaussian blur of background visible through semi-transparent windows (optional, expensive)
- [ ] Desktop wallpaper layer — wallpaper is the bottom layer of the composition stack
- [ ] Cursor layer — hardware cursor or compositor-rendered cursor on top of everything
- [ ] Composition bypass — fullscreen apps can write directly to framebuffer for performance
- [ ] Multi-monitor awareness — composition across two framebuffers (future, but design for it now)

### Phase 3: 2D Acceleration Abstraction
_Software blitting is slow. Abstract 2D operations so they can be accelerated later._
- [ ] 2D command buffer — apps submit draw commands (blit, fill, line, text) to a command queue
- [ ] Batch submission — group multiple draw commands into a single batch for efficiency
- [ ] Hardware blit interface — abstract `blit_rect(src, dst, w, h)` that backends can accelerate
- [ ] Hardware fill interface — abstract `fill_rect(x, y, w, h, color)` with hardware path
- [ ] Virtio-GPU 2D — implement virtio-gpu `RESOURCE_CREATE_2D`, `TRANSFER_TO_HOST`, `SET_SCANOUT` for QEMU/KVM
- [ ] BGA (Bochs Graphics Adapter) driver — simple register-based mode setting and LFB for Bochs/QEMU
- [ ] Software fallback — all 2D operations have a software path when no hardware is available
- [ ] Blit with ROP — accelerated raster operations for common patterns (SRCCOPY, SRCPAINT, PATCOPY)
- [ ] Stretch blit — accelerated image scaling (nearest-neighbor in hardware, bilinear in software)
- [ ] Cursor sprite — hardware cursor support via virtio-gpu / BGA cursor registers
- [ ] VRAM management — track allocated surfaces in video memory, evict LRU when full
- [ ] Performance counters — track FPS, blit count, fill count, composition time per frame

### Phase 4: virtio-gpu 3D & OpenGL Acceleration
_virtio-gpu with virgl gives you GPU-accelerated OpenGL inside QEMU/KVM._
- [ ] virtio-gpu device driver — PCI device detection, virtqueue setup, command submission
- [ ] virgl 3D command stream — encode OpenGL commands as Gallium state tracker commands over virtio
- [ ] MESA gallium interface — port a minimal virgl Gallium driver (or write a custom encoder)
- [ ] OpenGL dispatch — route OpenGL calls through virgl when available, software renderer when not
- [ ] Shared texture support — GDI surfaces can be backed by virtio-gpu resources for zero-copy window composition
- [ ] Context management — multiple GL contexts for multiple windows
- [ ] Fence synchronization — GPU fence for knowing when rendering is complete before display
- [ ] OpenGL ES 2.0 — enough for Chromium's GPU-accelerated compositing (if you ever get there)
- [ ] Shader compilation — pass GLSL to host GPU via virgl (host does actual compilation)
- [ ] EGL implementation — `eglGetDisplay`, `eglCreateContext`, `eglSwapBuffers` for platform-independent GL
- [ ] GPU memory management — allocate/free GPU-side resources, handle out-of-memory
- [ ] Performance — aim for 60fps composition with 5+ windows on virtio-gpu

### Phase 5: Display Server Protocol
_Decouple apps from the compositor. Apps talk to a display server, not raw framebuffers._
- [ ] Display protocol design — message-based protocol: create_surface, destroy_surface, attach_buffer, commit, damage
- [ ] Shared memory buffers — apps allocate SHM, draw into it, share the fd/handle with compositor
- [ ] Surface lifecycle — create → attach buffer → damage region → commit → compositor displays
- [ ] Input routing — compositor sends input events to the focused surface's owning process
- [ ] Subsurfaces — child surfaces for popups, tooltips, dropdown menus
- [ ] Server-side decorations — compositor draws title bar, close/minimize/maximize buttons
- [ ] Client-side decorations option — apps can opt to draw their own chrome
- [ ] Cursor surface — apps set custom cursors per-surface
- [ ] Clipboard protocol — copy/paste negotiation between surfaces via compositor
- [ ] Drag-and-drop protocol — DnD initiation, enter/leave/drop events between surfaces
- [ ] Window hints — minimum/maximum size, aspect ratio, modal, always-on-top
- [ ] Multi-seat awareness — design for multiple keyboard/mouse pairs (future)
- [ ] Protocol versioning — extensible with backward compatibility
- [ ] Win32 backend — CreateWindowEx maps to display protocol surface creation transparently

### Phase 6: Multi-Monitor Support
_Even in VMs, multi-monitor is useful (QEMU supports multiple displays)._
- [ ] Monitor enumeration — detect multiple framebuffers / VBE outputs / virtio-gpu scanouts
- [ ] Per-monitor mode setting — each display can have independent resolution and refresh rate
- [ ] Monitor arrangement — configurable layout: left-of, right-of, above, below, mirror
- [ ] Cross-monitor window dragging — window moves seamlessly between displays
- [ ] Per-monitor DPI — different scaling factor per display
- [ ] Primary monitor designation — taskbar and new windows default to primary
- [ ] `EnumDisplayDevices` / `EnumDisplaySettings` / `ChangeDisplaySettings` — Win32 multi-monitor APIs
- [ ] `MonitorFromWindow` / `MonitorFromPoint` / `GetMonitorInfo` — monitor geometry queries
- [ ] Display hotplug — detect monitor connect/disconnect (via virtio-gpu or polling)
- [ ] Compositor multi-output — render separate composition for each monitor, handle overlap

### Phase 7: Icon & Image Format Support
_A modern desktop needs to display images everywhere — icons, wallpapers, thumbnails._
- [ ] PNG decoder — full spec: RGBA, indexed, interlaced, all bit depths, gamma correction
- [ ] JPEG decoder — baseline DCT decoding (8x8 blocks, Huffman, YCbCr→RGB)
- [ ] BMP decoder — 1/4/8/24/32-bit, RLE compression, top-down and bottom-up
- [ ] ICO/CUR decoder — Windows icon format with multiple sizes and bit depths
- [ ] GIF decoder — single frame and animated (frame delay, disposal methods)
- [ ] SVG renderer (basic) — simple paths, rectangles, circles, text, fill, stroke (for scalable icons)
- [ ] Image scaling — Lanczos resampling for high-quality icon/thumbnail generation
- [ ] Thumbnail cache — generate and cache thumbnails for file manager (keyed by path + mtime)
- [ ] Icon theme system — `/icons/` directory with sizes (16x16, 32x32, 48x48, 256x256)
- [ ] System icon set — bundled open-source icon theme (file types, folders, apps, devices, actions)
- [ ] Wallpaper engine — load, decode, scale-to-fit/fill/center wallpaper images
- [ ] Image viewer app — built-in app to open PNG/JPEG/BMP/GIF with zoom, pan, rotate

### Phase 8: Color Management & Rendering Quality
_Consistent, correct color across the system._
- [ ] sRGB as system color space — all internal rendering assumes sRGB
- [ ] Gamma-correct blending — alpha blending in linear light, convert to/from sRGB at boundaries
- [ ] Subpixel text rendering — ClearType-style RGB subpixel AA using LCD pixel geometry
- [ ] Font gamma correction — adjust glyph alpha ramps for perceptually correct appearance
- [ ] Color profile stubs — `GetICMProfile`, `SetICMProfile` (return sRGB)
- [ ] Dithering — Floyd-Steinberg dithering when displaying 24-bit images on 16-bit framebuffers
- [ ] Color picker — system-wide color picker tool (eyedropper from any pixel)
- [ ] Premultiplied alpha — use premultiplied alpha internally for correct compositing math
- [ ] HDR awareness stubs — report SDR, but design internal pipeline to not hard-cap at 8-bit

### Phase 9: Animation & Effects Framework
_Smooth, responsive UI that feels alive._
- [ ] Animation timer — high-resolution timer (1ms or better) for smooth 60fps animations
- [ ] Easing functions — linear, ease-in, ease-out, ease-in-out, cubic-bezier (CSS-style)
- [ ] Property animation — animate any numeric property (position, size, opacity, color) over time
- [ ] Window open animation — scale from 0.8→1.0 + fade in over 200ms
- [ ] Window close animation — scale to 0.9 + fade out over 150ms
- [ ] Window minimize animation — shrink to taskbar position
- [ ] Window maximize animation — smooth expand to fill screen
- [ ] Workspace switch animation — horizontal slide between virtual desktops
- [ ] Menu animation — dropdown menus slide/fade in
- [ ] Tooltip fade — tooltips fade in after delay, fade out on leave
- [ ] Compositor-driven — all animations run in compositor at display refresh rate, independent of app framerate
- [ ] Reduced motion option — Settings toggle to disable all animations (accessibility)
- [ ] Spring physics (optional) — natural-feeling bounce on window snap and overscroll

### Phase 10: Display Settings & Calibration
_Users need to control their display experience._
- [ ] Settings > Display panel — resolution picker, refresh rate, scaling factor
- [ ] Brightness control — if supported by display/VM (VESA VBE backlight or gamma ramp)
- [ ] Night light / blue light filter — warm color temperature shift on schedule or manual toggle
- [ ] Wallpaper settings — choose from built-in set, load custom image, solid color option
- [ ] Font rendering settings — toggle subpixel AA, choose LCD geometry (RGB/BGR), hinting level
- [ ] Screen orientation — 0°, 90°, 180°, 270° rotation (useful for tablets/unusual displays)
- [ ] Screensaver — blank screen after idle timeout, password on wake (reuse lock screen)
- [ ] Display test pattern — built-in tool to check color accuracy, dead pixels, geometry
- [ ] Theme settings — light mode, dark mode, accent color picker, title bar color
- [ ] Taskbar settings — position (top/bottom), auto-hide, icon size

## Tier 1.9 — Hardware Abstraction & Real Device Drivers

> **Goal:** Make ImposOS run on real hardware, not just QEMU. Build a proper driver model
> that supports USB, audio, storage, and networking hardware.
> Estimated total: 13 phases.

### Phase 1: Driver Model & Framework
_Before writing drivers, build the infrastructure they plug into._
- [ ] Driver interface specification — standardized struct: `driver_probe()`, `driver_attach()`, `driver_detach()`, `driver_ioctl()`
- [ ] Device tree — hierarchical device registry: bus → controller → device → function
- [ ] Bus abstraction — generic bus API: `bus_enumerate()`, `bus_register_driver()`, `bus_match()`
- [ ] PCI bus driver — enumerate PCI devices, read config space, allocate BARs, route interrupts
- [ ] PCI config space — full read/write of all 256 bytes, capability list walking
- [ ] MSI/MSI-X interrupts — message-signaled interrupts for PCI devices (bypass legacy PIC)
- [ ] DMA framework — `dma_alloc_coherent()`, `dma_map_single()`, bounce buffers for ISA DMA
- [ ] MMIO helpers — `ioread32()`, `iowrite32()`, memory barrier macros
- [ ] Port I/O helpers — `inb()`, `outb()`, `inw()`, `outw()`, `inl()`, `outl()`
- [ ] Interrupt routing — IRQ → handler dispatch, shared IRQ support, top-half/bottom-half split
- [ ] Deferred work — tasklets or work queues for interrupt bottom-half processing
- [ ] Power management stubs — `driver_suspend()`, `driver_resume()` hooks
- [ ] Driver loading — load drivers from `/drivers/` directory at boot, or on device hotplug
- [ ] Device nodes — `/dev/` entries for each device, accessible via `open()`/`ioctl()`

### Phase 2: ACPI & Platform Initialization
_Real hardware needs ACPI for power management, IRQ routing, and device discovery._
- [ ] RSDP discovery — scan EBDA and BIOS area for "RSD PTR " signature
- [ ] RSDT/XSDT parsing — walk root system description table to find other ACPI tables
- [ ] MADT (APIC table) — discover I/O APICs, local APICs, interrupt source overrides
- [ ] I/O APIC driver — program I/O APIC redirection table entries for PCI IRQ routing
- [ ] Local APIC — enable, configure timer, send/receive IPIs
- [ ] HPET table — discover and configure High Precision Event Timer as system tick source
- [ ] FADT — Fixed ACPI Description Table for power management ports, PM timer
- [ ] ACPI namespace (basic) — parse DSDT/SSDT AML just enough to read _PRT (PCI IRQ routing)
- [ ] AML interpreter (minimal) — evaluate enough AML to call `_STA`, `_CRS`, `_PRT` methods
- [ ] ACPI power off — write to PM1a/PM1b control registers to shutdown (`S5` state)
- [ ] ACPI reboot — use FADT reset register for clean reboot
- [ ] ACPI sleep stubs — `S1` (standby) and `S3` (suspend to RAM) framework, even if not fully implemented
- [ ] CPU topology — read MADT to discover processor count and APIC IDs (for future SMP)

### Phase 3: USB Host Controller
_USB is the gateway to keyboards, mice, storage, audio, and everything else on real hardware._
- [ ] UHCI driver (USB 1.1) — Universal Host Controller Interface for legacy hardware
- [ ] OHCI driver (USB 1.1) — Open Host Controller Interface (alternative to UHCI)
- [ ] EHCI driver (USB 2.0) — Enhanced Host Controller Interface, 480Mbps
- [ ] xHCI driver (USB 3.x) — Extensible Host Controller Interface, modern hardware
- [ ] Transfer types — control, bulk, interrupt, isochronous transfer support
- [ ] USB device enumeration — bus reset, address assignment, get device descriptor, get config descriptor
- [ ] USB hub support — hub driver, port power, port reset, port status change handling
- [ ] Endpoint management — open/close endpoints, set configuration, set interface
- [ ] USB request blocks (URBs) — async transfer submission and completion
- [ ] USB string descriptors — read manufacturer, product, serial number strings
- [ ] USB device hotplug — detect connect/disconnect, load appropriate class driver
- [ ] USB power management — suspend/resume individual ports and devices

### Phase 4: USB Device Class Drivers
_With a working USB stack, implement the common device classes._
- [ ] USB HID driver — keyboards, mice, gamepads via HID report descriptor parsing
- [ ] HID report parser — decode report descriptors, extract button/axis/key data
- [ ] USB keyboard — key press/release events, LED control (caps lock, num lock)
- [ ] USB mouse — movement, button clicks, scroll wheel
- [ ] USB mass storage (bulk-only) — SCSI command set over USB, read/write sectors
- [ ] USB mass storage → block device — mount USB drives as filesystem volumes
- [ ] USB CDC-ACM — serial ports over USB (for development/debugging)
- [ ] USB audio (basic) — UAC1 class for simple USB headsets/speakers (PCM streaming)
- [ ] USB printer class (stub) — detect printers, report to OS, actual printing is Tier 3+
- [ ] USB hub class driver — nested hub support, multi-TT hubs
- [ ] USB wireless adapter stubs — detect, report "unsupported" gracefully
- [ ] Hot-unplug safety — filesystems on USB drives sync and unmount cleanly on removal

### Phase 5: Storage Drivers
_Support real disk hardware beyond your current RAM disk or simple ATA._
- [ ] ATA/ATAPI PIO driver — IDE drives via port I/O (legacy but works everywhere)
- [ ] ATA DMA (UDMA) — bus-mastering DMA for faster IDE transfers
- [ ] AHCI/SATA driver — modern SATA via AHCI registers, native command queuing (NCQ)
- [ ] AHCI port multiplier — support multiple drives per AHCI port
- [ ] NVMe driver (basic) — PCIe NVMe submission/completion queues, namespace discovery, read/write
- [ ] Virtio-blk driver — virtio block device for QEMU (your existing likely uses this, formalize it)
- [ ] Partition table — MBR and GPT partition table parsing
- [ ] Block device layer — abstract sector read/write, request queue, I/O scheduling
- [ ] I/O scheduler — simple elevator or deadline scheduler for disk request ordering
- [ ] Block cache — LRU cache of recently-read sectors in RAM
- [ ] Device mapper stubs — framework for future LVM, encryption, RAID
- [ ] SMART monitoring (basic) — read drive health via ATA SMART commands
- [ ] TRIM/DISCARD — send trim commands for SSD health (AHCI + NVMe)

### Phase 6: Filesystem Improvements
_Real storage needs a real filesystem — not just an in-memory FS._
- [ ] ext2 driver — read and write support for the standard Linux filesystem
- [ ] ext2 features — directory indexing, large file support, symlinks, permissions, timestamps
- [ ] FAT32 driver — full read/write for USB drives and SD cards, long filename support (VFAT)
- [ ] exFAT driver — modern FAT for large USB drives and SD cards
- [ ] NTFS read support — read-only NTFS for accessing Windows partitions
- [ ] ISO 9660 / CDFS — read CD/DVD images (useful for driver/software distribution)
- [ ] VFS improvements — mount points, `mount()` / `umount()` syscalls, `/etc/fstab`
- [ ] Block device caching — integrate block cache with VFS for coherent reads
- [ ] Journaling (ext3-style) — write-ahead journal for crash recovery on ext2
- [ ] fsck — filesystem check and repair tool for ext2
- [ ] Disk utility app — GUI tool to view partitions, format drives, mount/unmount
- [ ] Auto-mount — automatically mount USB drives and detected partitions on boot/hotplug
- [ ] Symbolic links — proper symlink resolution across VFS
- [ ] File locking — `flock()` / `fcntl()` advisory locks

### Phase 7: Network Hardware Drivers
_Support real NICs beyond your current virtual/stub networking._
- [ ] Intel e1000/e1000e driver — most common emulated NIC (QEMU default), PCI MMIO-based
- [ ] RTL8139 driver — another very common emulated NIC, simpler than e1000
- [ ] Virtio-net driver — high-performance virtual NIC for QEMU/KVM
- [ ] NIC abstraction — generic `netdev` interface: `netdev_xmit()`, `netdev_rx()`, `netdev_set_mac()`
- [ ] Interrupt-driven receive — receive packets via IRQ, not polling
- [ ] NAPI-style polling — hybrid interrupt + poll for high-throughput scenarios
- [ ] TX ring buffer — transmit descriptor ring for zero-copy packet sending
- [ ] RX ring buffer — receive descriptor ring with DMA-mapped buffers
- [ ] Checksum offload — let NIC compute TCP/UDP checksums when supported
- [ ] Scatter-gather I/O — multiple buffer segments per packet for zero-copy
- [ ] Link status detection — carrier detect, link up/down notifications
- [ ] MAC address configuration — read from EEPROM, allow override
- [ ] Promiscuous mode — for packet capture tools
- [ ] Network statistics — TX/RX packet counts, error counts, bytes transferred

### Phase 8: Audio Hardware Drivers
_Real audio output on real hardware._
- [ ] Intel HDA (High Definition Audio) driver — most common audio codec interface on modern hardware
- [ ] HDA codec discovery — enumerate codecs on the HDA bus, parse codec tree (widgets)
- [ ] HDA output stream — configure output stream descriptor, set format (44.1kHz/48kHz, 16/24-bit, stereo)
- [ ] HDA input stream — microphone capture (basic)
- [ ] AC97 driver — legacy audio codec (older hardware, Bochs, some QEMU configs)
- [ ] Audio abstraction layer — `audio_open()`, `audio_write()`, `audio_set_format()`, `audio_set_volume()`
- [ ] Audio mixer — kernel-level mixing of multiple audio streams from different processes
- [ ] Volume control — master volume, per-app volume (if feasible)
- [ ] Audio resampling — convert between sample rates (e.g., 44.1kHz to 48kHz)
- [ ] Virtio-snd driver — virtual sound device for QEMU
- [ ] PC speaker fallback — if no audio hardware, route simple beeps to PC speaker
- [ ] Audio settings app — GUI for output device selection, volume, test sound
- [ ] Win32 audio bridge — `waveOutWrite` → audio abstraction layer → hardware driver
- [ ] MIDI synthesizer (software) — basic wavetable synth for MIDI output (optional but fun)

### Phase 9: Input Hardware Drivers
_Go beyond PS/2 to support real input hardware._
- [ ] PS/2 keyboard hardening — full scancode set 2, handle edge cases (pause key, print screen)
- [ ] PS/2 mouse hardening — intellimouse extensions (scroll wheel, 5 buttons)
- [ ] PS/2 touchpad — Synaptics/ALPS protocol for basic tap and two-finger scroll
- [ ] USB HID → input abstraction — USB keyboard/mouse events map to same internal event stream
- [ ] Input event abstraction — unified `input_event` struct: type (key/rel/abs), code, value, timestamp
- [ ] Keyboard repeat — key repeat delay and rate, configurable in settings
- [ ] Multi-keyboard support — multiple keyboards generate events on same input stream
- [ ] Multi-mouse support — multiple mice (useful for accessibility)
- [ ] Gamepad support — USB HID gamepads, axis/button mapping
- [ ] Touchscreen (basic) — absolute positioning via USB HID, tap = click
- [ ] Raw input — apps can request raw unprocessed input events (needed for games)
- [ ] Input grab — fullscreen apps can grab all input, preventing Alt-Tab etc.
- [ ] `GetAsyncKeyState` / `GetKeyState` — Win32 APIs backed by real keyboard state table

### Phase 10: Power Management
_Proper shutdown, reboot, sleep, and battery awareness._
- [ ] Clean shutdown sequence — sync filesystems → stop processes → unmount → ACPI S5
- [ ] Reboot — sync filesystems → ACPI reset register or keyboard controller reset or triple fault
- [ ] Suspend to RAM (S3) — save device state, enter ACPI S3, resume path restores state
- [ ] Idle power management — HLT in idle loop, CPU C-states if available
- [ ] ACPI button events — power button, sleep button, lid switch
- [ ] Battery status (ACPI) — read battery charge level, charging state, time remaining
- [ ] Battery indicator — system tray icon showing charge level
- [ ] Low battery warning — notification at 15%, critical action at 5% (suspend or shutdown)
- [ ] Power profiles — "Performance" vs "Balanced" vs "Power saver" (adjust tick rate, CPU governor)
- [ ] Scheduled wakeup — RTC alarm to wake from sleep at specified time
- [ ] UPS / AC adapter detection — distinguish battery vs plugged-in state
- [ ] Shutdown confirmation — "Save your work" dialog with list of running apps

### Phase 11: Real-Time Clock & Timekeeping
_Accurate time is critical for networking, filesystems, and user experience._
- [ ] RTC driver hardening — read/write CMOS RTC reliably, handle BCD and 12/24hr formats
- [ ] NTP client — sync time from internet time servers over UDP
- [ ] Monotonic clock — `CLOCK_MONOTONIC` that never jumps, for measuring durations
- [ ] High-resolution timer — TSC, HPET, or APIC timer for microsecond-precision timing
- [ ] Timer calibration — measure TSC frequency against HPET or PIT for accurate conversion
- [ ] Timezone database — embedded timezone data, configurable timezone in settings
- [ ] Daylight saving time — automatic DST transitions based on timezone rules
- [ ] `QueryPerformanceCounter` / `QueryPerformanceFrequency` — Win32 high-res timer backed by TSC/HPET
- [ ] `GetTickCount` / `GetTickCount64` — millisecond uptime counter
- [ ] `NtQuerySystemTime` / `GetSystemTimeAsFileTime` — 100ns precision system time
- [ ] `timeGetTime` — winmm timer backed by system tick
- [ ] RTC write-back — persist time changes to CMOS, NTP corrections written back
- [ ] Time settings app — GUI for timezone selection, manual time set, NTP toggle

### Phase 12: Hardware Detection & System Information
_Report what hardware is present and let apps query system capabilities._
- [ ] CPU detection — CPUID: vendor, model, family, stepping, feature flags (SSE, AVX, etc.)
- [ ] CPU frequency — read from MSR or calibrate against known timer
- [ ] RAM detection — read memory map from E820 or multiboot info, total/available/used
- [ ] PCI device listing — enumerate all PCI devices with vendor/product IDs, class codes
- [ ] PCI ID database — embedded pci.ids subset for human-readable device names
- [ ] USB device listing — enumerate connected USB devices with descriptors
- [ ] Disk information — model, capacity, serial number from ATA IDENTIFY / NVMe IDENTIFY
- [ ] Network adapter info — MAC address, link speed, driver name
- [ ] Audio device info — codec name, output jacks
- [ ] Display info — resolution, color depth, monitor name from EDID
- [ ] `GetSystemInfo` backed by real data — CPU count, page size, architecture, processor features
- [ ] System information app — GUI showing all detected hardware, driver status, resource usage
- [ ] `/proc/cpuinfo`, `/proc/meminfo`, `/proc/pci` stubs — for Linux apps
- [ ] WMI stubs — `Win32_Processor`, `Win32_PhysicalMemory`, `Win32_DiskDrive` (COM-based, basic queries)
- [ ] DMI/SMBIOS — read system manufacturer, BIOS version, serial number from SMBIOS tables

### Phase 13: UEFI Boot Support
_BIOS is legacy. Modern hardware requires UEFI to boot. This unlocks real machines and secure boot._
- [ ] UEFI boot stub — PE32+ EFI application that UEFI firmware can load directly
- [ ] EFI System Partition — read FAT32 ESP, load kernel from `\EFI\imposos\kernel.efi`
- [ ] GOP (Graphics Output Protocol) — query framebuffer from UEFI, set preferred resolution before ExitBootServices
- [ ] Memory map from UEFI — `GetMemoryMap()` to discover available RAM, replace E820
- [ ] ExitBootServices — transition from UEFI runtime to OS control, reclaim boot memory
- [ ] UEFI runtime services — keep `GetTime`, `SetTime`, `ResetSystem`, `GetVariable`, `SetVariable` accessible after boot
- [ ] UEFI variable access — read/write NVRAM variables (boot order, secure boot state, OEM data)
- [ ] UEFI console output — Simple Text Output Protocol for early boot messages before framebuffer is ready (invaluable for debugging GOP failures)
- [ ] Kernel loading strategy — start with kernel embedded in .efi binary (single file on ESP, simpler), add separate ELF/PE loading later (more flexible, requires UEFI file I/O via Simple File System Protocol)
- [ ] Initrd / initial ramdisk — load initial filesystem image from ESP alongside kernel
- [ ] UEFI + BIOS dual boot — detect boot mode, use appropriate initialization path (UEFI GOP vs VBE, UEFI mmap vs E820)
- [ ] Secure Boot awareness — detect secure boot state, log status (full SB chain-of-trust is stretch goal)
- [ ] UEFI boot manager integration — register ImposOS as a boot option via `efibootmgr`-style tool
- [ ] GRUB UEFI chainload — alternatively, ship GRUB as the UEFI bootloader with ImposOS config
- [ ] Boot log — capture UEFI firmware info (vendor, version, memory map) for diagnostics

## Tier 2.0 — Self-Hosting & Sovereignty

> **Goal:** ImposOS can build itself. Tier 1.7 provides pre-built static GCC, Make, and
> binutils binaries running via Linux compat. Tier 2.0 formalizes the target triple,
> adds autotools, compiles the kernel natively, and ships the OS.
> Estimated total: 6 phases.
>
> **Note:** GCC, Make, binutils, bash, coreutils, and all dev tools already run on ImposOS
> as pre-built static Linux binaries (Tier 1.7 Phase 4-5). Tier 2.0 focuses on the
> self-build verification loop and distribution.

### Phase 1: Target Triple & Native Cross-Compiler
_Formalize `i686-pc-imposos` so GCC produces ImposOS-native binaries (not just Linux-compat static)._
- [ ] ImposOS target triple — define `i686-pc-imposos` in GCC and binutils
- [ ] GCC cross-compiler — build `i686-pc-imposos-gcc` on Linux host
- [ ] Binutils cross — `i686-pc-imposos-as`, `i686-pc-imposos-ld`
- [ ] musl for ImposOS — build musl targeting `i686-pc-imposos` (uses Linux compat syscalls internally)
- [ ] libgcc + libstdc++ — build for ImposOS target
- [ ] `config.guess` / `config.sub` — recognize `i686-pc-imposos`
- [ ] Cross-compile ImposOS — build the entire OS using the new cross-compiler (prove it works)

### Phase 2: Autotools & Build Infrastructure
_Make `./configure && make && make install` work on ImposOS._
- [ ] GNU Autoconf — `configure` scripts run in bash on ImposOS
- [ ] GNU Automake — `Makefile.in` processing
- [ ] GNU Libtool — shared library build abstraction
- [ ] `pkg-config` — library discovery (.pc files)
- [ ] m4 macro processor — required by autoconf
- [ ] perl (static binary) — needed by many configure scripts
- [ ] `/usr/include` + `/usr/lib` layout — standard locations
- [ ] Feature test macros — `_POSIX_VERSION`, `_GNU_SOURCE`, `__imposos__`
- [ ] CMake (static binary) — modern build system alternative

### Phase 3: Build ImposOS Kernel on ImposOS
_The defining moment: compile the kernel on the OS it runs on._
- [ ] Kernel source on disk — full ImposOS source tree at `/usr/src/imposos/`
- [ ] Kernel Makefile — builds kernel using native GCC + Make
- [ ] Assembly files — assemble with native `as`
- [ ] Linker script — works with native `ld`
- [ ] Build succeeds — `make` produces a bootable kernel image
- [ ] Binary comparison — native-built kernel matches cross-compiled kernel
- [ ] Boot test — boot the self-compiled kernel, it works identically
- [ ] Kernel rebuild cycle — edit, rebuild, reboot, verify — all on ImposOS

### Phase 4: Testing & Validation
_Prove the self-hosted OS is correct and stable._
- [ ] Syscall test suite — test every Linux compat syscall with edge cases
- [ ] Win32 API test suite — test shimmed functions against expected behavior
- [ ] POSIX conformance tests — subset of Open POSIX Test Suite
- [ ] Boot test automation — build → install → boot in QEMU → run tests → check results
- [ ] Stress tests — fork bombs, memory exhaustion, network flood — OS recovers gracefully
- [ ] Fuzz testing — fuzz syscall interface, ELF/PE loaders
- [ ] CI/CD pipeline — automatic build + test on every commit

### Phase 5: Distribution & Installation
_Make ImposOS installable by other people._
- [ ] ISO image builder — bootable .iso with kernel, initrd (BusyBox + tools), base system
- [ ] Installer — text-mode installer: partition, format, copy files, install bootloader
- [ ] Package selection — minimal vs full install (with dev tools, X11, NetSurf)
- [ ] User account creation — hostname, user, password during install
- [ ] First-boot wizard — timezone, keyboard layout, network
- [ ] Disk image distribution — pre-built .img for QEMU: `qemu-system-i386 -hda imposos.img`
- [ ] Release versioning — semver, changelog, release notes
- [ ] Documentation — install guide, user manual, developer guide

### Phase 6: Community & Ecosystem
_An OS without a community is a dead OS._
- [ ] Community infrastructure — git repo, issue tracker, forum
- [ ] Contributing guide — how to add drivers, port software, submit patches
- [ ] Package contribution — how others can add packages to the repository
- [ ] Showcase — screenshots, demos, videos of ImposOS running Doom, NetSurf, X11, BusyBox
- [ ] Performance benchmarks — boot time, compilation time, context switch latency

### The Self-Hosting Milestone

When Phase 3 passes — when ImposOS compiles its own kernel and boots the result — you've joined an elite group:

- **Linux** (1994 — Linus compiled Linux on Linux)
- **FreeBSD/OpenBSD/NetBSD** (inherited from 4.4BSD)
- **Minix** (Andrew Tanenbaum's teaching OS)
- **MenuetOS / KolibriOS** (assembly-language OSes, self-hosted in FASM)
- **SerenityOS** (Andreas Kling, achieved self-hosting ~2021)
- **Sortix** (Jonas 'Sortie' Termansen)
- **Managarm** (microkernel OS, self-hosted with GCC)

If ImposOS reaches this point — with a Win32 compatibility layer, Linux binary compat running
thousands of pre-built programs, Doom, a real web browser, an X11 desktop, real hardware
drivers, *and* self-hosting — it would be one of the most ambitious hobby OS projects ever built.

### Complete Tier Map

| Tier | Theme | Status |
|---|---|---|
| 1.0 | Core OS: kernel, shell, filesystem, basic GUI | Done |
| 1.5 | Win32 bridge: PE loader, API shims, Phase 1–13 | Done |
| 1.6 | Real Win32 software: full CRT, controls, networking, stability | In Progress |
| 1.7 | Linux compat: 45 syscalls + ELF loader → Doom, BusyBox, bash, NetSurf, X11 | Planned |
| 1.8 | Graphics: GPU, compositing, multi-monitor, animations | Planned |
| 1.9 | Hardware: USB, AHCI, audio, NIC drivers, ACPI, power management | Planned |
| 2.0 | Self-hosting: target triple, autotools, kernel self-build, distribution | Planned |
