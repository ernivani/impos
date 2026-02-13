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
- [ ] `<stdio.h>` completeness — sprintf, snprintf, sscanf, fprintf, fseek, ftell, rewind, tmpfile, perror, setvbuf
- [ ] `<stdlib.h>` completeness — atoi, atof, strtol, strtod, strtoul, qsort, bsearch, abs, labs, div, ldiv, system, getenv, putenv, _itoa, _atoi64
- [x] `<string.h>` completeness — strncpy, strncat, strncmp, strstr, strchr, strrchr, strtok, memset, memmove, memcmp, _stricmp, _strnicmp, _strdup
- [ ] `<math.h>` — sin, cos, tan, sqrt, pow, log, log10, exp, floor, ceil, fabs, fmod, atan2, asin, acos (link to soft-float or FPU)
- [ ] `<time.h>` — time, localtime, gmtime, mktime, strftime, difftime, clock, _ftime
- [x] `<ctype.h>` — isalpha, isdigit, isspace, toupper, tolower, isalnum, isprint, etc.
- [ ] `<setjmp.h>` — setjmp / longjmp (critical for error recovery in C code)
- [ ] `<signal.h>` — signal, raise (SIGSEGV, SIGABRT, SIGFPE handlers)
- [ ] `<locale.h>` — setlocale, localeconv (most apps call setlocale on startup)
- [x] `<errno.h>` — per-thread errno via TLS, all standard error codes
- [x] `_beginthread` / `_endthread` (simplified thread wrappers beyond _beginthreadex)
- [ ] `_open` / `_read` / `_write` / `_close` / `_lseek` — POSIX-style low-level I/O in msvcrt
- [ ] `_stat` / `_fstat` / `_access` — file info queries
- [x] `_snprintf` / `_vsnprintf` — Windows-specific format variants
- [ ] msvcrt global state — `_acmdln`, `_environ`, `__argc`, `__argv`, `_pgmptr` initialization
- [ ] C++ operator new / delete — map to HeapAlloc/HeapFree with proper alignment
- [ ] C++ RTTI stubs — `typeid`, `dynamic_cast` support structures
- [x] `atexit` / `_onexit` — shutdown callback chain

### Phase 2: Exception Handling Hardening
_Phase 12 of Tier 1.5 adds basic SEH. This phase makes it bulletproof — real apps crash without it._
- [x] Full SEH chain walking — nested __try/__except/__finally with correct unwinding order
- [ ] C++ exception infrastructure — `__CxxFrameHandler3` with proper catchable type matching, `_CxxThrowException`
- [ ] Exception info structures — `ThrowInfo`, `CatchableTypeArray`, `CatchableType` with type_info matching
- [ ] Stack unwinding with destructors — unwind calls dtors for stack objects (C++ RAII depends on this)
- [ ] Vectored Exception Handling — `AddVectoredExceptionHandler` / `RemoveVectoredExceptionHandler`
- [ ] `__CppXcptFilter` — C++ exception filter used by CRT startup
- [ ] `_set_se_translator` — SEH-to-C++ exception translation
- [ ] Guard pages — `STATUS_GUARD_PAGE_VIOLATION` for stack growth detection
- [x] `RaiseException` with custom exception codes
- [ ] Unhandled exception dialog — show crash info (address, registers, stack trace) in a WM dialog instead of silent death

### Phase 3: Full Unicode & Internationalization
_Phase 10 of Tier 1.5 adds W-suffix APIs. This phase makes Unicode actually work end-to-end._
- [ ] Correct UTF-16 surrogate pair handling in all W APIs
- [ ] Full `MultiByteToWideChar` / `WideCharToMultiByte` with all code pages (CP_ACP, CP_UTF8, 1252, etc.)
- [ ] `CharUpperW` / `CharLowerW` / `CharNextW` / `IsCharAlphaW` — character classification
- [ ] `CompareStringW` / `LStrCmpIW` — locale-aware string comparison
- [ ] `GetACP` / `GetOEMCP` / `IsValidCodePage`
- [ ] `wsprintfW` / `wvsprintfW`
- [ ] NLS stubs — `GetLocaleInfoW`, `GetNumberFormatW`, `GetDateFormatW`, `GetTimeFormatW`
- [ ] Internal kernel string handling — all paths stored as UTF-8, convert at syscall boundary
- [ ] Font rendering with Unicode glyph support — render CJK, Cyrillic, Arabic (at least basic Latin + extended)
- [ ] Console code page support — `SetConsoleCP`, `SetConsoleOutputCP`

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
- [ ] Crash dialog with stack trace — on unhandled exception, show faulting address, loaded DLLs, call stack
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

## Tier 1.7 — From Running Real Software to a Polished Platform

> **Goal:** After Tier 1.6, ImposOS runs real Win32 apps. Tier 1.7 makes it a platform
> people would actually *want* to use — with proper graphics, real networking,
> a working browser, plugin systems, and the foundation for modern applications.
> Estimated total: 12 phases.

### Phase 1: Software Rendering Engine (Skia-class 2D)
_Your GDI shims do basic drawing. This phase builds a real 2D engine that apps can actually rely on._
- [ ] Scanline rasterizer — filled triangles, polygons, and paths with sub-pixel accuracy
- [ ] Anti-aliased line and curve rendering — Bresenham is not enough for real apps
- [ ] Bézier curve support — quadratic and cubic, used by fonts, SVG, and every vector app
- [ ] Alpha blending engine — per-pixel ARGB compositing with Porter-Duff modes (SrcOver, SrcIn, DstOut, etc.)
- [ ] Gradient fills — linear and radial gradients with arbitrary color stops
- [ ] Image scaling — bilinear and bicubic interpolation for `StretchBlt` and image display
- [ ] Affine transforms on bitmaps — rotate, scale, skew any image
- [ ] Clipping engine — arbitrary polygon and path-based clipping regions, not just rectangles
- [ ] Color space support — sRGB as default, basic ICC profile awareness
- [ ] Compositor — layer-based compositing for window manager (each window = layer, alpha blend the stack)
- [ ] Double-buffered rendering — eliminate flicker system-wide, present completed frames
- [ ] Dirty rectangle tracking — only re-render changed regions for performance
- [ ] Off-screen render targets — render to texture/bitmap for effects and caching

### Phase 2: TrueType / OpenType Font Engine
_Real apps need real fonts. Bitmap fonts won't cut it for any serious text rendering._
- [ ] TrueType parser — read .ttf files: `head`, `cmap`, `glyf`, `loca`, `hmtx`, `maxp`, `name`, `OS/2`, `post` tables
- [ ] Glyph outline interpreter — parse TrueType quadratic Bézier contours into paths
- [ ] Hinting engine — execute TrueType bytecode instructions (or skip with auto-hinting)
- [ ] Rasterize glyphs — render glyph outlines to bitmaps at requested size
- [ ] Anti-aliased glyph rendering — 256-level grayscale or subpixel (ClearType-style RGB)
- [ ] Glyph cache — LRU cache of rendered glyphs keyed by (font, size, codepoint, flags)
- [ ] Font matching — `CreateFont` / `CreateFontIndirect` match against installed .ttf files by family, weight, style
- [ ] `EnumFontFamiliesEx` backed by real font inventory
- [ ] `GetTextMetrics` / `GetTextExtentPoint32` with accurate measurements from font tables
- [ ] Kerning — read `kern` table, apply pair adjustments
- [ ] OpenType layout (basic) — `GSUB` / `GPOS` for ligatures and positional forms
- [ ] System fonts — bundle 3-4 open-source fonts: a serif (Liberation Serif), sans (Liberation Sans), mono (Liberation Mono), and a symbol/emoji font
- [ ] Font installation — copy .ttf to /fonts, register in registry, available to all apps
- [ ] `AddFontResourceEx` / `RemoveFontResourceEx` — per-session font loading

### Phase 3: OpenGL Software Renderer
_Many real apps need OpenGL. A software implementation unlocks huge categories of software._
- [ ] OpenGL 1.1 core — `glBegin/glEnd`, `glVertex`, `glColor`, `glTexCoord`, `glNormal`
- [ ] Matrix stack — `glPushMatrix`, `glPopMatrix`, `glTranslate`, `glRotate`, `glScale`, `glMultMatrix`
- [ ] Texture mapping — `glGenTextures`, `glBindTexture`, `glTexImage2D`, `glTexParameter` (min/mag filtering)
- [ ] Depth buffer — Z-buffer with `glEnable(GL_DEPTH_TEST)`, depth compare functions
- [ ] Blending — `glBlendFunc` with standard blend equations
- [ ] Lighting — up to 8 lights, `GL_AMBIENT`, `GL_DIFFUSE`, `GL_SPECULAR`, material properties
- [ ] Display lists — `glNewList` / `glCallList` for batch rendering
- [ ] Stencil buffer — 8-bit stencil with compare and operations
- [ ] `wglCreateContext` / `wglMakeCurrent` / `wglDeleteContext` — real implementations instead of stubs
- [ ] `wglGetProcAddress` — extension function loading
- [ ] Pixel buffer readback — `glReadPixels` for screenshots and testing
- [ ] Fog — `glFog` with linear/exp/exp2 modes
- [ ] `glViewport` / `glScissor` — viewport and scissor test
- [ ] OpenGL 2.0 stubs — `glCreateShader`, `glCompileShader` return failure (apps fall back to fixed-function)
- [ ] Frame output — render to offscreen buffer, blit to window via WM

### Phase 4: Network Stack Hardening
_Your TLS and Winsock work. This phase makes networking reliable and complete._
- [ ] TCP retransmission & congestion control — Reno or Cubic (real TCP, not just "send and hope")
- [ ] TCP window scaling — support connections over high-bandwidth links
- [ ] TCP keepalive — `SO_KEEPALIVE` with configurable interval
- [ ] UDP support hardening — fragmentation, checksums, proper error handling
- [ ] ICMP — ping support (used by network diagnostic tools)
- [ ] ARP cache management — timeout, refresh, gratuitous ARP
- [ ] DNS caching — local resolver cache with TTL expiry
- [ ] DNS over TCP — fallback for large responses
- [ ] Multiple simultaneous connections — proper socket multiplexing for apps opening 50+ sockets
- [ ] Non-blocking I/O correctness — `select`, `WSAPoll`, `WSAAsyncSelect` all handle edge cases
- [ ] `SO_LINGER` / `SO_RCVBUF` / `SO_SNDBUF` — socket options that apps actually set
- [ ] Loopback interface — `127.0.0.1` connections without hitting the wire
- [ ] Connection tracking — proper FIN/RST handling, TIME_WAIT state
- [ ] Network error codes — map real failures to `WSAGetLastError` codes (WSAECONNREFUSED, WSAETIMEDOUT, etc.)

### Phase 5: HTTP/HTTPS Client Library
_An internal HTTP engine that WinInet, WinHTTP, and eventually a browser can use._
- [ ] HTTP/1.1 client — full request/response parser, chunked transfer encoding, content-length
- [ ] Connection pooling — keep-alive with connection reuse per host
- [ ] Redirect following — 301, 302, 307, 308 with configurable max redirects
- [ ] Cookie engine — parse Set-Cookie, store, send on matching requests (RFC 6265)
- [ ] HTTPS via your TLS stack — TLS 1.2 mandatory, TLS 1.3 optional
- [ ] Certificate chain validation — verify against embedded root CA bundle
- [ ] Certificate pinning stubs — for apps that pin specific certs
- [ ] Proxy support — HTTP CONNECT for HTTPS-through-proxy, basic/digest proxy auth
- [ ] Compression — gzip and deflate `Accept-Encoding` / `Content-Encoding`
- [ ] Content-Type parsing — charset detection, MIME type handling
- [ ] Multipart form data — `multipart/form-data` for file uploads
- [ ] Authentication — Basic and Digest `WWW-Authenticate` / `Authorization`
- [ ] Timeout handling — connect timeout, read timeout, total request timeout
- [ ] Download progress — callback mechanism for progress reporting
- [ ] Map to WinInet API — `InternetOpenUrl` → HTTP engine, `InternetReadFile` → buffered read
- [ ] Map to WinHTTP API — `WinHttpSendRequest` → HTTP engine

### Phase 6: Lightweight HTML Renderer
_Not Chromium. A simple HTML viewer that can render basic web pages and help text._
- [ ] HTML parser — tokenizer + tree builder for HTML5 subset (div, span, p, h1-h6, a, img, ul, ol, li, table, form, input, br, hr)
- [ ] CSS parser — basic selectors (element, class, id), box model properties, colors, fonts, display, position
- [ ] Layout engine — block and inline flow layout, margin collapsing, basic table layout
- [ ] Box model — margin, border, padding, content with correct sizing
- [ ] Text layout — word wrap, line height, text-align, vertical-align
- [ ] Image loading — fetch `<img src>` via HTTP client, decode PNG/JPEG, inline display
- [ ] Hyperlinks — clickable `<a href>`, history stack (back/forward), URL bar
- [ ] Forms — `<input type=text>`, `<textarea>`, `<select>`, `<button>`, `<input type=submit>`, POST form data
- [ ] CSS cascade — specificity, inheritance, !important
- [ ] Colors and backgrounds — background-color, background-image (solid + simple gradients)
- [ ] Scrolling — vertical scroll with scrollbar for overflow content
- [ ] Basic JavaScript stubs — `document.getElementById`, `alert()`, `console.log` (enough that pages don't error out)
- [ ] DOM manipulation (basic) — `innerHTML`, `textContent`, `style.*` property changes
- [ ] `<style>` and `<link rel=stylesheet>` — embedded and external CSS
- [ ] View source — show raw HTML for debugging
- [ ] about:blank, data: URIs, file:// protocol
- [ ] Wrap in Win32 window — register as a Win32 app, handle WM_SIZE for responsive layout

### Phase 7: POSIX Compatibility Layer
_Many open-source apps target POSIX. A basic layer dramatically expands what you can compile and run._
- [ ] `fork()` — process duplication via page directory COW (copy-on-write)
- [ ] `exec()` family — `execvp`, `execve`, `execl` (replace process image with new ELF/PE)
- [ ] `pipe()` — anonymous pipe pair
- [ ] `dup()` / `dup2()` — file descriptor duplication
- [ ] `open` / `read` / `write` / `close` / `lseek` — POSIX file I/O (separate from Win32 handles)
- [ ] `stat` / `fstat` / `lstat` — file info with `struct stat`
- [ ] `opendir` / `readdir` / `closedir` — directory enumeration
- [ ] `mkdir` / `rmdir` / `unlink` / `rename` / `link` — filesystem operations
- [ ] `mmap` / `munmap` / `mprotect` — memory-mapped I/O (map to VirtualAlloc internally)
- [ ] `waitpid` / `wait` — child process reaping
- [ ] `kill` / `signal` / `sigaction` — signal delivery between processes
- [ ] `getpid` / `getppid` / `getuid` / `getgid` — process and user identity
- [ ] `select` / `poll` — I/O multiplexing on file descriptors
- [ ] `socket` / `bind` / `listen` / `accept` / `connect` (BSD socket API) — map to ImposOS sockets
- [ ] `gettimeofday` / `clock_gettime` — high-resolution time
- [ ] `getcwd` / `chdir` — working directory
- [ ] `environ` global — environment variable access
- [ ] `pthreads` — `pthread_create`, `pthread_join`, `pthread_mutex_*`, `pthread_cond_*`, `pthread_key_*` (TLS)
- [ ] `dlopen` / `dlsym` / `dlclose` — dynamic library loading (map to LoadLibrary internally)
- [ ] `cygwin1.dll` / `msys-2.0.dll` style bridge — or native POSIX subsystem linked at compile time

### Phase 8: ELF Binary Loader
_With POSIX in place, you can run Linux-targeted binaries — massively expanding your software library._
- [ ] ELF32 parser — read ELF header, program headers (PT_LOAD, PT_DYNAMIC, PT_INTERP, PT_PHDR)
- [ ] Section loading — map PT_LOAD segments with correct permissions (RX for .text, RW for .data/.bss)
- [ ] Dynamic linker (ld.so) — resolve DT_NEEDED shared libraries, symbol lookup, relocation
- [ ] Relocation types — R_386_32, R_386_PC32, R_386_GLOB_DAT, R_386_JMP_SLOT, R_386_RELATIVE
- [ ] PLT/GOT — lazy binding via procedure linkage table
- [ ] Symbol versioning — `DT_VERSYM`, `DT_VERNEED` (glibc uses this)
- [ ] `__libc_start_main` — CRT entry point calling constructors, then main()
- [ ] ELF TLS — thread-local storage model (initial-exec and local-exec at minimum)
- [ ] Built-in libc — port musl or newlib as your C library for ELF binaries
- [ ] `vDSO` page — fast `gettimeofday` / `clock_gettime` without syscall overhead
- [ ] `/proc/self` stubs — `/proc/self/exe`, `/proc/self/maps` (many Linux apps read these)
- [ ] Mixed PE/ELF environment — both loaders coexist, file extension or magic number determines loader
- [ ] ELF `.interp` custom dynamic linker — ImposOS ships its own ld-imposos.so
- [ ] `LD_LIBRARY_PATH` equivalent — search paths for shared objects

### Phase 9: ImposOS Native App Framework
_Give developers a proper API to write native ImposOS apps — not just Win32 compatibility._
- [ ] `imposui.h` — native C API: `imposui_create_window`, `imposui_button`, `imposui_label`, `imposui_textbox`, `imposui_list`, `imposui_menu`
- [ ] Event system — callback-based: `imposui_on_click(widget, callback, userdata)`
- [ ] Layout engine — auto-layout: `imposui_vbox`, `imposui_hbox`, `imposui_grid` with padding, spacing, alignment
- [ ] Theme system — JSON-based themes with colors, fonts, border radius, spacing
- [ ] Custom drawing — `imposui_canvas` widget with `imposgfx_*` drawing API (backed by your Phase 1 renderer)
- [ ] File dialogs — native open/save/folder picker
- [ ] Standard dialogs — message box, input box, confirmation, progress
- [ ] Drag and drop — native DnD between ImposOS apps
- [ ] Clipboard integration — read/write text, images, custom formats
- [ ] App manifest format — `app.json` with name, icon, version, permissions, entry point
- [ ] App packaging — `/apps/appname/` directory with manifest, binary, resources
- [ ] IPC mechanism — lightweight message passing between native apps
- [ ] SDK toolchain — headers, static libs, example apps, Makefile templates
- [ ] Documentation — man pages or built-in help viewer for API reference

### Phase 10: Package Manager & Software Repository
_Let users install real software without manually copying files._
- [ ] `winget` improvements — search, install, update, remove, list commands
- [ ] Package manifest format — JSON with name, version, description, author, dependencies, download URL, checksum
- [ ] Remote repository — HTTPS-based package index (JSON catalog file)
- [ ] Dependency resolution — install required packages before the requested one
- [ ] Version constraints — `>=1.0`, `<2.0`, `~1.5` semver-style
- [ ] Integrity verification — SHA-256 checksum on downloaded packages
- [ ] Install scripts — pre-install, post-install, pre-remove, post-remove hooks
- [ ] Uninstall — clean removal of files, registry entries, shortcuts
- [ ] Upgrade path — `winget upgrade --all` to update everything
- [ ] Local package cache — avoid re-downloading on reinstall
- [ ] Multiple repositories — add community repos beyond the default
- [ ] Package signing — RSA/Ed25519 signature verification on packages
- [ ] Self-hosted repo tooling — scripts to build and publish a package index
- [ ] Built-in packages — ship 10-20 pre-packaged apps (text editor, file manager, calculator, hex editor, etc.)

### Phase 11: Accessibility & Input Methods
_Making the OS usable for everyone and supporting non-English input._
- [ ] Keyboard layouts — US QWERTY, UK, AZERTY, QWERTZ, Dvorak (switchable)
- [ ] Dead key support — compose sequences for accented characters (é, ñ, ü)
- [ ] IME framework — input method editor stub for CJK text entry
- [ ] Screen reader hooks — expose window tree, control labels, focus state via accessibility API
- [ ] `IAccessible` COM interface stubs — MSAA for Win32 apps
- [ ] High contrast mode — system-wide theme swap with configurable colors
- [ ] Large cursor option — 2x/3x cursor size
- [ ] Font scaling — system-wide DPI scaling (100%, 125%, 150%, 200%)
- [ ] Sticky keys — modifier key latching for one-handed use
- [ ] Mouse keys — keyboard arrow keys control cursor movement
- [ ] `SystemParametersInfo` — `SPI_GETWORKAREA`, `SPI_GETNONCLIENTMETRICS`, `SPI_GETHIGHCONTRAST`
- [ ] Caret (text cursor) — system-wide blinking caret with `CreateCaret`, `ShowCaret`, `SetCaretPos`
- [ ] Focus management — `SetFocus`, `GetFocus`, `WM_SETFOCUS`/`WM_KILLFOCUS` correct across all windows

### Phase 12: Developer Tools & Debugging
_If developers can't debug apps on your platform, they won't develop for it._
- [ ] Built-in debugger — attach to running process, set breakpoints (INT3), single-step
- [ ] Stack trace — walk EBP chain, resolve symbols from PE/ELF symbol tables
- [ ] `OutputDebugString` viewer — real-time log window for debug output
- [ ] Memory inspector — view/edit process memory, search for patterns
- [ ] Handle viewer — list all open handles (files, windows, GDI, threads) per process
- [ ] PE/ELF inspector — built-in tool to dump headers, imports, exports, resources
- [ ] System monitor — CPU usage, memory usage, process list, thread count, handle count (like Task Manager)
- [ ] API trace — log Win32/POSIX API calls per process with timestamps and return values
- [ ] GDI debug overlay — show window bounds, clipping regions, dirty rects
- [ ] Network monitor — show active sockets, connections, bytes sent/received
- [ ] Registry editor — GUI tool to browse and edit the emulated registry
- [ ] Profiler — sampling profiler that records instruction pointer samples, generates flame graph
- [ ] Core dump — on crash, write process state to file for post-mortem analysis
- [ ] Remote debugging — serial or network debug protocol for kernel-level debugging

### Target Software by Phase Completion

| After Phase | What's Now Possible |
|---|---|
| 1–2 | Apps look professional — anti-aliased text, smooth rendering, real fonts |
| 3 | OpenGL 1.x apps and games — Quake, GLXGears, simple 3D viewers |
| 4–5 | Reliable networking — apps that download, update, communicate over HTTP/HTTPS |
| 6 | Built-in web browser — view documentation, basic web pages, HTML help files |
| 7–8 | Run Linux CLI tools — busybox, coreutils, gcc, Python, Lua, many FOSS tools |
| 9 | Native ImposOS app ecosystem — developers can build polished apps without Win32 |
| 10 | Users can install software easily — app store experience |
| 11 | Usable by non-English speakers and people with disabilities |
| 12 | Developers can build and debug software *on* ImposOS |

### What This Unlocks (Realistic Software Targets)

#### After Full Tier 1.7:

**Win32 apps (expanded):**
- Winamp 2.x/5.x (full plugin support with DLL loading + audio)
- PuTTY, WinSCP (networking + crypto + GUI)
- 7-Zip (GUI version)
- IrfanView (image viewing + GDI + plugins)
- Notepad++/AkelPad (full-featured text editing)
- XP-era games: Minesweeper, Solitaire, Pinball
- Small Delphi/VB6 apps from the shareware era

**OpenGL apps:**
- Quake 1 (software or GL) — if you can run Quake, that's a legendary milestone
- GLXGears and GL demos
- Simple CAD viewers
- Tux Racer / other simple GL games

**Linux/POSIX apps (via ELF + POSIX layer):**
- BusyBox — 300+ Unix utilities in one binary
- Lua / Python (compiled for your target) — scripting language runtime
- SQLite CLI — database tool
- Nano / Vim (terminal) — text editors
- curl / wget — command-line HTTP
- gcc (self-hosted compilation milestone!)
- Git (command-line)

**Native ImposOS apps:**
- Whatever developers build with your framework
- Your own bundled app suite: editor, calculator, file manager, image viewer, terminal, settings

### After Tier 1.7: What's Next?

At this point ImposOS is a real, usable operating system. Future tiers would include:

- **Tier 1.8:** GPU driver framework (VESA VBE → basic framebuffer GPU → eventually virtio-gpu or simple hardware)
- **Tier 1.9:** Hardware abstraction — USB mass storage, HID, audio codecs, real NIC drivers
- **Tier 2.0:** Self-hosting — compile ImposOS on ImposOS (compiler, assembler, linker all running natively)
- **Tier 2.5:** Modern browser — embed a real layout engine (NetSurf, SerenityOS's Ladybird, or stripped WebKit)
- **Tier 3.0:** SMP (multi-core), 64-bit / long mode, UEFI boot

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
