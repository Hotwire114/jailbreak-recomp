/* Sony pads for the launcher: DualShock 4 and DualSense (incl. Edge), over USB
 * and Bluetooth, read as raw HID. They have no XInput, so without this they
 * could not drive the launcher. LGPL-2.1-or-later; independently written from
 * the pads' public input-report layouts. hid.dll and setupapi.dll are loaded
 * at run time (both exist under Wine / Proton too, through winebus), so the
 * launcher links nothing new. Header-only: included by save_browser.h.
 *
 * Input reports used (byte 0 = report id):
 *   0x01 short (DS4 USB and Bluetooth "simple" mode, DualSense Bluetooth
 *        simple mode): 1 LX, 2 LY, 3 RX, 4 RY, 5 hat (low nibble) + Square /
 *        Cross / Circle / Triangle (bits 4-7), 6 L1 R1 L2 R2 Share/Create
 *        Options L3 R3 (bits 0-7), 7 bit 0 PS.
 *   0x01 full (DualSense USB, 64 bytes): 1-4 sticks, 5 L2, 6 R2, 7 counter,
 *        8 hat + face, 9 shoulders and centre buttons (as byte 6 above), 10 PS.
 *   0x11 (DS4 Bluetooth full): the short layout two bytes later (sticks at 3).
 *   0x31 (DualSense Bluetooth full): the DualSense USB layout one byte later.
 */
#ifndef RR_SONY_PAD_H
#define RR_SONY_PAD_H
#include <windows.h>
#include <setupapi.h>
#include <stdlib.h>
#include <string.h>

/* A pad's state in XInput's terms (button bits as XINPUT_GAMEPAD_*), so the
 * launcher's navigation treats every pad alike. Sticks: -32768..32767, up +. */
typedef struct SonyPadState { WORD buttons; short lx, ly; } SonyPadState;
enum { SONY_DPAD_UP = 0x0001, SONY_DPAD_DOWN = 0x0002, SONY_DPAD_LEFT = 0x0004, SONY_DPAD_RIGHT = 0x0008,
       SONY_START = 0x0010, SONY_BACK = 0x0020, SONY_L3 = 0x0040, SONY_R3 = 0x0080,
       SONY_L1 = 0x0100, SONY_R1 = 0x0200, SONY_CROSS = 0x1000, SONY_CIRCLE = 0x2000,
       SONY_SQUARE = 0x4000, SONY_TRIANGLE = 0x8000 };
static int sony_is_dualsense(USHORT pid) { return pid == 0x0CE6 || pid == 0x0DF2; }
static int sony_known(USHORT vid, USHORT pid) {
    return vid == 0x054C && (pid == 0x05C4 || pid == 0x09CC || pid == 0x0BA0 || sony_is_dualsense(pid));
}
static short sony_axis(BYTE v, int invert) { int a = ((int)v - 128) * 256; if (invert) a = -a - 1; return (short)(a < -32768 ? -32768 : a > 32767 ? 32767 : a); }
/* One input report -> state; 0 when it is not a report we read. */
static int sony_parse(const BYTE *r, DWORD n, int dualsense, SonyPadState *out) {
    int at;           /* offset of the stick bytes */
    int face, shoulders;
    if (n < 1) return 0;
    /* Windows hands every read the device's longest input report: a DualSense
     * reads 64 bytes on USB (its full 0x01) and 78 on Bluetooth (0x31, or the
     * 10-byte simple 0x01 padded). */
    if (r[0] == 0x01 && dualsense && n == 64) { at = 1; face = 8; shoulders = 9; }   /* DualSense USB */
    else if (r[0] == 0x01 && n >= 8) { at = 1; face = 5; shoulders = 6; }           /* short reports */
    else if (r[0] == 0x11 && !dualsense && n >= 10) { at = 3; face = 7; shoulders = 8; }   /* DS4 Bluetooth */
    else if (r[0] == 0x31 && dualsense && n >= 12) { at = 2; face = 9; shoulders = 10; }   /* DualSense Bluetooth */
    else return 0;
    static const WORD hat[9] = {SONY_DPAD_UP, SONY_DPAD_UP | SONY_DPAD_RIGHT, SONY_DPAD_RIGHT, SONY_DPAD_RIGHT | SONY_DPAD_DOWN,
                                SONY_DPAD_DOWN, SONY_DPAD_DOWN | SONY_DPAD_LEFT, SONY_DPAD_LEFT, SONY_DPAD_LEFT | SONY_DPAD_UP, 0};
    const BYTE f = r[face], s = r[shoulders];
    WORD b = hat[(f & 15) < 8 ? (f & 15) : 8];
    if (f & 0x10) b |= SONY_SQUARE;
    if (f & 0x20) b |= SONY_CROSS;
    if (f & 0x40) b |= SONY_CIRCLE;
    if (f & 0x80) b |= SONY_TRIANGLE;
    if (s & 0x01) b |= SONY_L1;
    if (s & 0x02) b |= SONY_R1;
    if (s & 0x10) b |= SONY_BACK;    /* Share / Create */
    if (s & 0x20) b |= SONY_START;   /* Options */
    if (s & 0x40) b |= SONY_L3;
    if (s & 0x80) b |= SONY_R3;
    out->buttons = b; out->lx = sony_axis(r[at], 0); out->ly = sony_axis(r[at + 1], 1);
    return 1;
}

#ifndef SONY_PAD_PARSE_ONLY
/* hid.dll / setupapi.dll entry points (declared here: no DDK headers needed). */
typedef struct { ULONG Size; USHORT VendorID, ProductID, VersionNumber; } SonyHidAttributes;
static struct {
    int loaded;
    void (WINAPI *get_guid)(GUID *);
    BOOLEAN (WINAPI *get_attributes)(HANDLE, SonyHidAttributes *);
    HDEVINFO (WINAPI *class_devs)(const GUID *, PCWSTR, HWND, DWORD);
    BOOL (WINAPI *enum_interfaces)(HDEVINFO, PSP_DEVINFO_DATA, const GUID *, DWORD, PSP_DEVICE_INTERFACE_DATA);
    BOOL (WINAPI *interface_detail)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_W, DWORD, PDWORD, PSP_DEVINFO_DATA);
    BOOL (WINAPI *destroy_list)(HDEVINFO);
    HANDLE h; OVERLAPPED ov; BYTE buf[1024]; int pending, dualsense; DWORD next_scan;
    SonyPadState state; int connected;
} sony;
static void sony_close(void) {
    if (sony.h) { CancelIo(sony.h); CloseHandle(sony.h); sony.h = NULL; }
    sony.pending = 0; sony.connected = 0; memset(&sony.state, 0, sizeof sony.state);
}
static int sony_load(void) {
    if (sony.loaded) return sony.loaded > 0;
    HMODULE hid = LoadLibraryW(L"hid.dll"), setup = LoadLibraryW(L"setupapi.dll");
    sony.loaded = -1;
    if (!hid || !setup) return 0;
    *(FARPROC *)&sony.get_guid = GetProcAddress(hid, "HidD_GetHidGuid");
    *(FARPROC *)&sony.get_attributes = GetProcAddress(hid, "HidD_GetAttributes");
    *(FARPROC *)&sony.class_devs = GetProcAddress(setup, "SetupDiGetClassDevsW");
    *(FARPROC *)&sony.enum_interfaces = GetProcAddress(setup, "SetupDiEnumDeviceInterfaces");
    *(FARPROC *)&sony.interface_detail = GetProcAddress(setup, "SetupDiGetDeviceInterfaceDetailW");
    *(FARPROC *)&sony.destroy_list = GetProcAddress(setup, "SetupDiDestroyDeviceInfoList");
    if (!sony.get_guid || !sony.get_attributes || !sony.class_devs || !sony.enum_interfaces || !sony.interface_detail || !sony.destroy_list) return 0;
    sony.ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    sony.loaded = sony.ov.hEvent ? 1 : -1;
    return sony.loaded > 0;
}
/* The first connected DualShock 4 / DualSense. */
static void sony_open(void) {
    GUID guid; sony.get_guid(&guid);
    HDEVINFO list = sony.class_devs(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (list == INVALID_HANDLE_VALUE) return;
    SP_DEVICE_INTERFACE_DATA item; item.cbSize = sizeof item;
    for (DWORD i = 0; !sony.h && sony.enum_interfaces(list, NULL, &guid, i, &item); i++) {
        DWORD need = 0; sony.interface_detail(list, &item, NULL, 0, &need, NULL);
        if (!need || need > 4096) continue;
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(need);
        if (!detail) continue;
        detail->cbSize = sizeof *detail;
        if (sony.interface_detail(list, &item, detail, need, NULL, NULL)) {
            HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
            if (h == INVALID_HANDLE_VALUE) h = CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                SonyHidAttributes a; memset(&a, 0, sizeof a); a.Size = sizeof a;
                if (sony.get_attributes(h, &a) && sony_known(a.VendorID, a.ProductID)) { sony.h = h; sony.dualsense = sony_is_dualsense(a.ProductID); }
                else CloseHandle(h);
            }
        }
        free(detail);
    }
    sony.destroy_list(list);
}
/* Latest state of the Sony pad (if any); 1 while one is connected. Never
 * blocks: one overlapped read stays queued, finished reports are drained. */
static int sony_poll(SonyPadState *out) {
    memset(out, 0, sizeof *out);
    if (!sony_load()) return 0;
    const DWORD now = GetTickCount();
    if (!sony.h) { if ((LONG)(now - sony.next_scan) < 0) return 0; sony.next_scan = now + 2000; sony_open(); if (!sony.h) return 0; }
    for (int k = 0; k < 32; k++) {
        DWORD n = 0;
        if (!sony.pending) {
            ResetEvent(sony.ov.hEvent);
            if (ReadFile(sony.h, sony.buf, sizeof sony.buf, &n, &sony.ov)) { if (sony_parse(sony.buf, n, sony.dualsense, &sony.state)) sony.connected = 1; continue; }
            if (GetLastError() != ERROR_IO_PENDING) { sony_close(); return 0; }   /* unplugged */
            sony.pending = 1;
        }
        if (!GetOverlappedResult(sony.h, &sony.ov, &n, FALSE)) {
            if (GetLastError() == ERROR_IO_INCOMPLETE) break;
            sony_close(); return 0;
        }
        sony.pending = 0;
        if (sony_parse(sony.buf, n, sony.dualsense, &sony.state)) sony.connected = 1;
    }
    *out = sony.state;
    return sony.connected;
}
#endif
#endif
