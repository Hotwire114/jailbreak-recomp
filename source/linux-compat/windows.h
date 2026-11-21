/* Linux only: the small part of the Windows API the game's PC code uses (settings files,
 * paths, files, timers, SHA-256, shared memory), on top of POSIX. LGPL-2.1-or-later.
 * Original author code. This folder is on the include path only for Linux builds, so the
 * Windows build never sees it and keeps the real <windows.h>.
 *
 * Paths: wchar_t is 32-bit UTF-32 here; every path is turned into UTF-8 at the call.
 * The PC code builds paths Windows style ("dir\\file"), so these functions accept '\\' and
 * '/' alike, and the paths they hand back (GetModuleFileNameW, GetFullPathNameW) use '\\'
 * as the separator. "\\home\\me\\Games\\x" is "/home/me/Games/x". */
#ifndef RR_LINUX_WINDOWS_H
#define RR_LINUX_WINDOWS_H
#ifdef _WIN32
#error "linux-compat/windows.h is for Linux builds only"
#endif
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>
#include <wctype.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef int BOOL;
typedef unsigned char BYTE, UCHAR, BOOLEAN;
typedef BYTE *PBYTE, *PUCHAR, *LPBYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD, ULONG, UINT;
typedef DWORD *LPDWORD, *PDWORD;
typedef ULONG *PULONG;
typedef int32_t LONG, NTSTATUS, INT;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG, DWORD64;
typedef uintptr_t DWORD_PTR, UINT_PTR, ULONG_PTR, WPARAM, SIZE_T;
typedef intptr_t INT_PTR, LONG_PTR, LPARAM, LRESULT;
typedef wchar_t WCHAR;
typedef WCHAR *LPWSTR, *PWSTR;
typedef const WCHAR *LPCWSTR, *PCWSTR;
typedef char CHAR, *LPSTR;
typedef const char *LPCSTR;
typedef void VOID, *LPVOID, *PVOID, *HANDLE;
typedef const void *LPCVOID;
typedef HANDLE HWND, HMODULE, HINSTANCE, HICON, HDC, HBITMAP, HGDIOBJ, HFONT, HBRUSH, HMENU, HCURSOR;
typedef DWORD COLORREF;
typedef HANDLE BCRYPT_ALG_HANDLE, BCRYPT_HASH_HANDLE;
typedef union { struct { DWORD LowPart; LONG HighPart; } u; LONGLONG QuadPart; } LARGE_INTEGER;

#define WINAPI
#define APIENTRY
#define CALLBACK
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFFu
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define FILE_ATTRIBUTE_READONLY 0x1
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL 0x80
#define CP_ACP 0
#define CP_UTF8 65001
#define MOVEFILE_REPLACE_EXISTING 0x1
#define MOVEFILE_COPY_ALLOWED 0x2
#define MOVEFILE_WRITE_THROUGH 0x8
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define WAIT_FAILED 0xFFFFFFFFu
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_ALREADY_EXISTS 183
#define ERROR_IO_PENDING 997
#define FILE_MAP_WRITE 0x2
#define FILE_MAP_READ 0x4
#define FILE_MAP_ALL_ACCESS 0xF001F
#define PAGE_READWRITE 0x04
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x1
#define FILE_SHARE_WRITE 0x2
#define FILE_SHARE_DELETE 0x4
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define BCRYPT_SHA256_ALGORITHM L"SHA256"
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG 0x2
#define BCRYPT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(c) ((BYTE)(c))
#define GetGValue(c) ((BYTE)((c) >> 8))
#define GetBValue(c) ((BYTE)((c) >> 16))
#define MAKEINTRESOURCEW(i) ((LPWSTR)(uintptr_t)(WORD)(i))
#define MemoryBarrier() __sync_synchronize()
#define UNREFERENCED_PARAMETER(p) (void)(p)
/* Virtual-key codes (the in-game Online page and the launcher pass keys this way). */
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_DELETE 0x2E

typedef struct { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;
typedef struct { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME;
typedef struct { LONG x, y; } POINT;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct {
    DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA;
typedef enum { GetFileExInfoStandard } GET_FILEEX_INFO_LEVELS;
typedef struct {
    DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
    WCHAR cFileName[MAX_PATH]; WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW;
typedef struct { ULONG_PTR Internal, InternalHigh; DWORD Offset, OffsetHigh; HANDLE hEvent; } OVERLAPPED;
typedef DWORD(WINAPI *LPTHREAD_START_ROUTINE)(LPVOID);
#pragma pack(push, 2)
typedef struct { WORD bfType; DWORD bfSize; WORD bfReserved1, bfReserved2; DWORD bfOffBits; } BITMAPFILEHEADER;
#pragma pack(pop)
typedef struct {
    DWORD biSize; LONG biWidth, biHeight; WORD biPlanes, biBitCount; DWORD biCompression, biSizeImage;
    LONG biXPelsPerMeter, biYPelsPerMeter; DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER;
typedef struct { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;
typedef struct { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; } BITMAPINFO;
#define BI_RGB 0

/* Settings files (INI). Same rules as Windows: names are not case sensitive, spaces round
 * keys and values are dropped, a value in matching quotes loses them, a NULL section or key
 * lists names, a NULL key or value deletes. Files are UTF-8 (a UTF-8 or UTF-16 BOM is read). */
DWORD GetPrivateProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR def, LPWSTR out, DWORD size, LPCWSTR path);
UINT GetPrivateProfileIntW(LPCWSTR section, LPCWSTR key, INT def, LPCWSTR path);
DWORD GetPrivateProfileSectionW(LPCWSTR section, LPWSTR out, DWORD size, LPCWSTR path);
BOOL WritePrivateProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR path);

/* Paths and files. */
DWORD GetModuleFileNameW(HMODULE module, LPWSTR out, DWORD size);
HMODULE GetModuleHandleW(LPCWSTR name);
void *GetProcAddress(HMODULE module, const char *name); /* always NULL: no Windows extras */
DWORD GetFullPathNameW(LPCWSTR in, DWORD size, LPWSTR out, LPWSTR *file_part);
DWORD GetFileAttributesW(LPCWSTR path);
BOOL GetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS level, void *out);
BOOL CreateDirectoryW(LPCWSTR path, void *security);
BOOL RemoveDirectoryW(LPCWSTR path);
BOOL DeleteFileW(LPCWSTR path);
BOOL MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags);
BOOL MoveFileW(LPCWSTR from, LPCWSTR to);
BOOL CopyFileW(LPCWSTR from, LPCWSTR to, BOOL fail_if_exists);
HANDLE FindFirstFileW(LPCWSTR pattern, WIN32_FIND_DATAW *out);
BOOL FindNextFileW(HANDLE find, WIN32_FIND_DATAW *out);
BOOL FindClose(HANDLE find);
DWORD GetTempPathW(DWORD size, LPWSTR out);
DWORD GetCurrentDirectoryW(DWORD size, LPWSTR out);
BOOL SetCurrentDirectoryW(LPCWSTR path);
FILE *_wfopen(const wchar_t *path, const wchar_t *mode);
int _wremove(const wchar_t *path);
int _wrename(const wchar_t *from, const wchar_t *to);
wchar_t *_wgetenv(const wchar_t *name);
LPWSTR GetCommandLineW(void);
/* Plain files by handle (read whole files, write new ones). */
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, void *security, DWORD disposition, DWORD flags, HANDLE tmpl);
BOOL ReadFile(HANDLE file, void *buffer, DWORD size, DWORD *done, OVERLAPPED *ov);
BOOL WriteFile(HANDLE file, const void *buffer, DWORD size, DWORD *done, OVERLAPPED *ov);
DWORD GetFileSize(HANDLE file, DWORD *high);
BOOL CloseHandle(HANDLE h);

/* Shared memory between the launcher and the game ("Local\\Name" -> /dev/shm/Name). */
HANDLE CreateFileMappingW(HANDLE file, void *security, DWORD protect, DWORD size_high, DWORD size_low, LPCWSTR name);
HANDLE OpenFileMappingW(DWORD access, BOOL inherit, LPCWSTR name);
void *MapViewOfFile(HANDLE mapping, DWORD access, DWORD offset_high, DWORD offset_low, SIZE_T bytes);
BOOL UnmapViewOfFile(const void *view);

/* Time, process, errors. */
ULONGLONG GetTickCount64(void);
DWORD GetTickCount(void);
void Sleep(DWORD ms);
BOOL QueryPerformanceCounter(LARGE_INTEGER *out);
BOOL QueryPerformanceFrequency(LARGE_INTEGER *out);
void GetLocalTime(SYSTEMTIME *out);
void GetSystemTime(SYSTEMTIME *out);
DWORD GetCurrentProcessId(void);
HANDLE GetCurrentProcess(void);
HANDLE GetCurrentThread(void);
BOOL GetProcessAffinityMask(HANDLE process, DWORD_PTR *process_mask, DWORD_PTR *system_mask); /* FALSE */
DWORD GetLastError(void);
void SetLastError(DWORD error);

/* Text. Only UTF-8 (CP_ACP is taken as UTF-8 too). */
int MultiByteToWideChar(UINT cp, DWORD flags, const char *in, int in_len, wchar_t *out, int out_len);
int WideCharToMultiByte(UINT cp, DWORD flags, const wchar_t *in, int in_len, char *out, int out_len, const char *def, BOOL *used_def);
#define _wcsicmp wcscasecmp
#define _wcsnicmp wcsncasecmp
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _wtoi(s) ((int)wcstol((s), NULL, 10))
#define _wtol(s) wcstol((s), NULL, 10)
#define _fseeki64 fseeko
#define _ftelli64 ftello
#define _snwprintf swprintf
#define localtime_s(tm_out, time_in) (localtime_r((time_in), (tm_out)) ? 0 : 1)

/* SHA-256 (the only algorithm the PC code asks for) and random bytes. */
NTSTATUS BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE *alg, LPCWSTR id, LPCWSTR impl, ULONG flags);
NTSTATUS BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE alg, ULONG flags);
NTSTATUS BCryptCreateHash(BCRYPT_ALG_HANDLE alg, BCRYPT_HASH_HANDLE *hash, PUCHAR object, ULONG object_size, PUCHAR secret, ULONG secret_size, ULONG flags);
NTSTATUS BCryptHashData(BCRYPT_HASH_HANDLE hash, PUCHAR data, ULONG size, ULONG flags);
NTSTATUS BCryptFinishHash(BCRYPT_HASH_HANDLE hash, PUCHAR out, ULONG out_size, ULONG flags);
NTSTATUS BCryptDestroyHash(BCRYPT_HASH_HANDLE hash);
NTSTATUS BCryptGenRandom(BCRYPT_ALG_HANDLE alg, PUCHAR out, ULONG size, ULONG flags);

/* Path helper for Linux-only code: a Windows-style wide path as a UTF-8 POSIX path. */
void rr_linux_path(const wchar_t *in, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
