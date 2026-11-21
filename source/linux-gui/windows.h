/* Linux only: the Windows launcher's window, painting, input, dialogs, processes, threads and
 * the few other Windows calls it makes, on top of SDL3 and POSIX (rr_win32_gui_linux.c).
 * LGPL-2.1-or-later. Original author code.
 *
 * Include order: this folder comes before linux-compat on the include path, so the launcher's
 * <windows.h> is this file, which first pulls in linux-compat/windows.h (settings files, paths,
 * files, shared memory: shared with the game) and then adds the GUI part. The game never sees
 * this folder, so the Linux game build and its build ID are unchanged by it.
 *
 * What is real here: one top-level window per CreateWindowExW (an SDL window that shows the
 * launcher's own 32-bit images), the message loop with timers, painting by BitBlt of
 * DIB sections, keyboard / mouse / wheel input as WM_ messages, message boxes and file pickers
 * (SDL), processes (posix_spawn), threads (pthreads), XInput and waveOut through
 * GetProcAddress (SDL gamepads and audio). What is not: child controls (EDIT, BUTTON, STATIC),
 * COM (WIC, the shell's folder picker), WinHTTP, HID: those report failure, and the launcher
 * code that needs them has a Linux path of its own or is not offered on Linux. */
#ifndef RR_LINUX_GUI_WINDOWS_H
#define RR_LINUX_GUI_WINDOWS_H
#include_next <windows.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#ifdef __cplusplus
extern "C" {
#endif

#define __stdcall
#define __cdecl
#define STDMETHODCALLTYPE
#define PASCAL
#define DECLSPEC_IMPORT

/* ---- Types ------------------------------------------------------------------------------ */
typedef unsigned short USHORT;
typedef short SHORT;
typedef int64_t LONG64;
typedef uint64_t ULONG64;
typedef int32_t HRESULT;
typedef unsigned int MMRESULT;
typedef HANDLE HGLOBAL, HRSRC, HMONITOR, HWAVEOUT, HINTERNET, HDEVINFO, HKEY;
typedef intptr_t (*FARPROC)(void);
typedef wchar_t *BSTR;
typedef short VARIANT_BOOL;
typedef RECT *LPRECT;
typedef HWAVEOUT *LPHWAVEOUT;
typedef union { struct { DWORD LowPart, HighPart; } u; ULONGLONG QuadPart; } ULARGE_INTEGER;
typedef struct { LONG cx, cy; } SIZE;
typedef struct { DWORD Data1; WORD Data2, Data3; BYTE Data4[8]; } GUID;
typedef GUID IID, CLSID;
typedef struct { DWORD cbSize; GUID ClassGuid; DWORD DevInst; ULONG_PTR Reserved; } SP_DEVINFO_DATA, *PSP_DEVINFO_DATA;
typedef const GUID *REFGUID, *REFIID, *REFCLSID;
typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef struct { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG, *LPMSG;
typedef struct {
    UINT style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra; HINSTANCE hInstance; HICON hIcon;
    HCURSOR hCursor; HBRUSH hbrBackground; LPCWSTR lpszMenuName, lpszClassName;
} WNDCLASSW;
typedef struct {
    LPVOID lpCreateParams; HINSTANCE hInstance; HMENU hMenu; HWND hwndParent; int cy, cx, y, x;
    LONG style; LPCWSTR lpszName, lpszClass; DWORD dwExStyle;
} CREATESTRUCTW;
typedef struct { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore, fIncUpdate; BYTE rgbReserved[32]; } PAINTSTRUCT;
typedef struct { POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize; } MINMAXINFO;
typedef struct { DWORD cbSize, dwFlags; HWND hwndTrack; DWORD dwHoverTime; } TRACKMOUSEEVENT;
typedef struct { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; } MONITORINFO;
typedef struct { DWORD nLength; LPVOID lpSecurityDescriptor; BOOL bInheritHandle; } SECURITY_ATTRIBUTES;
typedef struct {
    DWORD cb; LPWSTR lpReserved, lpDesktop, lpTitle; DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars,
    dwYCountChars, dwFillAttribute, dwFlags; WORD wShowWindow, cbReserved2; LPBYTE lpReserved2;
    HANDLE hStdInput, hStdOutput, hStdError;
} STARTUPINFOW;
typedef struct { HANDLE hProcess, hThread; DWORD dwProcessId, dwThreadId; } PROCESS_INFORMATION;
typedef struct {
    DWORD lStructSize; HWND hwndOwner; HINSTANCE hInstance; LPCWSTR lpstrFilter; LPWSTR lpstrCustomFilter;
    DWORD nMaxCustFilter, nFilterIndex; LPWSTR lpstrFile; DWORD nMaxFile; LPWSTR lpstrFileTitle;
    DWORD nMaxFileTitle; LPCWSTR lpstrInitialDir, lpstrTitle; DWORD Flags; WORD nFileOffset, nFileExtension;
    LPCWSTR lpstrDefExt; LPARAM lCustData; void *lpfnHook; LPCWSTR lpTemplateName; void *pvReserved;
    DWORD dwReserved, FlagsEx;
} OPENFILENAMEW;
typedef struct {
    LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight; BYTE lfItalic, lfUnderline, lfStrikeOut,
    lfCharSet, lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily; WCHAR lfFaceName[32];
} LOGFONTW;
typedef struct { LONG tmHeight, tmAscent, tmDescent; } TEXTMETRICW;
typedef int (CALLBACK *FONTENUMPROCW)(const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM);
typedef struct { DWORD dwOSVersionInfoSize, dwMajorVersion, dwMinorVersion, dwBuildNumber, dwPlatformId; WCHAR szCSDVersion[128]; } OSVERSIONINFOW;
typedef struct { WORD wFormatTag, nChannels; DWORD nSamplesPerSec, nAvgBytesPerSec; WORD nBlockAlign, wBitsPerSample, cbSize; } WAVEFORMATEX;
typedef struct wavehdr_tag {
    LPSTR lpData; DWORD dwBufferLength, dwBytesRecorded; DWORD_PTR dwUser; DWORD dwFlags, dwLoops;
    struct wavehdr_tag *lpNext; DWORD_PTR reserved;
} WAVEHDR, *LPWAVEHDR;
typedef const WAVEFORMATEX *LPCWAVEFORMATEX;
typedef struct { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XINPUT_GAMEPAD;
typedef struct { DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad; } XINPUT_STATE;
typedef struct { DWORD cbSize; GUID InterfaceClassGuid; DWORD Flags; ULONG_PTR Reserved; } SP_DEVICE_INTERFACE_DATA, *PSP_DEVICE_INTERFACE_DATA;
typedef struct { DWORD cbSize; WCHAR DevicePath[1]; } SP_DEVICE_INTERFACE_DETAIL_DATA_W, *PSP_DEVICE_INTERFACE_DETAIL_DATA_W;
typedef struct {
    DWORD dwStructSize; LPWSTR lpszScheme; DWORD dwSchemeLength; int nScheme; LPWSTR lpszHostName;
    DWORD dwHostNameLength; WORD nPort; LPWSTR lpszUserName; DWORD dwUserNameLength; LPWSTR lpszPassword;
    DWORD dwPasswordLength; LPWSTR lpszUrlPath; DWORD dwUrlPathLength; LPWSTR lpszExtraInfo; DWORD dwExtraInfoLength;
} URL_COMPONENTS;
typedef int SOCKET;
typedef struct { WORD wVersion, wHighVersion; char szDescription[257], szSystemStatus[129]; unsigned short iMaxSockets, iMaxUdpDg; char *lpVendorInfo; } WSADATA;

/* COM objects the launcher can ask for: opaque here; creating one fails (see CoCreateInstance). */
typedef struct IUnknown IUnknown;
typedef struct IStream IStream;
typedef struct IWICImagingFactory IWICImagingFactory;
typedef struct IWICStream IWICStream;
typedef struct IWICBitmapDecoder IWICBitmapDecoder;
typedef struct IWICBitmapFrameDecode IWICBitmapFrameDecode;
typedef struct IWICFormatConverter IWICFormatConverter;
typedef struct IWICBitmapSource IWICBitmapSource;
typedef struct IWICBitmapEncoder IWICBitmapEncoder;
typedef struct IWICBitmapFrameEncode IWICBitmapFrameEncode;
typedef struct IPropertyBag2 IPropertyBag2;
typedef struct IFileOpenDialog IFileOpenDialog;
typedef struct IShellItem IShellItem;
typedef GUID WICPixelFormatGUID;
typedef DWORD FILEOPENDIALOGOPTIONS;
typedef int WICDecodeOptions, WICBitmapDitherType, WICBitmapPaletteType, WICBitmapEncoderCacheOption, SIGDN;

/* ---- Constants -------------------------------------------------------------------------- */
#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_FAIL ((HRESULT)0x80004005)
#define E_NOTIMPL ((HRESULT)0x80004001)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define VARIANT_TRUE ((short)-1)
#define COINIT_APARTMENTTHREADED 0x2
#define COINIT_MULTITHREADED 0x0
#define CLSCTX_INPROC_SERVER 0x1
#define WICDecodeMetadataCacheOnLoad 1
#define WICBitmapDitherTypeNone 0
#define WICBitmapPaletteTypeCustom 0
#define WICBitmapEncoderNoCache 2
#define FOS_PICKFOLDERS 0x20
#define FOS_FORCEFILESYSTEM 0x40
#define FOS_PATHMUSTEXIST 0x800
#define SIGDN_FILESYSPATH ((SIGDN)0x80058000)
extern const GUID CLSID_WICImagingFactory, IID_IWICImagingFactory, GUID_WICPixelFormat32bppPBGRA,
    GUID_WICPixelFormat32bppBGRA, GUID_WICPixelFormat32bppBGR, GUID_ContainerFormatPng, CLSID_FileOpenDialog, IID_IFileOpenDialog;

#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_SIZE 0x0005
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_ENABLE 0x000A
#define WM_SETTEXT 0x000C
#define WM_GETTEXT 0x000D
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SETCURSOR 0x0020
#define WM_GETMINMAXINFO 0x0024
#define WM_SETFONT 0x0030
#define WM_NCCREATE 0x0081
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_COMMAND 0x0111
#define WM_TIMER 0x0113
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_RBUTTONDOWN 0x0204
#define WM_MOUSEWHEEL 0x020A
#define WM_XBUTTONDOWN 0x020B
#define WM_DROPFILES 0x0233
#define WM_MOUSELEAVE 0x02A3
#define WM_DPICHANGED 0x02E0
#define WM_APP 0x8000
#define WM_USER 0x0400
#define HTCLIENT 1
#define XBUTTON1 0x0001
#define XBUTTON2 0x0002
#define WHEEL_DELTA 120
#define TME_LEAVE 0x2
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_RESTORE 9
#define SW_SHOWDEFAULT 10
#define SWP_NOSIZE 0x1
#define SWP_NOMOVE 0x2
#define SWP_NOZORDER 0x4
#define SWP_NOACTIVATE 0x10
#define CW_USEDEFAULT ((int)0x80000000)
#define WS_OVERLAPPED 0x00000000L
#define WS_CAPTION 0x00C00000L
#define WS_SYSMENU 0x00080000L
#define WS_THICKFRAME 0x00040000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_MAXIMIZEBOX 0x00010000L
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_CHILD 0x40000000L
#define WS_VISIBLE 0x10000000L
#define WS_TABSTOP 0x00010000L
#define WS_VSCROLL 0x00200000L
#define WS_POPUP 0x80000000L
#define WS_EX_DLGMODALFRAME 0x1
#define WS_EX_CLIENTEDGE 0x200
#define ES_MULTILINE 0x4
#define ES_AUTOVSCROLL 0x40
#define ES_AUTOHSCROLL 0x80
#define ES_WANTRETURN 0x1000
#define BS_DEFPUSHBUTTON 0x1
#define EM_SETSEL 0x00B1
#define EM_LIMITTEXT 0x00C5
#define GWLP_USERDATA (-21)
#define COLOR_WINDOW 5
#define IDOK 1
#define IDCANCEL 2
#define IDABORT 3
#define IDRETRY 4
#define IDIGNORE 5
#define IDYES 6
#define IDNO 7
#define MB_OK 0x0
#define MB_OKCANCEL 0x1
#define MB_YESNOCANCEL 0x3
#define MB_YESNO 0x4
#define MB_RETRYCANCEL 0x5
#define MB_ICONERROR 0x10
#define MB_ICONQUESTION 0x20
#define MB_ICONWARNING 0x30
#define MB_ICONINFORMATION 0x40
#define MB_DEFBUTTON2 0x100
#define MB_TOPMOST 0x40000
#define IDC_ARROW ((LPCWSTR)(uintptr_t)32512)
#define IDC_HAND ((LPCWSTR)(uintptr_t)32649)
#define IDC_WAIT ((LPCWSTR)(uintptr_t)32514)
#define IDI_APPLICATION ((LPCWSTR)(uintptr_t)32512)
#define RT_RCDATA ((LPCWSTR)(uintptr_t)10)
#define SRCCOPY 0x00CC0020
#define DIB_RGB_COLORS 0
#define TRANSPARENT 1
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define FW_NORMAL 400
#define FW_SEMIBOLD 600
#define FW_BOLD 700
#define DEFAULT_CHARSET 1
#define OUT_TT_PRECIS 4
#define CLIP_DEFAULT_PRECIS 0
#define CLEARTYPE_QUALITY 5
#define ANTIALIASED_QUALITY 4
#define DEFAULT_PITCH 0
#define FIXED_PITCH 1
#define FF_SWISS 0x20
#define FF_MODERN 0x30
#define DT_TOP 0x0
#define DT_LEFT 0x0
#define DT_CENTER 0x1
#define DT_RIGHT 0x2
#define DT_VCENTER 0x4
#define DT_BOTTOM 0x8
#define DT_WORDBREAK 0x10
#define DT_SINGLELINE 0x20
#define DT_CALCRECT 0x400
#define DT_NOPREFIX 0x800
#define DT_EDITCONTROL 0x2000
#define DT_END_ELLIPSIS 0x8000
#define OFN_OVERWRITEPROMPT 0x2
#define OFN_NOCHANGEDIR 0x8
#define OFN_PATHMUSTEXIST 0x800
#define OFN_FILEMUSTEXIST 0x1000
#define STILL_ACTIVE 259
#define STARTF_USESTDHANDLES 0x100
#define CREATE_NO_WINDOW 0x08000000
#define CREATE_UNICODE_ENVIRONMENT 0x00000400
#define DETACHED_PROCESS 0x8
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#define SYNCHRONIZE 0x00100000
#define PROCESS_TERMINATE 0x1
#define STD_INPUT_HANDLE ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE ((DWORD)-12)
#define FILE_TYPE_UNKNOWN 0
#define FILE_TYPE_DISK 1
#define FILE_TYPE_PIPE 3
#define ATTACH_PARENT_PROCESS ((DWORD)-1)
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define FILE_ATTRIBUTE_HIDDEN 0x2
#define FILE_ATTRIBUTE_SYSTEM 0x4
#define FILE_ATTRIBUTE_ARCHIVE 0x20
#define FILE_ATTRIBUTE_TEMPORARY 0x100
#define FILE_ATTRIBUTE_REPARSE_POINT 0x400
#define FILE_FLAG_OVERLAPPED 0x40000000
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000
#define FILE_FLAG_WRITE_THROUGH 0x80000000
#define CREATE_NEW 1
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define ERROR_IO_INCOMPLETE 996
#define ERROR_INVALID_HANDLE 6
#define ERROR_NO_MORE_FILES 18
#define ERROR_DEVICE_NOT_CONNECTED 1167
#define PROGRESS_CONTINUE 0
#define PROGRESS_CANCEL 1
#define COPY_FILE_FAIL_IF_EXISTS 0x1
#define PAGE_READONLY 0x02
#define CSIDL_PERSONAL 0x0005
#define CSIDL_LOCAL_APPDATA 0x001c
#define SHGFP_TYPE_CURRENT 0
#define MONITOR_DEFAULTTONEAREST 2
#define MONITOR_DEFAULTTOPRIMARY 1
#define SPI_GETCLIENTAREAANIMATION 0x1042
#define SPI_GETWORKAREA 0x0030
#define VER_PLATFORM_WIN32_NT 2
#define CF_UNICODETEXT 13
#define GMEM_MOVEABLE 0x2
#define CALLBACK_NULL 0
#define WAVE_MAPPER ((UINT)-1)
#define WAVE_FORMAT_PCM 1
#define MMSYSERR_NOERROR 0
#define MMSYSERR_ERROR 1
#define WHDR_DONE 0x1
#define WHDR_PREPARED 0x2
#define WHDR_INQUEUE 0x10
#define ERROR_DEVICE_NOT_CONNECTED 1167
#define XINPUT_GAMEPAD_DPAD_UP 0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN 0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT 0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT 0x0008
#define XINPUT_GAMEPAD_START 0x0010
#define XINPUT_GAMEPAD_BACK 0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB 0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB 0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER 0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200
#define XINPUT_GAMEPAD_A 0x1000
#define XINPUT_GAMEPAD_B 0x2000
#define XINPUT_GAMEPAD_X 0x4000
#define XINPUT_GAMEPAD_Y 0x8000
#define INTERNET_DEFAULT_HTTPS_PORT 443
#define INTERNET_DEFAULT_HTTP_PORT 80
#define INTERNET_SCHEME_HTTP 1
#define INTERNET_SCHEME_HTTPS 2
#define WINHTTP_ACCESS_TYPE_DEFAULT_PROXY 0
#define WINHTTP_ACCESS_TYPE_NO_PROXY 1
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#define WINHTTP_NO_PROXY_NAME NULL
#define WINHTTP_NO_PROXY_BYPASS NULL
#define WINHTTP_NO_REFERER NULL
#define WINHTTP_DEFAULT_ACCEPT_TYPES NULL
#define WINHTTP_NO_ADDITIONAL_HEADERS NULL
#define WINHTTP_NO_REQUEST_DATA NULL
#define WINHTTP_HEADER_NAME_BY_INDEX NULL
#define WINHTTP_NO_HEADER_INDEX NULL
#define WINHTTP_FLAG_SECURE 0x00800000
#define WINHTTP_QUERY_STATUS_CODE 19
#define WINHTTP_QUERY_FLAG_NUMBER 0x20000000
#define WINHTTP_OPTION_REDIRECT_POLICY 88
#define DIGCF_PRESENT 0x2
#define DIGCF_DEVICEINTERFACE 0x10
/* More virtual keys (linux-compat has the navigation keys). */
#define VK_LBUTTON 0x01
#define VK_CLEAR 0x0C
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_INSERT 0x2D
#define VK_LWIN 0x5B
#define VK_NUMPAD0 0x60
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F10 0x79
#define VK_F12 0x7B
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define VK_OEM_8 0xDF
#define VK_OEM_102 0xE2
/* Sockets: POSIX underneath. */
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define WSAEWOULDBLOCK EWOULDBLOCK
#define WSAECONNRESET ECONNRESET
#define WSAETIMEDOUT ETIMEDOUT
#define closesocket close
#define ioctlsocket(s, cmd, argp) ioctl((s), (cmd), (argp))
#define WSAStartup(v, d) ((void)(v), (void)(d), 0)
#define WSACleanup() (0)
#define WSAGetLastError() (errno)
#define SD_BOTH SHUT_RDWR
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))

/* ---- Macros ----------------------------------------------------------------------------- */
#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)((WORD)(l) | ((DWORD)((WORD)(h))) << 16))
#define MAKEWPARAM(l, h) ((WPARAM)(DWORD)((WORD)(l) | ((DWORD)((WORD)(h))) << 16))
#define GET_WHEEL_DELTA_WPARAM(wp) ((short)HIWORD(wp))
#define GET_XBUTTON_WPARAM(wp) (HIWORD(wp))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define InterlockedExchange(p, v) __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedIncrement(p) __atomic_add_fetch((p), 1, __ATOMIC_SEQ_CST)
#define InterlockedDecrement(p) __atomic_sub_fetch((p), 1, __ATOMIC_SEQ_CST)
#define InterlockedExchangeAdd(p, v) __atomic_fetch_add((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedExchangeAdd64(p, v) __atomic_fetch_add((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedCompareExchange(p, x, c) __sync_val_compare_and_swap((p), (c), (x))
#define _wcsdup wcsdup
#define _wcsupr(s) rr_wcsupr(s)
#define _wtof(s) wcstod((s), NULL)
#define _atoi64(s) strtoll((s), NULL, 10)
#define _strtoui64 strtoull
#define _wcstoui64 wcstoull
#define _wcstoi64 wcstoll
#define _wsplitpath_s rr_unused
#define CoTaskMemFree free
#define SysAllocString wcsdup
#define SysFreeString free
#define LocalFree(p) (free(p), (HANDLE)NULL)

/* ---- Windows and messages (rr_win32_gui_linux.c) ---------------------------------------- */
WORD RegisterClassW(const WNDCLASSW *wc);
HWND CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param);
#define CreateWindowW(cls, title, style, x, y, w, h, parent, menu, inst, param) CreateWindowExW(0, cls, title, style, x, y, w, h, parent, menu, inst, param)
BOOL DestroyWindow(HWND w);
BOOL IsWindow(HWND w);
BOOL ShowWindow(HWND w, int cmd);
BOOL UpdateWindow(HWND w);
BOOL InvalidateRect(HWND w, const RECT *r, BOOL erase);
BOOL IsWindowVisible(HWND w);
BOOL IsIconic(HWND w);
BOOL EnableWindow(HWND w, BOOL on);
HWND GetForegroundWindow(void);
BOOL SetForegroundWindow(HWND w);
HWND SetFocus(HWND w);
HWND GetFocus(void);
HWND FindWindowW(LPCWSTR cls, LPCWSTR title);
BOOL GetClientRect(HWND w, RECT *r);
BOOL GetWindowRect(HWND w, RECT *r);
BOOL SetWindowPos(HWND w, HWND after, int x, int y, int cx, int cy, UINT flags);
BOOL AdjustWindowRectEx(RECT *r, DWORD style, BOOL menu, DWORD ex);
LONG_PTR SetWindowLongPtrW(HWND w, int index, LONG_PTR value);
LONG_PTR GetWindowLongPtrW(HWND w, int index);
int GetWindowTextW(HWND w, LPWSTR out, int cap);
BOOL SetWindowTextW(HWND w, LPCWSTR text);
HWND GetNextDlgTabItem(HWND dlg, HWND ctl, BOOL previous);
LRESULT DefWindowProcW(HWND w, UINT msg, WPARAM wp, LPARAM lp);
LRESULT SendMessageW(HWND w, UINT msg, WPARAM wp, LPARAM lp);
BOOL PostMessageW(HWND w, UINT msg, WPARAM wp, LPARAM lp);
void PostQuitMessage(int code);
BOOL GetMessageW(MSG *m, HWND w, UINT lo, UINT hi);
BOOL PeekMessageW(MSG *m, HWND w, UINT lo, UINT hi, UINT remove);
BOOL TranslateMessage(const MSG *m);
LRESULT DispatchMessageW(const MSG *m);
UINT_PTR SetTimer(HWND w, UINT_PTR id, UINT ms, void *proc);
BOOL KillTimer(HWND w, UINT_PTR id);
SHORT GetKeyState(int vk);
SHORT GetAsyncKeyState(int vk);
BOOL TrackMouseEvent(TRACKMOUSEEVENT *t);
BOOL GetCursorPos(POINT *p);
BOOL ScreenToClient(HWND w, POINT *p);
HCURSOR LoadCursorW(HINSTANCE inst, LPCWSTR name);
HCURSOR SetCursor(HCURSOR c);
HICON LoadIconW(HINSTANCE inst, LPCWSTR name);
HMONITOR MonitorFromWindow(HWND w, DWORD flags);
BOOL GetMonitorInfoW(HMONITOR m, MONITORINFO *mi);
BOOL SystemParametersInfoW(UINT action, UINT param, void *out, UINT ini);
BOOL SetProcessDPIAware(void);
int MulDiv(int a, int b, int c);
int MessageBoxW(HWND owner, LPCWSTR text, LPCWSTR title, UINT type);
BOOL GetOpenFileNameW(OPENFILENAMEW *o);
BOOL GetSaveFileNameW(OPENFILENAMEW *o);
/* ---- Painting: 32-bit DIB sections and the window -------------------------------------- */
HDC GetDC(HWND w);
int ReleaseDC(HWND w, HDC dc);
HDC BeginPaint(HWND w, PAINTSTRUCT *ps);
BOOL EndPaint(HWND w, const PAINTSTRUCT *ps);
HDC CreateCompatibleDC(HDC dc);
BOOL DeleteDC(HDC dc);
HBITMAP CreateDIBSection(HDC dc, const BITMAPINFO *bi, UINT usage, void **bits, HANDLE section, DWORD offset);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ obj);
BOOL DeleteObject(HGDIOBJ obj);
BOOL BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop);
BOOL GdiFlush(void);
int GetDeviceCaps(HDC dc, int index);
HFONT CreateFontW(int h, int w, int esc, int orient, int weight, DWORD italic, DWORD underline, DWORD strike, DWORD charset, DWORD out, DWORD clip, DWORD quality, DWORD pitch, LPCWSTR face);
int SetBkMode(HDC dc, int mode);
COLORREF SetTextColor(HDC dc, COLORREF c);
int DrawTextW(HDC dc, LPCWSTR text, int n, RECT *r, UINT format);
int EnumFontFamiliesExW(HDC dc, LOGFONTW *lf, FONTENUMPROCW proc, LPARAM lp, DWORD flags);
/* ---- Resources (none on Linux: files beside the launcher instead) ---------------------- */
HRSRC FindResourceW(HMODULE m, LPCWSTR name, LPCWSTR type);
DWORD SizeofResource(HMODULE m, HRSRC r);
HGLOBAL LoadResource(HMODULE m, HRSRC r);
LPVOID LockResource(HGLOBAL g);
HMODULE LoadLibraryW(LPCWSTR name);
BOOL FreeLibrary(HMODULE m);
/* ---- Processes and threads ------------------------------------------------------------- */
BOOL CreateProcessW(LPCWSTR app, LPWSTR cmd, SECURITY_ATTRIBUTES *pa, SECURITY_ATTRIBUTES *ta, BOOL inherit, DWORD flags, LPVOID env, LPCWSTR cwd, STARTUPINFOW *si, PROCESS_INFORMATION *pi);
BOOL GetExitCodeProcess(HANDLE p, DWORD *code);
BOOL TerminateProcess(HANDLE p, UINT code);
DWORD GetProcessId(HANDLE p);
HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD pid);
BOOL QueryFullProcessImageNameW(HANDLE p, DWORD flags, LPWSTR out, DWORD *size);
DWORD WaitForSingleObject(HANDLE h, DWORD ms);
uintptr_t _beginthreadex(void *security, unsigned stack, unsigned (*start)(void *), void *arg, unsigned flags, unsigned *id);
HANDLE CreateThread(void *security, SIZE_T stack, LPTHREAD_START_ROUTINE start, LPVOID arg, DWORD flags, DWORD *id);
BOOL GetExitCodeThread(HANDLE t, DWORD *code);
HANDLE CreateEventW(void *security, BOOL manual, BOOL initial, LPCWSTR name);
BOOL SetEvent(HANDLE e);
BOOL ResetEvent(HANDLE e);
LPWSTR GetEnvironmentStringsW(void);
BOOL FreeEnvironmentStringsW(LPWSTR env);
DWORD GetEnvironmentVariableW(LPCWSTR name, LPWSTR out, DWORD size);
BOOL SetEnvironmentVariableW(LPCWSTR name, LPCWSTR value);
LPWSTR *CommandLineToArgvW(LPCWSTR line, int *argc);
HANDLE GetStdHandle(DWORD which);
DWORD GetFileType(HANDLE h);
BOOL AttachConsole(DWORD pid);
BOOL FreeConsole(void);
HINSTANCE ShellExecuteW(HWND w, LPCWSTR op, LPCWSTR file, LPCWSTR params, LPCWSTR dir, int show);
/* ---- Files and paths ------------------------------------------------------------------- */
BOOL SetFileAttributesW(LPCWSTR path, DWORD attributes);
BOOL GetFileSizeEx(HANDLE f, LARGE_INTEGER *size);
BOOL SetFilePointerEx(HANDLE f, LARGE_INTEGER to, LARGE_INTEGER *now, DWORD how);
BOOL GetDiskFreeSpaceExW(LPCWSTR path, ULARGE_INTEGER *avail, ULARGE_INTEGER *total, ULARGE_INTEGER *free_);
BOOL GetVolumePathNameW(LPCWSTR path, LPWSTR out, DWORD cap);
typedef DWORD (CALLBACK *LPPROGRESS_ROUTINE)(LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE, LPVOID);
BOOL CopyFileExW(LPCWSTR from, LPCWSTR to, LPPROGRESS_ROUTINE progress, LPVOID data, BOOL *cancel, DWORD flags);
LONG CompareFileTime(const FILETIME *a, const FILETIME *b);
BOOL FileTimeToLocalFileTime(const FILETIME *in, FILETIME *out);
BOOL FileTimeToSystemTime(const FILETIME *in, SYSTEMTIME *out);
int SHCreateDirectoryExW(HWND w, LPCWSTR path, const SECURITY_ATTRIBUTES *sa);
HRESULT SHGetFolderPathW(HWND w, int csidl, HANDLE token, DWORD flags, LPWSTR out);
BOOL PathRemoveFileSpecW(LPWSTR path);
LPWSTR PathFindFileNameW(LPCWSTR path);
LPWSTR PathFindExtensionW(LPCWSTR path);
BOOL PathFileExistsW(LPCWSTR path);
BOOL PathIsRootW(LPCWSTR path);
BOOL PathCompactPathExW(LPWSTR out, LPCWSTR in, UINT max, DWORD flags);
BOOL CancelIo(HANDLE f);
BOOL GetOverlappedResult(HANDLE f, OVERLAPPED *o, DWORD *done, BOOL wait);
wchar_t *rr_wcsupr(wchar_t *s);
/* ---- Clipboard: the launcher's own (SDL) ------------------------------------------------- */
BOOL OpenClipboard(HWND w);
BOOL CloseClipboard(void);
BOOL EmptyClipboard(void);
HANDLE SetClipboardData(UINT format, HANDLE data);
HGLOBAL GlobalAlloc(UINT flags, SIZE_T size);
LPVOID GlobalLock(HGLOBAL g);
BOOL GlobalUnlock(HGLOBAL g);
HGLOBAL GlobalFree(HGLOBAL g);
/* ---- COM, WinHTTP, HID: report failure (Linux paths are elsewhere) ---------------------- */
HRESULT CoInitializeEx(LPVOID reserved, DWORD flags);
void CoUninitialize(void);
HRESULT CoCreateInstance(REFCLSID cls, IUnknown *outer, DWORD ctx, REFIID iid, void **out);
#define RR_COM_FAIL(...) ((HRESULT)E_NOTIMPL)
#define RR_COM_NOP(...) ((ULONG)0)
#define IWICImagingFactory_CreateStream RR_COM_FAIL
#define IWICImagingFactory_CreateDecoderFromStream RR_COM_FAIL
#define IWICImagingFactory_CreateFormatConverter RR_COM_FAIL
#define IWICImagingFactory_CreateEncoder RR_COM_FAIL
#define IWICImagingFactory_Release RR_COM_NOP
#define IWICStream_InitializeFromMemory RR_COM_FAIL
#define IWICStream_InitializeFromFilename RR_COM_FAIL
#define IWICStream_Release RR_COM_NOP
#define IWICBitmapDecoder_GetFrame RR_COM_FAIL
#define IWICBitmapDecoder_Release RR_COM_NOP
#define IWICBitmapFrameDecode_GetSize RR_COM_FAIL
#define IWICBitmapFrameDecode_Release RR_COM_NOP
#define IWICFormatConverter_Initialize RR_COM_FAIL
#define IWICFormatConverter_CopyPixels RR_COM_FAIL
#define IWICFormatConverter_Release RR_COM_NOP
#define IWICBitmapEncoder_Initialize RR_COM_FAIL
#define IWICBitmapEncoder_CreateNewFrame RR_COM_FAIL
#define IWICBitmapEncoder_Commit RR_COM_FAIL
#define IWICBitmapEncoder_Release RR_COM_NOP
#define IWICBitmapFrameEncode_Initialize RR_COM_FAIL
#define IWICBitmapFrameEncode_SetSize RR_COM_FAIL
#define IWICBitmapFrameEncode_SetPixelFormat RR_COM_FAIL
#define IWICBitmapFrameEncode_WritePixels RR_COM_FAIL
#define IWICBitmapFrameEncode_Commit RR_COM_FAIL
#define IWICBitmapFrameEncode_Release RR_COM_NOP
#define IFileOpenDialog_GetOptions RR_COM_FAIL
#define IFileOpenDialog_SetOptions RR_COM_FAIL
#define IFileOpenDialog_SetTitle RR_COM_FAIL
#define IFileOpenDialog_Show RR_COM_FAIL
#define IFileOpenDialog_GetResult RR_COM_FAIL
#define IFileOpenDialog_Release RR_COM_NOP
#define IShellItem_GetDisplayName RR_COM_FAIL
#define IShellItem_Release RR_COM_NOP
BOOL WinHttpCrackUrl(LPCWSTR url, DWORD len, DWORD flags, URL_COMPONENTS *u);
HINTERNET WinHttpOpen(LPCWSTR agent, DWORD access, LPCWSTR proxy, LPCWSTR bypass, DWORD flags);
HINTERNET WinHttpConnect(HINTERNET s, LPCWSTR host, WORD port, DWORD reserved);
HINTERNET WinHttpOpenRequest(HINTERNET c, LPCWSTR verb, LPCWSTR path, LPCWSTR version, LPCWSTR referrer, LPCWSTR *types, DWORD flags);
BOOL WinHttpSetTimeouts(HINTERNET h, int resolve, int connect, int send, int receive);
BOOL WinHttpSetOption(HINTERNET h, DWORD option, LPVOID buffer, DWORD len);
BOOL WinHttpAddRequestHeaders(HINTERNET r, LPCWSTR headers, DWORD len, DWORD modifiers);
BOOL WinHttpSendRequest(HINTERNET r, LPCWSTR headers, DWORD len, LPVOID data, DWORD data_len, DWORD total, DWORD_PTR context);
BOOL WinHttpReceiveResponse(HINTERNET r, LPVOID reserved);
BOOL WinHttpQueryHeaders(HINTERNET r, DWORD level, LPCWSTR name, LPVOID buffer, DWORD *len, DWORD *index);
BOOL WinHttpReadData(HINTERNET r, LPVOID buffer, DWORD size, DWORD *read);
BOOL WinHttpCloseHandle(HINTERNET h);

/* Linux-only helpers for the launcher (rr_win32_gui_linux.c). */
/* A text box over the launcher window, drawn in the launcher's own look: the stand-in for the
 * Windows EDIT dialogs (rider names, custom cheat codes, a report's note). 1 = OK, 0 = cancel. */
int rr_linux_prompt(HWND owner, const wchar_t *title, const wchar_t *caption, wchar_t *text, size_t cap, int max_chars, int multiline);
/* A PNG file (8-bit RGB/RGBA, any filter) as premultiplied 32-bit BGRA, top-down. */
uint32_t *rr_linux_load_png(const wchar_t *path, unsigned *w, unsigned *h);
/* A file dropped on the launcher window since the last call (NULL: none); the caller frees it. */
wchar_t *rr_linux_take_dropped_file(void);
/* The desktop clipboard's text (NULL: none); the caller frees it. */
wchar_t *rr_linux_clipboard_text(void);

#ifdef __cplusplus
}
#endif
#endif
