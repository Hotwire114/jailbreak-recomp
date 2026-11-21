/* Linux only: the Windows launcher's window, painting, input, dialogs, processes and threads on
 * SDL3 and POSIX. See linux-gui/windows.h for what is real and what reports failure.
 * LGPL-2.1-or-later. Original author code.
 *
 * Link with -Wl,--wrap=CloseHandle -Wl,--wrap=GetProcAddress: process, thread and event handles
 * close here (files and shared memory still close in linux-compat), and GetProcAddress hands out
 * the waveOut and XInput functions below for the "winmm.dll" / "xinput1_4.dll" the launcher loads. */
#define _GNU_SOURCE
#include <windows.h>
#include <SDL3/SDL.h>
#include <zlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <limits.h>

extern char **environ;

/* ---- Text and paths --------------------------------------------------------------------- */
static char *utf8_of(const wchar_t *w) {
    if (!w) return NULL;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = (char *)malloc(n > 0 ? (size_t)n : 1);
    if (!s) return NULL;
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL); else s[0] = 0;
    return s;
}
static wchar_t *wide_of(const char *s) {
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc(sizeof(wchar_t) * (n > 0 ? (size_t)n : 1));
    if (!w) return NULL;
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n); else w[0] = 0;
    return w;
}
static char *path_of(const wchar_t *w) {   /* Windows-style wide path -> POSIX UTF-8 */
    if (!w) return NULL;
    size_t cap = wcslen(w) * 4 + 8;
    char *s = (char *)malloc(cap);
    if (s) rr_linux_path(w, s, cap);
    return s;
}
static void copy_wide(wchar_t *out, size_t cap, const char *s) {
    if (!out || !cap) return;
    out[0] = 0;
    if (!s) return;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, (int)cap);
    out[cap - 1] = 0;
}
wchar_t *rr_wcsupr(wchar_t *s) { for (wchar_t *p = s; p && *p; p++) *p = (wchar_t)towupper((wint_t)*p); return s; }

/* linux-compat's file handle (rr_win32_linux.c): kept in step with it by hand. */
enum { H_FILE = 0x46494C45 };
typedef struct { int kind; int fd; size_t size; } CompatHandle;
static int compat_fd(HANDLE h) {
    CompatHandle *c = (CompatHandle *)h;
    return h && h != INVALID_HANDLE_VALUE && c->kind == H_FILE ? c->fd : -1;
}

/* ---- SDL start-up ----------------------------------------------------------------------- */
static int sdl_ready;
static int sdl_up(void) {
    if (sdl_ready) return 1;
    SDL_SetHint(SDL_HINT_APP_ID, "jailbreak-recomp");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) return 0;
    sdl_ready = 1;
    return 1;
}

/* ---- Windows ---------------------------------------------------------------------------- */
#define WIN_MAGIC 0x57494E44
typedef struct Win {
    int magic; WNDPROC proc; SDL_Window *sdl; SDL_Renderer *ren; SDL_Texture *tex; int tw, th;
    uint32_t *fb; int fw, fh; int invalid, dirty, visible, destroyed, enabled; LONG_PTR userdata;
} Win;
static Win *main_win;
static Win *win_of(HWND h) { Win *w = (Win *)h; return w && w->magic == WIN_MAGIC && !w->destroyed ? w : NULL; }
static Win *win_of_sdl(SDL_WindowID id) {
    SDL_Window *s = SDL_GetWindowFromID(id);
    Win *w = s ? (Win *)SDL_GetPointerProperty(SDL_GetWindowProperties(s), "rr.win", NULL) : NULL;
    return win_of(w);
}
typedef struct { wchar_t name[64]; WNDPROC proc; } Class;
static Class classes[16];
static int class_count;
WORD RegisterClassW(const WNDCLASSW *wc) {
    if (!wc || !wc->lpszClassName) return 0;
    for (int i = 0; i < class_count; i++) if (!wcscmp(classes[i].name, wc->lpszClassName)) { classes[i].proc = wc->lpfnWndProc; return (WORD)(i + 1); }
    if (class_count >= 16) return 0;
    wcsncpy(classes[class_count].name, wc->lpszClassName, 63);
    classes[class_count].proc = wc->lpfnWndProc;
    return (WORD)++class_count;
}
static void fb_fit(Win *w) {   /* the window's own image, the size of its client area */
    int cw = 0, ch = 0;
    SDL_GetWindowSizeInPixels(w->sdl, &cw, &ch);
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    if (cw == w->fw && ch == w->fh && w->fb) return;
    uint32_t *fb = (uint32_t *)calloc((size_t)cw * ch, 4);
    if (!fb) return;
    free(w->fb); w->fb = fb; w->fw = cw; w->fh = ch;
}
static void present(Win *w) {
    if (!w || !w->fb || !w->ren) return;
    if (!w->tex || w->tw != w->fw || w->th != w->fh) {
        if (w->tex) SDL_DestroyTexture(w->tex);
        w->tex = SDL_CreateTexture(w->ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w->fw, w->fh);
        w->tw = w->fw; w->th = w->fh;
        if (!w->tex) return;
        SDL_SetTextureBlendMode(w->tex, SDL_BLENDMODE_NONE);
    }
    SDL_UpdateTexture(w->tex, NULL, w->fb, w->fw * 4);
    SDL_RenderClear(w->ren);
    SDL_RenderTexture(w->ren, w->tex, NULL, NULL);
    SDL_RenderPresent(w->ren);
    w->dirty = 0;
}
HWND CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    (void)ex; (void)parent; (void)menu;
    if (style & WS_CHILD) return NULL;   /* child controls: not on Linux (the launcher uses rr_linux_prompt) */
    WNDPROC proc = NULL;
    for (int i = 0; i < class_count; i++) if (cls && !wcscmp(classes[i].name, cls)) proc = classes[i].proc;
    if (!proc || !sdl_up()) return NULL;
    Win *win = (Win *)calloc(1, sizeof *win);
    if (!win) return NULL;
    char *t = utf8_of(title ? title : L"");
    if (w == CW_USEDEFAULT || w <= 0) w = 1280;
    if (h == CW_USEDEFAULT || h <= 0) h = 760;
    win->sdl = SDL_CreateWindow(t ? t : "", w, h, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    free(t);
    if (!win->sdl) { free(win); return NULL; }
    if (x != CW_USEDEFAULT && y != CW_USEDEFAULT) SDL_SetWindowPosition(win->sdl, x, y);
    win->ren = SDL_CreateRenderer(win->sdl, NULL);
    if (!win->ren) { SDL_DestroyWindow(win->sdl); free(win); return NULL; }
    SDL_SetRenderVSync(win->ren, 1);
    win->magic = WIN_MAGIC; win->proc = proc; win->enabled = 1; win->invalid = 1;
    SDL_SetPointerProperty(SDL_GetWindowProperties(win->sdl), "rr.win", win);
    SDL_StartTextInput(win->sdl);
    if (!main_win) main_win = win;
    CREATESTRUCTW cs = { param, inst, menu, parent, h, w, y, x, (LONG)style, title, cls, ex };
    proc((HWND)win, WM_NCCREATE, 0, (LPARAM)&cs);
    proc((HWND)win, WM_CREATE, 0, (LPARAM)&cs);
    fb_fit(win);
    return (HWND)win;
}
static void drop_timers(HWND h);
BOOL DestroyWindow(HWND h) {
    Win *w = win_of(h);
    if (!w) return FALSE;
    w->proc(h, WM_DESTROY, 0, 0);
    drop_timers(h);
    w->destroyed = 1;
    if (w->tex) SDL_DestroyTexture(w->tex);
    if (w->ren) SDL_DestroyRenderer(w->ren);
    if (w->sdl) SDL_DestroyWindow(w->sdl);
    free(w->fb); w->fb = NULL;
    if (w == main_win) main_win = NULL;
    return TRUE;   /* the struct stays (a stale HWND then fails win_of) */
}
BOOL IsWindow(HWND h) { return win_of(h) != NULL; }
BOOL ShowWindow(HWND h, int cmd) {
    Win *w = win_of(h);
    if (!w) return FALSE;
    int was = w->visible;
    if (cmd == SW_HIDE) { SDL_HideWindow(w->sdl); w->visible = 0; }
    else if (cmd == SW_MINIMIZE) SDL_MinimizeWindow(w->sdl);
    else { SDL_ShowWindow(w->sdl); if (cmd == SW_RESTORE) SDL_RestoreWindow(w->sdl); w->visible = 1; w->invalid = 1; }
    return was;
}
BOOL UpdateWindow(HWND h) {
    Win *w = win_of(h);
    if (w && w->invalid && w->visible) w->proc(h, WM_PAINT, 0, 0);
    return w != NULL;
}
BOOL InvalidateRect(HWND h, const RECT *r, BOOL erase) {
    (void)r; (void)erase;
    Win *w = win_of(h);
    if (w) w->invalid = 1;
    return w != NULL;
}
BOOL IsWindowVisible(HWND h) { Win *w = win_of(h); return w && w->visible; }
BOOL IsIconic(HWND h) { Win *w = win_of(h); return w && (SDL_GetWindowFlags(w->sdl) & SDL_WINDOW_MINIMIZED); }
BOOL EnableWindow(HWND h, BOOL on) { Win *w = win_of(h); BOOL was = w ? !w->enabled : FALSE; if (w) w->enabled = on; return was; }
HWND GetForegroundWindow(void) {
    if (main_win && (SDL_GetWindowFlags(main_win->sdl) & SDL_WINDOW_INPUT_FOCUS)) return (HWND)main_win;
    return NULL;
}
BOOL SetForegroundWindow(HWND h) { Win *w = win_of(h); if (w) SDL_RaiseWindow(w->sdl); return w != NULL; }
HWND SetFocus(HWND h) { return h; }
HWND GetFocus(void) { return (HWND)main_win; }
HWND FindWindowW(LPCWSTR cls, LPCWSTR title) { (void)cls; (void)title; return NULL; }
BOOL GetClientRect(HWND h, RECT *r) {
    Win *w = win_of(h);
    if (!w || !r) return FALSE;
    int cw = 0, ch = 0;
    SDL_GetWindowSizeInPixels(w->sdl, &cw, &ch);
    r->left = r->top = 0; r->right = cw; r->bottom = ch;
    return TRUE;
}
BOOL GetWindowRect(HWND h, RECT *r) {
    Win *w = win_of(h);
    if (!w || !r) return FALSE;
    int x = 0, y = 0, cw = 0, ch = 0;
    SDL_GetWindowPosition(w->sdl, &x, &y); SDL_GetWindowSize(w->sdl, &cw, &ch);
    r->left = x; r->top = y; r->right = x + cw; r->bottom = y + ch;
    return TRUE;
}
BOOL SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags) {
    (void)after;
    Win *w = win_of(h);
    if (!w) return FALSE;
    if (!(flags & SWP_NOSIZE) && cx > 0 && cy > 0) SDL_SetWindowSize(w->sdl, cx, cy);
    if (!(flags & SWP_NOMOVE)) SDL_SetWindowPosition(w->sdl, x, y);
    w->invalid = 1;
    return TRUE;
}
BOOL AdjustWindowRectEx(RECT *r, DWORD style, BOOL menu, DWORD ex) { (void)r; (void)style; (void)menu; (void)ex; return TRUE; }
LONG_PTR SetWindowLongPtrW(HWND h, int index, LONG_PTR value) {
    Win *w = (Win *)h;
    if (!w || w->magic != WIN_MAGIC || index != GWLP_USERDATA) return 0;
    LONG_PTR old = w->userdata; w->userdata = value; return old;
}
LONG_PTR GetWindowLongPtrW(HWND h, int index) { Win *w = (Win *)h; return w && w->magic == WIN_MAGIC && index == GWLP_USERDATA ? w->userdata : 0; }
int GetWindowTextW(HWND h, LPWSTR out, int cap) {
    Win *w = win_of(h);
    if (!out || cap < 1) return 0;
    copy_wide(out, (size_t)cap, w ? SDL_GetWindowTitle(w->sdl) : "");
    return (int)wcslen(out);
}
BOOL SetWindowTextW(HWND h, LPCWSTR text) {
    Win *w = win_of(h);
    char *t = utf8_of(text);
    if (w && t) SDL_SetWindowTitle(w->sdl, t);
    free(t);
    return w != NULL;
}
HWND GetNextDlgTabItem(HWND dlg, HWND ctl, BOOL previous) { (void)dlg; (void)ctl; (void)previous; return NULL; }
LRESULT DefWindowProcW(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    (void)wp; (void)lp;
    if (msg == WM_CLOSE) DestroyWindow(h);
    if (msg == WM_PAINT) { Win *w = win_of(h); if (w) w->invalid = 0; }
    return 0;
}
LRESULT SendMessageW(HWND h, UINT msg, WPARAM wp, LPARAM lp) { Win *w = win_of(h); return w ? w->proc(h, msg, wp, lp) : 0; }

/* ---- Messages and timers ---------------------------------------------------------------- */
static MSG queue[512];
static int q_head, q_count, quit_code, quit_posted;
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
static Uint32 wake_event;
static void push_msg(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    pthread_mutex_lock(&q_lock);
    if (q_count < 512) {
        MSG *m = &queue[(q_head + q_count++) % 512];
        m->hwnd = h; m->message = msg; m->wParam = wp; m->lParam = lp; m->time = GetTickCount(); m->pt.x = m->pt.y = 0;
    }
    pthread_mutex_unlock(&q_lock);
}
static int pop_msg(MSG *out) {
    pthread_mutex_lock(&q_lock);
    int got = q_count > 0;
    if (got) { *out = queue[q_head]; q_head = (q_head + 1) % 512; q_count--; }
    pthread_mutex_unlock(&q_lock);
    return got;
}
BOOL PostMessageW(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    push_msg(h, msg, wp, lp);
    if (sdl_ready) {   /* wake a waiting GetMessageW (PostMessageW comes from worker threads too) */
        if (!wake_event) wake_event = SDL_RegisterEvents(1);
        SDL_Event e; SDL_zero(e); e.type = wake_event; SDL_PushEvent(&e);
    }
    return TRUE;
}
void PostQuitMessage(int code) { quit_code = code; quit_posted = 1; }
typedef struct { HWND w; UINT_PTR id; UINT ms; Uint64 due; int used; } Timer;
static Timer timers[32];
UINT_PTR SetTimer(HWND h, UINT_PTR id, UINT ms, void *proc) {
    (void)proc;
    int free_slot = -1;
    for (int i = 0; i < 32; i++) {
        if (timers[i].used && timers[i].w == h && timers[i].id == id) { free_slot = i; break; }
        if (!timers[i].used && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return 0;
    timers[free_slot] = (Timer){ h, id, ms ? ms : 1, SDL_GetTicks() + (ms ? ms : 1), 1 };
    return id;
}
BOOL KillTimer(HWND h, UINT_PTR id) {
    for (int i = 0; i < 32; i++) if (timers[i].used && timers[i].w == h && timers[i].id == id) { timers[i].used = 0; return TRUE; }
    return FALSE;
}
static void drop_timers(HWND h) { for (int i = 0; i < 32; i++) if (timers[i].w == h) timers[i].used = 0; }

/* SDL keys -> Windows virtual keys. */
static WPARAM vk_of(SDL_Keycode k) {
    if (k >= 'a' && k <= 'z') return (WPARAM)(k - 'a' + 'A');
    if (k >= '0' && k <= '9') return (WPARAM)k;
    if (k >= SDLK_F1 && k <= SDLK_F12) return (WPARAM)(VK_F1 + (k - SDLK_F1));
    if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return (WPARAM)(VK_NUMPAD0 + 1 + (k - SDLK_KP_1));
    switch (k) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return VK_RETURN;
    case SDLK_ESCAPE: return VK_ESCAPE; case SDLK_BACKSPACE: return VK_BACK; case SDLK_TAB: return VK_TAB;
    case SDLK_SPACE: return VK_SPACE; case SDLK_UP: return VK_UP; case SDLK_DOWN: return VK_DOWN;
    case SDLK_LEFT: return VK_LEFT; case SDLK_RIGHT: return VK_RIGHT; case SDLK_HOME: return VK_HOME;
    case SDLK_END: return VK_END; case SDLK_PAGEUP: return VK_PRIOR; case SDLK_PAGEDOWN: return VK_NEXT;
    case SDLK_DELETE: return VK_DELETE; case SDLK_INSERT: return VK_INSERT; case SDLK_PAUSE: return VK_PAUSE;
    case SDLK_CAPSLOCK: return VK_CAPITAL; case SDLK_NUMLOCKCLEAR: return VK_NUMLOCK; case SDLK_SCROLLLOCK: return VK_SCROLL;
    case SDLK_LSHIFT: return VK_LSHIFT; case SDLK_RSHIFT: return VK_RSHIFT;
    case SDLK_LCTRL: return VK_LCONTROL; case SDLK_RCTRL: return VK_RCONTROL;
    case SDLK_LALT: return VK_LMENU; case SDLK_RALT: return VK_RMENU; case SDLK_LGUI: return VK_LWIN;
    case SDLK_KP_0: return VK_NUMPAD0; case SDLK_KP_MULTIPLY: return VK_MULTIPLY; case SDLK_KP_PLUS: return VK_ADD;
    case SDLK_KP_MINUS: return VK_SUBTRACT; case SDLK_KP_PERIOD: return VK_DECIMAL; case SDLK_KP_DIVIDE: return VK_DIVIDE;
    case SDLK_SEMICOLON: return VK_OEM_1; case SDLK_EQUALS: return VK_OEM_PLUS; case SDLK_COMMA: return VK_OEM_COMMA;
    case SDLK_MINUS: return VK_OEM_MINUS; case SDLK_PERIOD: return VK_OEM_PERIOD; case SDLK_SLASH: return VK_OEM_2;
    case SDLK_GRAVE: return VK_OEM_3; case SDLK_LEFTBRACKET: return VK_OEM_4; case SDLK_BACKSLASH: return VK_OEM_5;
    case SDLK_RIGHTBRACKET: return VK_OEM_6; case SDLK_APOSTROPHE: return VK_OEM_7;
    default: return 0;
    }
}
static LPARAM key_lparam(const SDL_KeyboardEvent *k) {   /* scan code in bits 16-23, extended in 24 */
    int sc = 0, ext = 0;
    switch (k->scancode) {
    case SDL_SCANCODE_RCTRL: sc = 0x1D; ext = 1; break; case SDL_SCANCODE_RALT: sc = 0x38; ext = 1; break;
    case SDL_SCANCODE_LSHIFT: sc = 0x2A; break; case SDL_SCANCODE_RSHIFT: sc = 0x36; break;
    case SDL_SCANCODE_LCTRL: sc = 0x1D; break; case SDL_SCANCODE_LALT: sc = 0x38; break;
    case SDL_SCANCODE_KP_ENTER: sc = 0x1C; ext = 1; break;
    case SDL_SCANCODE_UP: case SDL_SCANCODE_DOWN: case SDL_SCANCODE_LEFT: case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_HOME: case SDL_SCANCODE_END: case SDL_SCANCODE_PAGEUP: case SDL_SCANCODE_PAGEDOWN:
    case SDL_SCANCODE_INSERT: case SDL_SCANCODE_DELETE: ext = 1; break;
    default: break;
    }
    return (LPARAM)(1 | (sc << 16) | (ext << 24) | (k->repeat ? 1 << 30 : 0));
}
static SDL_Cursor *cursor_arrow, *cursor_hand;
static int mouse_in;
static float px_scale(Win *w) {   /* window points -> pixels */
    int pw = 0, lw = 0, ph = 0, lh = 0;
    SDL_GetWindowSizeInPixels(w->sdl, &pw, &ph); SDL_GetWindowSize(w->sdl, &lw, &lh);
    return lw > 0 ? (float)pw / (float)lw : 1.0f;
}
static wchar_t *dropped_file;
static pthread_mutex_t drop_lock = PTHREAD_MUTEX_INITIALIZER;
wchar_t *rr_linux_take_dropped_file(void) {
    pthread_mutex_lock(&drop_lock); wchar_t *f = dropped_file; dropped_file = NULL; pthread_mutex_unlock(&drop_lock);
    return f;
}
static int modal_depth;   /* a file picker or message box is open: input to the windows is dropped */
static void translate(const SDL_Event *e) {
    switch (e->type) {
    case SDL_EVENT_QUIT: if (main_win) push_msg((HWND)main_win, WM_CLOSE, 0, 0); return;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: { Win *w = win_of_sdl(e->window.windowID); if (w && !modal_depth) push_msg((HWND)w, WM_CLOSE, 0, 0); return; }
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: case SDL_EVENT_WINDOW_RESIZED: {
        Win *w = win_of_sdl(e->window.windowID);
        if (w) { fb_fit(w); w->invalid = 1; RECT r; GetClientRect((HWND)w, &r); push_msg((HWND)w, WM_SIZE, 0, MAKELPARAM(r.right, r.bottom)); }
        return; }
    case SDL_EVENT_WINDOW_EXPOSED: case SDL_EVENT_WINDOW_SHOWN: case SDL_EVENT_WINDOW_RESTORED: {
        Win *w = win_of_sdl(e->window.windowID); if (w) w->invalid = 1; return; }
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: { Win *w = win_of_sdl(e->window.windowID); mouse_in = 0; if (w && !modal_depth) push_msg((HWND)w, WM_MOUSELEAVE, 0, 0); return; }
    case SDL_EVENT_DROP_FILE: {
        Win *w = win_of_sdl(e->drop.windowID);
        if (!w) w = main_win;
        if (!w || modal_depth || !e->drop.data) return;
        wchar_t *f = wide_of(e->drop.data);
        pthread_mutex_lock(&drop_lock); free(dropped_file); dropped_file = f; pthread_mutex_unlock(&drop_lock);
        push_msg((HWND)w, WM_DROPFILES, 0, 0);
        return; }
    default: break;
    }
    if (modal_depth) return;
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN: {
        Win *w = win_of_sdl(e->key.windowID); WPARAM vk = vk_of(e->key.key);
        if (!w || !w->enabled || !vk) return;
        int alt = (e->key.mod & SDL_KMOD_ALT) != 0;
        if (vk == VK_LSHIFT || vk == VK_RSHIFT) vk = VK_SHIFT;
        else if (vk == VK_LCONTROL || vk == VK_RCONTROL) vk = VK_CONTROL;
        else if (vk == VK_LMENU || vk == VK_RMENU) vk = VK_MENU;
        push_msg((HWND)w, alt || vk == VK_MENU || vk == VK_F10 ? WM_SYSKEYDOWN : WM_KEYDOWN, vk, key_lparam(&e->key));
        return; }
    case SDL_EVENT_TEXT_INPUT: {
        Win *w = win_of_sdl(e->text.windowID);
        if (!w || !w->enabled) return;
        wchar_t *t = wide_of(e->text.text);
        for (wchar_t *p = t; p && *p; p++) push_msg((HWND)w, WM_CHAR, (WPARAM)*p, 1);
        free(t);
        return; }
    case SDL_EVENT_MOUSE_MOTION: {
        Win *w = win_of_sdl(e->motion.windowID);
        if (!w || !w->enabled) return;
        float s = px_scale(w); mouse_in = 1;
        push_msg((HWND)w, WM_MOUSEMOVE, 0, MAKELPARAM((int)(e->motion.x * s), (int)(e->motion.y * s)));
        return; }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        Win *w = win_of_sdl(e->button.windowID);
        if (!w || !w->enabled) return;
        float s = px_scale(w); LPARAM lp = MAKELPARAM((int)(e->button.x * s), (int)(e->button.y * s));
        if (e->button.button == SDL_BUTTON_LEFT) push_msg((HWND)w, WM_LBUTTONDOWN, 0, lp);
        else if (e->button.button == SDL_BUTTON_RIGHT) push_msg((HWND)w, WM_RBUTTONDOWN, 0, lp);
        else if (e->button.button == SDL_BUTTON_X1) push_msg((HWND)w, WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON1), lp);
        else if (e->button.button == SDL_BUTTON_X2) push_msg((HWND)w, WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON2), lp);
        return; }
    case SDL_EVENT_MOUSE_WHEEL: {
        Win *w = win_of_sdl(e->wheel.windowID);
        if (!w || !w->enabled || e->wheel.y == 0) return;
        int delta = e->wheel.y > 0 ? WHEEL_DELTA : -WHEEL_DELTA;
        push_msg((HWND)w, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)(short)delta), 0);
        return; }
    default: return;
    }
}
static void pump(void) { SDL_Event e; while (SDL_PollEvent(&e)) translate(&e); }
static int next_timer(Uint64 now, Uint64 *wait) {
    int best = -1; Uint64 soonest = UINT64_MAX;
    for (int i = 0; i < 32; i++) if (timers[i].used && win_of(timers[i].w) && timers[i].due < soonest) { soonest = timers[i].due; best = i; }
    *wait = best < 0 ? 100 : soonest <= now ? 0 : soonest - now;
    return best;
}
BOOL PeekMessageW(MSG *m, HWND h, UINT lo, UINT hi, UINT remove) {
    (void)h; (void)lo; (void)hi; (void)remove;
    if (sdl_ready) pump();
    return pop_msg(m);
}
BOOL GetMessageW(MSG *m, HWND h, UINT lo, UINT hi) {
    (void)h; (void)lo; (void)hi;
    for (;;) {
        if (quit_posted) { memset(m, 0, sizeof *m); m->message = WM_QUIT; m->wParam = (WPARAM)quit_code; return FALSE; }
        if (pop_msg(m)) return TRUE;
        if (!sdl_ready) { memset(m, 0, sizeof *m); m->message = WM_QUIT; return FALSE; }
        pump();
        if (pop_msg(m)) return TRUE;
        Uint64 now = SDL_GetTicks(), wait;
        int t = next_timer(now, &wait);
        if (t >= 0 && wait == 0) {
            Timer *tm = &timers[t];
            tm->due = now + tm->ms;   /* like Windows: late ticks merge, they don't pile up */
            memset(m, 0, sizeof *m); m->hwnd = tm->w; m->message = WM_TIMER; m->wParam = tm->id; m->time = (DWORD)now;
            return TRUE;
        }
        for (Win *w = main_win; w; w = NULL)   /* WM_PAINT comes last, as on Windows */
            if (w->invalid && w->visible) { memset(m, 0, sizeof *m); m->hwnd = (HWND)w; m->message = WM_PAINT; return TRUE; }
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, (Sint32)(wait > 100 ? 100 : wait))) translate(&e);
    }
}
BOOL TranslateMessage(const MSG *m) { (void)m; return TRUE; }   /* WM_CHAR comes from SDL's text input */
LRESULT DispatchMessageW(const MSG *m) {
    Win *w = win_of(m->hwnd);
    if (!w) return 0;
    LRESULT r = w->proc(m->hwnd, m->message, m->wParam, m->lParam);
    if (m->message == WM_PAINT) w->invalid = 0;
    if (w->dirty && win_of(m->hwnd)) present(w);
    return r;
}

/* ---- Keyboard, mouse, cursor, screen ---------------------------------------------------- */
SHORT GetKeyState(int vk) {
    if (!sdl_ready) return 0;
    SDL_Keymod mod = SDL_GetModState();
    int down = 0;
    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) down = (mod & SDL_KMOD_SHIFT) != 0;
    else if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) down = (mod & SDL_KMOD_CTRL) != 0;
    else if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) down = (mod & SDL_KMOD_ALT) != 0;
    else if (vk == VK_CAPITAL) return (mod & SDL_KMOD_CAPS) ? 1 : 0;
    else {
        int n = 0; const bool *keys = SDL_GetKeyboardState(&n);
        SDL_Keycode k = vk >= 'A' && vk <= 'Z' ? (SDL_Keycode)(vk - 'A' + 'a') : (SDL_Keycode)vk;
        SDL_Scancode sc = SDL_GetScancodeFromKey(k, NULL);
        down = keys && sc > 0 && sc < n && keys[sc];
    }
    return down ? (SHORT)0x8000 : 0;
}
SHORT GetAsyncKeyState(int vk) { return GetKeyState(vk); }
BOOL TrackMouseEvent(TRACKMOUSEEVENT *t) { (void)t; return TRUE; }
BOOL GetCursorPos(POINT *p) {
    float x = 0, y = 0;
    if (sdl_ready) SDL_GetGlobalMouseState(&x, &y);
    if (p) { p->x = (LONG)x; p->y = (LONG)y; }
    return TRUE;
}
BOOL ScreenToClient(HWND h, POINT *p) {
    Win *w = win_of(h);
    if (!w || !p) return FALSE;
    float x = 0, y = 0;
    SDL_GetMouseState(&x, &y);   /* the window's own coordinates: no global positions on Wayland */
    float s = px_scale(w);
    p->x = (LONG)(x * s); p->y = (LONG)(y * s);
    return TRUE;
}
HCURSOR LoadCursorW(HINSTANCE inst, LPCWSTR name) { (void)inst; return (HCURSOR)name; }
HCURSOR SetCursor(HCURSOR c) {
    if (!sdl_ready) return NULL;
    if (!cursor_arrow) cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    if (!cursor_hand) cursor_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    SDL_SetCursor((LPCWSTR)c == IDC_HAND ? cursor_hand : cursor_arrow);
    return NULL;
}
HICON LoadIconW(HINSTANCE inst, LPCWSTR name) { (void)inst; (void)name; return NULL; }
HMONITOR MonitorFromWindow(HWND h, DWORD flags) {
    (void)flags;
    Win *w = win_of(h);
    if (!sdl_up()) return NULL;
    SDL_DisplayID d = w ? SDL_GetDisplayForWindow(w->sdl) : SDL_GetPrimaryDisplay();
    return (HMONITOR)(uintptr_t)(d ? d : SDL_GetPrimaryDisplay());
}
BOOL GetMonitorInfoW(HMONITOR m, MONITORINFO *mi) {
    if (!mi || !sdl_up()) return FALSE;
    SDL_DisplayID d = (SDL_DisplayID)(uintptr_t)m;
    SDL_Rect full = { 0, 0, 1280, 800 }, work;
    if (!d) d = SDL_GetPrimaryDisplay();
    SDL_GetDisplayBounds(d, &full);
    if (!SDL_GetDisplayUsableBounds(d, &work)) work = full;
    mi->rcMonitor = (RECT){ full.x, full.y, full.x + full.w, full.y + full.h };
    mi->rcWork = (RECT){ work.x, work.y, work.x + work.w, work.y + work.h };
    mi->dwFlags = 1;
    return TRUE;
}
BOOL SystemParametersInfoW(UINT action, UINT param, void *out, UINT ini) {
    (void)param; (void)ini;
    if (action == SPI_GETCLIENTAREAANIMATION && out) { *(BOOL *)out = TRUE; return TRUE; }
    if (action == SPI_GETWORKAREA && out) { MONITORINFO mi = { sizeof mi }; if (!GetMonitorInfoW(MonitorFromWindow(NULL, 0), &mi)) return FALSE; *(RECT *)out = mi.rcWork; return TRUE; }
    return FALSE;
}
BOOL SetProcessDPIAware(void) { return TRUE; }
int MulDiv(int a, int b, int c) { return c ? (int)(((int64_t)a * b + (c / 2)) / c) : -1; }

/* ---- Message boxes and file pickers ------------------------------------------------------ */
int MessageBoxW(HWND owner, LPCWSTR text, LPCWSTR title, UINT type) {
    char *t = utf8_of(text ? text : L""), *c = utf8_of(title ? title : L"");
    SDL_MessageBoxButtonData b[3]; int n = 0, kind = (int)(type & 0xF);
    int def_second = (type & MB_DEFBUTTON2) != 0;
#define BTN(id, label) (b[n] = (SDL_MessageBoxButtonData){ 0, id, label }, n++)
    if (kind == MB_YESNO) { BTN(IDYES, "Yes"); BTN(IDNO, "No"); }
    else if (kind == MB_YESNOCANCEL) { BTN(IDYES, "Yes"); BTN(IDNO, "No"); BTN(IDCANCEL, "Cancel"); }
    else if (kind == MB_RETRYCANCEL) { BTN(IDRETRY, "Retry"); BTN(IDCANCEL, "Cancel"); }
    else if (kind == MB_OKCANCEL) { BTN(IDOK, "OK"); BTN(IDCANCEL, "Cancel"); }
    else BTN(IDOK, "OK");
#undef BTN
    b[def_second && n > 1 ? 1 : 0].flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    for (int i = 0; i < n; i++) if (b[i].buttonID == IDCANCEL || b[i].buttonID == IDNO || (n == 1)) { b[i].flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT; break; }
    UINT icon = type & 0xF0;
    Win *w = win_of(owner);
    SDL_MessageBoxData box = { icon == MB_ICONERROR ? SDL_MESSAGEBOX_ERROR : icon == MB_ICONWARNING ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_INFORMATION,
                               w ? w->sdl : (main_win ? main_win->sdl : NULL), c ? c : "", t ? t : "", n, b, NULL };
    int pressed = -1;
    modal_depth++;
    if (!SDL_ShowMessageBox(&box, &pressed)) {
        /* No message box on this system: the text goes to the terminal, and the default answer. */
        fprintf(stderr, "%s: %s\n", c ? c : "", t ? t : "");
        pressed = b[def_second && n > 1 ? 1 : 0].buttonID;
    }
    modal_depth--;
    free(t); free(c);
    if (main_win) main_win->invalid = 1;
    return pressed < 0 ? (kind == MB_OK ? IDOK : IDCANCEL) : pressed;
}
typedef struct { volatile int done; char *path; int failed; } Pick;
static void SDLCALL picked(void *data, const char *const *files, int filter) {
    (void)filter;
    Pick *p = (Pick *)data;
    if (!files) p->failed = 1;
    else if (files[0]) p->path = SDL_strdup(files[0]);
    p->done = 1;
}
/* "Name\0*.bin;*.iso\0..." -> SDL filters ("bin;iso"). */
static int filters_of(LPCWSTR f, SDL_DialogFileFilter *out, char **names, char **patterns, int cap) {
    int n = 0;
    while (f && *f && n < cap) {
        const wchar_t *name = f; f += wcslen(f) + 1;
        if (!*f) break;
        const wchar_t *pat = f; f += wcslen(f) + 1;
        char *pn = utf8_of(name), *pp = utf8_of(pat), *q = pp ? (char *)malloc(strlen(pp) + 1) : NULL;
        if (!pn || !pp || !q) { free(pn); free(pp); free(q); break; }
        size_t o = 0;
        for (char *s = pp; *s; s++) { if (*s == '*' || (*s == '.' && (s == pp || s[-1] == '*'))) continue; q[o++] = *s; }
        q[o] = 0; free(pp);
        if (!strcmp(q, "") ) { free(q); q = SDL_strdup("*"); }
        names[n] = pn; patterns[n] = q; out[n].name = pn; out[n].pattern = q; n++;
    }
    return n;
}
static BOOL file_dialog(OPENFILENAMEW *o, int save) {
    if (!o || !o->lpstrFile || !sdl_up()) return FALSE;
    SDL_DialogFileFilter f[8]; char *names[8] = { 0 }, *patterns[8] = { 0 };
    int nf = filters_of(o->lpstrFilter, f, names, patterns, 8);
    char *start = o->lpstrFile[0] ? path_of(o->lpstrFile) : NULL;
    Pick p = { 0, NULL, 0 };
    Win *w = win_of(o->hwndOwner);
    SDL_Window *parent = w ? w->sdl : (main_win ? main_win->sdl : NULL);
    if (o->lpstrTitle) { char *t = utf8_of(o->lpstrTitle); (void)t; free(t); }
    modal_depth++;
    if (save) SDL_ShowSaveFileDialog(picked, &p, parent, nf ? f : NULL, nf, start);
    else SDL_ShowOpenFileDialog(picked, &p, parent, nf ? f : NULL, nf, start, false);
    while (!p.done) { SDL_Event e; if (SDL_WaitEventTimeout(&e, 50)) translate(&e); }
    modal_depth--;
    for (int i = 0; i < nf; i++) { free(names[i]); free(patterns[i]); }
    free(start);
    if (main_win) main_win->invalid = 1;
    if (p.failed) {
        char msg[600];
        snprintf(msg, sizeof msg, "This system has no file picker the launcher can open (%s).\n\n"
                 "Drag the file onto the launcher window instead, or install xdg-desktop-portal or zenity.", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Choose a file", msg, parent);
        return FALSE;
    }
    if (!p.path) return FALSE;
    copy_wide(o->lpstrFile, o->nMaxFile, p.path);
    SDL_free(p.path);
    return TRUE;
}
BOOL GetOpenFileNameW(OPENFILENAMEW *o) { return file_dialog(o, 0); }
BOOL GetSaveFileNameW(OPENFILENAMEW *o) { return file_dialog(o, 1); }

/* ---- Painting ---------------------------------------------------------------------------- */
#define DC_MAGIC 0x48444320
#define BMP_MAGIC 0x48424D50
#define FONT_MAGIC 0x48464E54
typedef struct { int magic; int w, h; uint32_t *px; } Bmp;
typedef struct { int magic; Win *win; Bmp *sel; HGDIOBJ font; } Dc;
typedef struct { int magic; int height, weight; } Font;
static Dc *dc_of(HDC h) { Dc *d = (Dc *)h; return d && d->magic == DC_MAGIC ? d : NULL; }
static Bmp *bmp_of(HGDIOBJ h) { Bmp *b = (Bmp *)h; return b && b->magic == BMP_MAGIC ? b : NULL; }
HDC GetDC(HWND h) {
    Dc *d = (Dc *)calloc(1, sizeof *d);
    if (!d) return NULL;
    d->magic = DC_MAGIC; d->win = win_of(h);
    if (d->win) fb_fit(d->win);
    return (HDC)d;
}
int ReleaseDC(HWND h, HDC dc) {
    (void)h;
    Dc *d = dc_of(dc);
    if (!d) return 0;
    if (d->win && win_of((HWND)d->win) && d->win->dirty) present(d->win);
    d->magic = 0; free(d);
    return 1;
}
HDC BeginPaint(HWND h, PAINTSTRUCT *ps) {
    Win *w = win_of(h);
    HDC dc = GetDC(h);
    if (ps) { memset(ps, 0, sizeof *ps); ps->hdc = dc; if (w) { ps->rcPaint.right = w->fw; ps->rcPaint.bottom = w->fh; } }
    if (w) w->invalid = 0;
    return dc;
}
BOOL EndPaint(HWND h, const PAINTSTRUCT *ps) { return ReleaseDC(h, ps ? ps->hdc : NULL); }
HDC CreateCompatibleDC(HDC dc) { (void)dc; return GetDC(NULL); }
BOOL DeleteDC(HDC dc) { Dc *d = dc_of(dc); if (!d) return FALSE; d->magic = 0; free(d); return TRUE; }
HBITMAP CreateDIBSection(HDC dc, const BITMAPINFO *bi, UINT usage, void **bits, HANDLE section, DWORD offset) {
    (void)dc; (void)usage; (void)section; (void)offset;
    if (bits) *bits = NULL;
    if (!bi || bi->bmiHeader.biBitCount != 32) return NULL;
    int w = bi->bmiHeader.biWidth, h = bi->bmiHeader.biHeight < 0 ? -bi->bmiHeader.biHeight : bi->bmiHeader.biHeight;
    if (w < 1 || h < 1 || w > 16384 || h > 16384) return NULL;
    Bmp *b = (Bmp *)calloc(1, sizeof *b);
    if (!b) return NULL;
    b->px = (uint32_t *)calloc((size_t)w * h, 4);
    if (!b->px) { free(b); return NULL; }
    b->magic = BMP_MAGIC; b->w = w; b->h = h;   /* always top-down: the launcher only asks for that */
    if (bits) *bits = b->px;
    return (HBITMAP)b;
}
HGDIOBJ SelectObject(HDC dc, HGDIOBJ obj) {
    Dc *d = dc_of(dc);
    if (!d) return NULL;
    if (bmp_of(obj)) { HGDIOBJ old = (HGDIOBJ)d->sel; d->sel = (Bmp *)obj; return old; }
    HGDIOBJ old = d->font; d->font = obj; return old;
}
BOOL DeleteObject(HGDIOBJ obj) {
    Bmp *b = bmp_of(obj);
    if (b) { b->magic = 0; free(b->px); free(b); return TRUE; }
    Font *f = (Font *)obj;
    if (f && f->magic == FONT_MAGIC) { f->magic = 0; free(f); return TRUE; }
    return FALSE;
}
BOOL BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop) {
    (void)rop;
    Dc *d = dc_of(dst), *s = dc_of(src);
    if (!d || !s || !s->sel) return FALSE;
    uint32_t *to; int tw, th;
    if (d->win) { if (!win_of((HWND)d->win)) return FALSE; fb_fit(d->win); to = d->win->fb; tw = d->win->fw; th = d->win->fh; }
    else if (d->sel) { to = d->sel->px; tw = d->sel->w; th = d->sel->h; }
    else return FALSE;
    if (!to) return FALSE;
    for (int j = 0; j < h; j++) {
        int ty = y + j, fy = sy + j;
        if (ty < 0 || ty >= th || fy < 0 || fy >= s->sel->h) continue;
        int x0 = x, fx = sx, n = w;
        if (x0 < 0) { fx -= x0; n += x0; x0 = 0; }
        if (fx < 0) { x0 -= fx; n += fx; fx = 0; }
        if (x0 + n > tw) n = tw - x0;
        if (fx + n > s->sel->w) n = s->sel->w - fx;
        if (n > 0) memcpy(to + (size_t)ty * tw + x0, s->sel->px + (size_t)fy * s->sel->w + fx, (size_t)n * 4);
    }
    if (d->win) d->win->dirty = 1;
    return TRUE;
}
BOOL GdiFlush(void) { return TRUE; }
int GetDeviceCaps(HDC dc, int index) { (void)dc; return index == LOGPIXELSX || index == LOGPIXELSY ? 96 : 0; }
HFONT CreateFontW(int h, int w, int esc, int orient, int weight, DWORD italic, DWORD underline, DWORD strike, DWORD charset, DWORD out, DWORD clip, DWORD quality, DWORD pitch, LPCWSTR face) {
    (void)w; (void)esc; (void)orient; (void)italic; (void)underline; (void)strike; (void)charset; (void)out; (void)clip; (void)quality; (void)pitch; (void)face;
    Font *f = (Font *)calloc(1, sizeof *f);
    if (f) { f->magic = FONT_MAGIC; f->height = h; f->weight = weight; }
    return (HFONT)f;
}
int SetBkMode(HDC dc, int mode) { (void)dc; (void)mode; return 1; }
COLORREF SetTextColor(HDC dc, COLORREF c) { (void)dc; (void)c; return 0; }
int DrawTextW(HDC dc, LPCWSTR text, int n, RECT *r, UINT format) { (void)dc; (void)text; (void)n; (void)r; (void)format; return 0; }   /* the launcher's Linux text goes through its HD font */
int EnumFontFamiliesExW(HDC dc, LOGFONTW *lf, FONTENUMPROCW proc, LPARAM lp, DWORD flags) { (void)dc; (void)lf; (void)proc; (void)lp; (void)flags; return 1; }

/* ---- Resources and libraries -------------------------------------------------------------- */
HRSRC FindResourceW(HMODULE m, LPCWSTR name, LPCWSTR type) { (void)m; (void)name; (void)type; return NULL; }
DWORD SizeofResource(HMODULE m, HRSRC r) { (void)m; (void)r; return 0; }
HGLOBAL LoadResource(HMODULE m, HRSRC r) { (void)m; (void)r; return NULL; }
LPVOID LockResource(HGLOBAL g) { return g; }
enum { LIB_WINMM = 1, LIB_XINPUT = 2 };
HMODULE LoadLibraryW(LPCWSTR name) {
    if (!name) return NULL;
    if (!wcscasecmp(name, L"winmm.dll")) return (HMODULE)(uintptr_t)LIB_WINMM;
    if (!wcsncasecmp(name, L"xinput", 6)) return (HMODULE)(uintptr_t)LIB_XINPUT;
    return NULL;   /* hid.dll, setupapi.dll ...: not on Linux */
}
BOOL FreeLibrary(HMODULE m) { (void)m; return TRUE; }

/* waveOut (the voice preview): one SDL audio stream per open device. */
typedef struct { SDL_AudioStream *s; WAVEHDR *hdr; } WaveOut;
static MMRESULT WINAPI wave_open(HWAVEOUT *out, UINT dev, LPCWAVEFORMATEX f, DWORD_PTR cb, DWORD_PTR inst, DWORD flags) {
    (void)dev; (void)cb; (void)inst; (void)flags;
    if (!out || !f || f->wFormatTag != WAVE_FORMAT_PCM || (f->wBitsPerSample != 16 && f->wBitsPerSample != 8)) return MMSYSERR_ERROR;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return MMSYSERR_ERROR;
    SDL_AudioSpec spec = { f->wBitsPerSample == 16 ? SDL_AUDIO_S16 : SDL_AUDIO_U8, f->nChannels, (int)f->nSamplesPerSec };
    WaveOut *w = (WaveOut *)calloc(1, sizeof *w);
    if (!w) return MMSYSERR_ERROR;
    w->s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!w->s) { free(w); return MMSYSERR_ERROR; }
    SDL_ResumeAudioStreamDevice(w->s);
    *out = (HWAVEOUT)w;
    return MMSYSERR_NOERROR;
}
static MMRESULT WINAPI wave_prepare(HWAVEOUT h, LPWAVEHDR hdr, UINT size) { (void)h; (void)size; if (!hdr) return MMSYSERR_ERROR; hdr->dwFlags |= WHDR_PREPARED; return MMSYSERR_NOERROR; }
static MMRESULT WINAPI wave_unprepare(HWAVEOUT h, LPWAVEHDR hdr, UINT size) { (void)h; (void)size; if (!hdr) return MMSYSERR_ERROR; hdr->dwFlags &= ~(DWORD)WHDR_PREPARED; return MMSYSERR_NOERROR; }
static MMRESULT WINAPI wave_write(HWAVEOUT h, LPWAVEHDR hdr, UINT size) {
    (void)size;
    WaveOut *w = (WaveOut *)h;
    if (!w || !hdr || !SDL_PutAudioStreamData(w->s, hdr->lpData, (int)hdr->dwBufferLength)) return MMSYSERR_ERROR;
    hdr->dwFlags |= WHDR_DONE;   /* the data is copied: the buffer can be reused at once */
    w->hdr = hdr;
    return MMSYSERR_NOERROR;
}
static MMRESULT WINAPI wave_reset(HWAVEOUT h) { WaveOut *w = (WaveOut *)h; if (w) SDL_ClearAudioStream(w->s); return MMSYSERR_NOERROR; }
static MMRESULT WINAPI wave_close(HWAVEOUT h) { WaveOut *w = (WaveOut *)h; if (!w) return MMSYSERR_ERROR; SDL_DestroyAudioStream(w->s); free(w); return MMSYSERR_NOERROR; }

/* XInputGetState from SDL gamepads (player index = the order they were connected). */
static DWORD WINAPI xinput_get_state(DWORD index, XINPUT_STATE *st) {
    if (!st || !sdl_ready) return ERROR_DEVICE_NOT_CONNECTED;
    static SDL_Gamepad *pads[4];
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    for (int i = 0; i < 4; i++) {
        SDL_JoystickID want = ids && i < n ? ids[i] : 0;
        if (pads[i] && (!want || SDL_GetGamepadID(pads[i]) != want)) { SDL_CloseGamepad(pads[i]); pads[i] = NULL; }
        if (!pads[i] && want) pads[i] = SDL_OpenGamepad(want);
    }
    SDL_free(ids);
    SDL_Gamepad *g = index < 4 ? pads[index] : NULL;
    if (!g) return ERROR_DEVICE_NOT_CONNECTED;
    static DWORD packet;
    memset(st, 0, sizeof *st);
    st->dwPacketNumber = ++packet;
    static const struct { SDL_GamepadButton b; WORD x; } map[] = {
        { SDL_GAMEPAD_BUTTON_DPAD_UP, XINPUT_GAMEPAD_DPAD_UP }, { SDL_GAMEPAD_BUTTON_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_DOWN },
        { SDL_GAMEPAD_BUTTON_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_LEFT }, { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_RIGHT },
        { SDL_GAMEPAD_BUTTON_START, XINPUT_GAMEPAD_START }, { SDL_GAMEPAD_BUTTON_BACK, XINPUT_GAMEPAD_BACK },
        { SDL_GAMEPAD_BUTTON_LEFT_STICK, XINPUT_GAMEPAD_LEFT_THUMB }, { SDL_GAMEPAD_BUTTON_RIGHT_STICK, XINPUT_GAMEPAD_RIGHT_THUMB },
        { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, XINPUT_GAMEPAD_LEFT_SHOULDER }, { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER },
        { SDL_GAMEPAD_BUTTON_SOUTH, XINPUT_GAMEPAD_A }, { SDL_GAMEPAD_BUTTON_EAST, XINPUT_GAMEPAD_B },
        { SDL_GAMEPAD_BUTTON_WEST, XINPUT_GAMEPAD_X }, { SDL_GAMEPAD_BUTTON_NORTH, XINPUT_GAMEPAD_Y } };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) if (SDL_GetGamepadButton(g, map[i].b)) st->Gamepad.wButtons |= map[i].x;
    st->Gamepad.bLeftTrigger = (BYTE)(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) >> 7);
    st->Gamepad.bRightTrigger = (BYTE)(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >> 7);
    Sint16 ly = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY), ry = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY);
    st->Gamepad.sThumbLX = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
    st->Gamepad.sThumbLY = (SHORT)(ly == -32768 ? 32767 : -ly);   /* XInput: up is positive */
    st->Gamepad.sThumbRX = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX);
    st->Gamepad.sThumbRY = (SHORT)(ry == -32768 ? 32767 : -ry);
    return ERROR_SUCCESS;
}
void *__real_GetProcAddress(HMODULE m, const char *name);
void *__wrap_GetProcAddress(HMODULE m, const char *name) {
    if (!name) return NULL;
    if ((uintptr_t)m == LIB_WINMM) {
        if (!strcmp(name, "waveOutOpen")) return (void *)wave_open;
        if (!strcmp(name, "waveOutPrepareHeader")) return (void *)wave_prepare;
        if (!strcmp(name, "waveOutUnprepareHeader")) return (void *)wave_unprepare;
        if (!strcmp(name, "waveOutWrite")) return (void *)wave_write;
        if (!strcmp(name, "waveOutReset")) return (void *)wave_reset;
        if (!strcmp(name, "waveOutClose")) return (void *)wave_close;
        return NULL;
    }
    if ((uintptr_t)m == LIB_XINPUT) return !strcmp(name, "XInputGetState") ? (void *)xinput_get_state : NULL;
    return __real_GetProcAddress(m, name);
}

/* ---- Processes, threads, events ----------------------------------------------------------- */
#define PROC_MAGIC 0x50524F43
#define THREAD_MAGIC 0x54485244
#define EVENT_MAGIC 0x45564E54
typedef struct { int magic; pid_t pid; int child, exited; DWORD code; } Proc;
typedef struct {
    int magic; pthread_t t; unsigned (*start)(void *); void *arg; DWORD code; int done, closed;
    pthread_mutex_t m; pthread_cond_t c;
} Thread;
typedef struct { int magic; int manual, set; pthread_mutex_t m; pthread_cond_t c; } Event;
static int magic_of(HANDLE h) { return h && h != INVALID_HANDLE_VALUE ? *(int *)h : 0; }
static void reap(Proc *p, int block) {
    if (p->exited || !p->child) return;
    int st = 0;
    pid_t r;
    do r = waitpid(p->pid, &st, block ? 0 : WNOHANG); while (r < 0 && errno == EINTR);
    if (r == p->pid) { p->exited = 1; p->code = WIFEXITED(st) ? (DWORD)WEXITSTATUS(st) : (DWORD)(128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0)); }
    else if (r < 0) { p->exited = 1; p->code = 1; }
}
/* Windows command line -> arguments (CommandLineToArgvW rules). */
static int split_command(const wchar_t *line, wchar_t ***out) {
    size_t cap = 8, n = 0, len = wcslen(line);
    wchar_t **args = (wchar_t **)calloc(cap + 1, sizeof *args);
    wchar_t *buf = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!args || !buf) { free(args); free(buf); return -1; }
    const wchar_t *p = line;
    while (*p) {
        while (*p == L' ' || *p == L'\t') p++;
        if (!*p) break;
        size_t o = 0; int quoted = 0;
        while (*p && (quoted || (*p != L' ' && *p != L'\t'))) {
            if (*p == L'\\') {
                size_t k = 0; while (p[k] == L'\\') k++;
                if (p[k] == L'"') { for (size_t i = 0; i < k / 2; i++) buf[o++] = L'\\'; p += k; if (k % 2) { buf[o++] = L'"'; p++; } }
                else { for (size_t i = 0; i < k; i++) buf[o++] = L'\\'; p += k; }
            } else if (*p == L'"') { if (quoted && p[1] == L'"') { buf[o++] = L'"'; p += 2; } else { quoted = !quoted; p++; } }
            else buf[o++] = *p++;
        }
        buf[o] = 0;
        if (n == cap) { cap *= 2; wchar_t **a = (wchar_t **)realloc(args, (cap + 1) * sizeof *args); if (!a) break; args = a; }
        args[n++] = wcsdup(buf);
    }
    args[n] = NULL;
    free(buf);
    *out = args;
    return (int)n;
}
LPWSTR *CommandLineToArgvW(LPCWSTR line, int *argc) {
    wchar_t **a = NULL;
    int n = split_command(line ? line : L"", &a);
    if (argc) *argc = n < 0 ? 0 : n;
    return a;   /* LocalFree(argv) frees the array; the strings are left (a few bytes, once) */
}
/* Arguments on Linux: '\\' path separators (the launcher builds Windows-style paths) become '/'. */
static char *arg_of(const wchar_t *w) {
    char *s = utf8_of(w);
    for (char *p = s; p && *p; p++) if (*p == '\\') *p = '/';
    return s;
}
BOOL CreateProcessW(LPCWSTR app, LPWSTR cmd, SECURITY_ATTRIBUTES *pa, SECURITY_ATTRIBUTES *ta, BOOL inherit, DWORD flags, LPVOID env, LPCWSTR cwd, STARTUPINFOW *si, PROCESS_INFORMATION *pi) {
    (void)pa; (void)ta; (void)inherit;
    wchar_t **wargs = NULL;
    int n = split_command(cmd ? cmd : (app ? app : L""), &wargs);
    if (n <= 0) { free(wargs); SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
    char **argv = (char **)calloc((size_t)n + 1, sizeof *argv);
    for (int i = 0; argv && i < n; i++) argv[i] = arg_of(wargs[i]);
    char *program = app ? arg_of(app) : (argv ? strdup(argv[0]) : NULL);
    char **envp = NULL;
    if (env && (flags & CREATE_UNICODE_ENVIRONMENT)) {
        size_t count = 0;
        for (const wchar_t *e = (const wchar_t *)env; *e; e += wcslen(e) + 1) count++;
        envp = (char **)calloc(count + 1, sizeof *envp);
        size_t k = 0;
        for (const wchar_t *e = (const wchar_t *)env; envp && *e; e += wcslen(e) + 1) envp[k++] = utf8_of(e);
    }
    posix_spawn_file_actions_t fa; posix_spawnattr_t at;
    posix_spawn_file_actions_init(&fa); posix_spawnattr_init(&at);
    char *dir = cwd ? path_of(cwd) : NULL;
    if (dir) posix_spawn_file_actions_addchdir_np(&fa, dir);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    if (si && (si->dwFlags & STARTF_USESTDHANDLES)) {
        int out = compat_fd(si->hStdOutput), err = compat_fd(si->hStdError);
        if (out >= 0) posix_spawn_file_actions_adddup2(&fa, out, 1);
        if (err >= 0) posix_spawn_file_actions_adddup2(&fa, err, 2);
    }
    sigset_t none; sigemptyset(&none);
    sigset_t def; sigemptyset(&def); sigaddset(&def, SIGPIPE);
    posix_spawnattr_setsigmask(&at, &none); posix_spawnattr_setsigdefault(&at, &def);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    pid_t pid = -1;
    int r = program && argv ? posix_spawn(&pid, program, &fa, &at, argv, envp ? envp : environ) : ENOMEM;
    posix_spawn_file_actions_destroy(&fa); posix_spawnattr_destroy(&at);
    for (int i = 0; i < n; i++) free(wargs[i]);
    free(wargs);
    for (int i = 0; argv && i < n; i++) free(argv[i]);
    free(argv); free(program); free(dir);
    if (envp) { for (size_t i = 0; envp[i]; i++) free(envp[i]); free(envp); }
    if (r) { errno = r; SetLastError(r == ENOENT ? ERROR_FILE_NOT_FOUND : ERROR_ACCESS_DENIED); return FALSE; }
    Proc *p = (Proc *)calloc(1, sizeof *p);
    if (!p) return FALSE;
    p->magic = PROC_MAGIC; p->pid = pid; p->child = 1;
    if (pi) { pi->hProcess = (HANDLE)p; pi->hThread = NULL; pi->dwProcessId = (DWORD)pid; pi->dwThreadId = 0; }
    return TRUE;
}
BOOL GetExitCodeProcess(HANDLE h, DWORD *code) {
    Proc *p = (Proc *)h;
    if (magic_of(h) != PROC_MAGIC || !code) return FALSE;
    if (p->child) reap(p, 0);
    else if (!p->exited && kill(p->pid, 0) && errno == ESRCH) { p->exited = 1; p->code = 0; }
    *code = p->exited ? p->code : STILL_ACTIVE;
    return TRUE;
}
BOOL TerminateProcess(HANDLE h, UINT code) {
    Proc *p = (Proc *)h;
    if (magic_of(h) != PROC_MAGIC) return FALSE;
    if (!p->exited) kill(p->pid, SIGKILL);
    if (p->child) reap(p, 1); else { p->exited = 1; }
    if (!p->code) p->code = code;
    return TRUE;
}
DWORD GetProcessId(HANDLE h) { return magic_of(h) == PROC_MAGIC ? (DWORD)((Proc *)h)->pid : 0; }
HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD pid) {
    (void)access; (void)inherit;
    if (!pid || kill((pid_t)pid, 0)) return NULL;
    Proc *p = (Proc *)calloc(1, sizeof *p);
    if (p) { p->magic = PROC_MAGIC; p->pid = (pid_t)pid; }
    return (HANDLE)p;
}
BOOL QueryFullProcessImageNameW(HANDLE h, DWORD flags, LPWSTR out, DWORD *size) {
    (void)flags;
    if (magic_of(h) != PROC_MAGIC || !out || !size) return FALSE;
    char link[64], target[PATH_MAX];
    snprintf(link, sizeof link, "/proc/%d/exe", (int)((Proc *)h)->pid);
    ssize_t n = readlink(link, target, sizeof target - 1);
    if (n <= 0) return FALSE;
    target[n] = 0;
    copy_wide(out, *size, target);
    *size = (DWORD)wcslen(out);
    return TRUE;
}
static void *thread_main(void *arg) {
    Thread *t = (Thread *)arg;
    unsigned code = t->start(t->arg);
    pthread_mutex_lock(&t->m);
    t->code = code; t->done = 1;
    int closed = t->closed;
    pthread_cond_broadcast(&t->c);
    pthread_mutex_unlock(&t->m);
    if (closed) { t->magic = 0; free(t); }
    return NULL;
}
uintptr_t _beginthreadex(void *security, unsigned stack, unsigned (*start)(void *), void *arg, unsigned flags, unsigned *id) {
    (void)security; (void)flags;
    Thread *t = (Thread *)calloc(1, sizeof *t);
    if (!t) return 0;
    t->magic = THREAD_MAGIC; t->start = start; t->arg = arg;
    pthread_mutex_init(&t->m, NULL); pthread_cond_init(&t->c, NULL);
    pthread_attr_t a; pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, stack > (1u << 20) ? stack : 8u << 20);   /* the launcher's threads use big stack arrays */
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    int r = pthread_create(&t->t, &a, thread_main, t);
    pthread_attr_destroy(&a);
    if (r) { free(t); return 0; }
    if (id) *id = 1;
    return (uintptr_t)t;
}
typedef struct { LPTHREAD_START_ROUTINE f; void *arg; } Trampoline;
static unsigned trampoline(void *a) { Trampoline t = *(Trampoline *)a; free(a); return t.f(t.arg); }
HANDLE CreateThread(void *security, SIZE_T stack, LPTHREAD_START_ROUTINE start, LPVOID arg, DWORD flags, DWORD *id) {
    Trampoline *t = (Trampoline *)malloc(sizeof *t);
    if (!t) return NULL;
    t->f = start; t->arg = arg;
    unsigned tid = 0;
    uintptr_t h = _beginthreadex(security, (unsigned)stack, trampoline, t, flags, &tid);
    if (!h) { free(t); return NULL; }
    if (id) *id = tid;
    return (HANDLE)h;
}
BOOL GetExitCodeThread(HANDLE h, DWORD *code) {
    Thread *t = (Thread *)h;
    if (magic_of(h) != THREAD_MAGIC || !code) return FALSE;
    pthread_mutex_lock(&t->m); *code = t->done ? t->code : STILL_ACTIVE; pthread_mutex_unlock(&t->m);
    return TRUE;
}
HANDLE CreateEventW(void *security, BOOL manual, BOOL initial, LPCWSTR name) {
    (void)security; (void)name;
    Event *e = (Event *)calloc(1, sizeof *e);
    if (!e) return NULL;
    e->magic = EVENT_MAGIC; e->manual = manual; e->set = initial;
    pthread_mutex_init(&e->m, NULL); pthread_cond_init(&e->c, NULL);
    return (HANDLE)e;
}
BOOL SetEvent(HANDLE h) { Event *e = (Event *)h; if (magic_of(h) != EVENT_MAGIC) return FALSE; pthread_mutex_lock(&e->m); e->set = 1; pthread_cond_broadcast(&e->c); pthread_mutex_unlock(&e->m); return TRUE; }
BOOL ResetEvent(HANDLE h) { Event *e = (Event *)h; if (magic_of(h) != EVENT_MAGIC) return FALSE; pthread_mutex_lock(&e->m); e->set = 0; pthread_mutex_unlock(&e->m); return TRUE; }
static void deadline_of(DWORD ms, struct timespec *ts) {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000; ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec++; ts->tv_nsec -= 1000000000L; }
}
DWORD WaitForSingleObject(HANDLE h, DWORD ms) {
    int kind = magic_of(h);
    if (kind == PROC_MAGIC) {
        Proc *p = (Proc *)h;
        if (ms == INFINITE && p->child) { reap(p, 1); return WAIT_OBJECT_0; }
        Uint64 end = GetTickCount64() + ms;
        for (;;) {
            DWORD code; GetExitCodeProcess(h, &code);
            if (p->exited) return WAIT_OBJECT_0;
            if (ms != INFINITE && GetTickCount64() >= end) return WAIT_TIMEOUT;
            Sleep(10);
        }
    }
    if (kind == THREAD_MAGIC || kind == EVENT_MAGIC) {
        pthread_mutex_t *m = kind == THREAD_MAGIC ? &((Thread *)h)->m : &((Event *)h)->m;
        pthread_cond_t *c = kind == THREAD_MAGIC ? &((Thread *)h)->c : &((Event *)h)->c;
        int *flag = kind == THREAD_MAGIC ? &((Thread *)h)->done : &((Event *)h)->set;
        struct timespec ts; deadline_of(ms, &ts);
        pthread_mutex_lock(m);
        int r = 0;
        while (!*flag && r == 0) r = ms == INFINITE ? pthread_cond_wait(c, m) : pthread_cond_timedwait(c, m, &ts);
        int got = *flag;
        if (got && kind == EVENT_MAGIC && !((Event *)h)->manual) ((Event *)h)->set = 0;
        pthread_mutex_unlock(m);
        return got ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
    }
    return WAIT_FAILED;
}
BOOL __real_CloseHandle(HANDLE h);
BOOL __wrap_CloseHandle(HANDLE h) {
    int kind = magic_of(h);
    if (kind == PROC_MAGIC) { Proc *p = (Proc *)h; p->magic = 0; free(p); return TRUE; }   /* an unreaped child is reaped by SIGCHLD's default (it stays a zombie until the launcher exits) */
    if (kind == THREAD_MAGIC) {
        Thread *t = (Thread *)h;
        pthread_mutex_lock(&t->m);
        int done = t->done; t->closed = 1;
        pthread_mutex_unlock(&t->m);
        if (done) { t->magic = 0; free(t); }
        return TRUE;
    }
    if (kind == EVENT_MAGIC) { Event *e = (Event *)h; e->magic = 0; free(e); return TRUE; }
    return __real_CloseHandle(h);
}
LPWSTR GetEnvironmentStringsW(void) {
    size_t total = 1;
    for (char **e = environ; *e; e++) total += strlen(*e) + 1;
    wchar_t *out = (wchar_t *)malloc(total * sizeof(wchar_t)), *d = out;
    if (!out) return NULL;
    for (char **e = environ; *e; e++) {
        int n = MultiByteToWideChar(CP_UTF8, 0, *e, -1, d, (int)(total - (size_t)(d - out)));
        if (n > 0) d += n;
    }
    *d = 0;
    return out;
}
BOOL FreeEnvironmentStringsW(LPWSTR env) { free(env); return TRUE; }
/* LOCALAPPDATA: the launcher's data folder lives under it, so on Linux it is the XDG data folder
 * ($XDG_DATA_HOME, else ~/.local/share): the game folder is ~/.local/share/RoadRashJailbreak. */
DWORD GetEnvironmentVariableW(LPCWSTR name, LPWSTR out, DWORD size) {
    char *n = utf8_of(name), buf[PATH_MAX];
    const char *v = n ? getenv(n) : NULL;
    if (n && !strcmp(n, "LOCALAPPDATA")) {
        const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
        if (xdg && *xdg == '/') v = xdg;
        else if (home && *home) { snprintf(buf, sizeof buf, "%s/.local/share", home); v = buf; }
    }
    free(n);
    if (!v) { SetLastError(203); if (out && size) out[0] = 0; return 0; }
    wchar_t *w = wide_of(v);
    DWORD need = w ? (DWORD)wcslen(w) + 1 : 0;
    if (w && out && size >= need) { wcscpy(out, w); free(w); return need - 1; }
    free(w);
    return need;
}
BOOL SetEnvironmentVariableW(LPCWSTR name, LPCWSTR value) {
    char *n = utf8_of(name), *v = value ? utf8_of(value) : NULL;
    int r = n ? (value ? setenv(n, v ? v : "", 1) : unsetenv(n)) : -1;
    free(n); free(v);
    return r == 0;
}
static CompatHandle std_handles[3] = { { H_FILE, 0, 0 }, { H_FILE, 1, 0 }, { H_FILE, 2, 0 } };
HANDLE GetStdHandle(DWORD which) {
    return (HANDLE)&std_handles[which == STD_INPUT_HANDLE ? 0 : which == STD_ERROR_HANDLE ? 2 : 1];
}
DWORD GetFileType(HANDLE h) {
    int fd = compat_fd(h);
    struct stat st;
    if (fd < 0 || fstat(fd, &st)) return FILE_TYPE_UNKNOWN;
    return S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode) ? FILE_TYPE_PIPE : FILE_TYPE_DISK;
}
BOOL AttachConsole(DWORD pid) { (void)pid; return FALSE; }
BOOL FreeConsole(void) { return TRUE; }
/* "open" a folder, file or web address with the desktop's choice (xdg-open). */
HINSTANCE ShellExecuteW(HWND w, LPCWSTR op, LPCWSTR file, LPCWSTR params, LPCWSTR dir, int show) {
    (void)w; (void)op; (void)params; (void)dir; (void)show;
    if (!file) return (HINSTANCE)(intptr_t)2;
    char *target = wcsstr(file, L"://") ? utf8_of(file) : path_of(file);
    if (!target) return (HINSTANCE)(intptr_t)8;
    const char *tool = access("/usr/bin/xdg-open", X_OK) == 0 ? "/usr/bin/xdg-open" : "/bin/xdg-open";
    char *argv[] = { (char *)tool, target, NULL };
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    pid_t pid;
    int r = posix_spawn(&pid, tool, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    free(target);
    if (r) return (HINSTANCE)(intptr_t)2;
    /* xdg-open returns at once (it hands the file over): don't leave it a zombie. */
    for (int i = 0; i < 50; i++) { int st; if (waitpid(pid, &st, WNOHANG) == pid) break; Sleep(20); }
    return (HINSTANCE)(intptr_t)42;
}

/* ---- Files and paths ---------------------------------------------------------------------- */
BOOL SetFileAttributesW(LPCWSTR path, DWORD attributes) {
    char *p = path_of(path);
    struct stat st;
    BOOL ok = p && !stat(p, &st);
    if (ok && !S_ISDIR(st.st_mode)) chmod(p, (attributes & FILE_ATTRIBUTE_READONLY) ? (st.st_mode & ~0222u) : (st.st_mode | S_IWUSR));
    free(p);
    return ok;
}
BOOL GetFileSizeEx(HANDLE f, LARGE_INTEGER *size) {
    int fd = compat_fd(f); struct stat st;
    if (fd < 0 || fstat(fd, &st) || !size) return FALSE;
    size->QuadPart = st.st_size;
    return TRUE;
}
BOOL SetFilePointerEx(HANDLE f, LARGE_INTEGER to, LARGE_INTEGER *now, DWORD how) {
    int fd = compat_fd(f);
    off_t r = fd < 0 ? -1 : lseek(fd, (off_t)to.QuadPart, how == FILE_END ? SEEK_END : how == FILE_CURRENT ? SEEK_CUR : SEEK_SET);
    if (r < 0) return FALSE;
    if (now) now->QuadPart = r;
    return TRUE;
}
BOOL GetDiskFreeSpaceExW(LPCWSTR path, ULARGE_INTEGER *avail, ULARGE_INTEGER *total, ULARGE_INTEGER *free_) {
    char *p = path_of(path);
    struct statvfs v;
    int r = -1;
    /* the folder may not exist yet: ask its nearest existing parent */
    for (char *cut; p && (r = statvfs(p, &v)) != 0 && (cut = strrchr(p, '/')) != NULL;) { if (cut == p) { p[1] = 0; } else *cut = 0; if (!strcmp(p, "/")) { r = statvfs(p, &v); break; } }
    free(p);
    if (r) return FALSE;
    if (avail) avail->QuadPart = (ULONGLONG)v.f_bavail * v.f_frsize;
    if (total) total->QuadPart = (ULONGLONG)v.f_blocks * v.f_frsize;
    if (free_) free_->QuadPart = (ULONGLONG)v.f_bfree * v.f_frsize;
    return TRUE;
}
BOOL GetVolumePathNameW(LPCWSTR path, LPWSTR out, DWORD cap) { (void)path; if (!out || cap < 2) return FALSE; wcscpy(out, L"/"); return TRUE; }
BOOL CopyFileExW(LPCWSTR from, LPCWSTR to, LPPROGRESS_ROUTINE progress, LPVOID data, BOOL *cancel, DWORD flags) {
    char *a = path_of(from), *b = path_of(to);
    int in = a ? open(a, O_RDONLY | O_CLOEXEC) : -1;
    struct stat st;
    int out = -1;
    if (in >= 0 && !fstat(in, &st)) out = open(b, O_WRONLY | O_CREAT | O_CLOEXEC | ((flags & COPY_FILE_FAIL_IF_EXISTS) ? O_EXCL : O_TRUNC), 0644);
    free(a);
    BOOL ok = in >= 0 && out >= 0;
    static char buf[1 << 20];
    LARGE_INTEGER total, done, zero; total.QuadPart = ok ? st.st_size : 0; done.QuadPart = 0; zero.QuadPart = 0;
    while (ok) {
        ssize_t n = read(in, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { ok = FALSE; break; }
        if (n == 0) break;
        for (ssize_t w = 0; w < n;) { ssize_t k = write(out, buf + w, (size_t)(n - w)); if (k < 0 && errno == EINTR) continue; if (k <= 0) { ok = FALSE; break; } w += k; }
        done.QuadPart += n;
        if (progress && progress(total, done, zero, zero, 1, 0, NULL, NULL, data) != PROGRESS_CONTINUE) ok = FALSE;
        if (cancel && *cancel) ok = FALSE;
    }
    if (ok && out >= 0 && fsync(out)) ok = FALSE;
    if (in >= 0) close(in);
    if (out >= 0 && close(out)) ok = FALSE;
    if (!ok && out >= 0 && b) unlink(b);
    free(b);
    return ok;
}
LONG CompareFileTime(const FILETIME *a, const FILETIME *b) {
    uint64_t x = (uint64_t)a->dwHighDateTime << 32 | a->dwLowDateTime, y = (uint64_t)b->dwHighDateTime << 32 | b->dwLowDateTime;
    return x < y ? -1 : x > y ? 1 : 0;
}
static int64_t filetime_seconds(const FILETIME *f) { return (int64_t)(((uint64_t)f->dwHighDateTime << 32 | f->dwLowDateTime) / 10000000ull) - 11644473600ll; }
BOOL FileTimeToLocalFileTime(const FILETIME *in, FILETIME *out) {
    time_t t = (time_t)filetime_seconds(in); struct tm tm;
    if (!localtime_r(&t, &tm)) return FALSE;
    uint64_t v = ((uint64_t)in->dwHighDateTime << 32 | in->dwLowDateTime) + (uint64_t)((int64_t)tm.tm_gmtoff * 10000000ll);
    out->dwLowDateTime = (DWORD)v; out->dwHighDateTime = (DWORD)(v >> 32);
    return TRUE;
}
BOOL FileTimeToSystemTime(const FILETIME *in, SYSTEMTIME *out) {
    uint64_t v = (uint64_t)in->dwHighDateTime << 32 | in->dwLowDateTime;
    time_t t = (time_t)filetime_seconds(in); struct tm tm;
    if (!gmtime_r(&t, &tm) || !out) return FALSE;
    out->wYear = (WORD)(tm.tm_year + 1900); out->wMonth = (WORD)(tm.tm_mon + 1); out->wDayOfWeek = (WORD)tm.tm_wday;
    out->wDay = (WORD)tm.tm_mday; out->wHour = (WORD)tm.tm_hour; out->wMinute = (WORD)tm.tm_min; out->wSecond = (WORD)tm.tm_sec;
    out->wMilliseconds = (WORD)(v / 10000 % 1000);
    return TRUE;
}
int SHCreateDirectoryExW(HWND w, LPCWSTR path, const SECURITY_ATTRIBUTES *sa) {
    (void)w; (void)sa;
    char *p = path_of(path);
    if (!p) return ERROR_PATH_NOT_FOUND;
    int existed = 0;
    for (char *s = p + 1; ; s++) {
        if (*s == '/' || !*s) {
            char c = *s; *s = 0;
            if (mkdir(p, 0755) && errno != EEXIST) { free(p); return ERROR_PATH_NOT_FOUND; }
            if (!c) existed = errno == EEXIST;
            *s = c;
            if (!c) break;
        }
    }
    free(p);
    return existed ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS;
}
HRESULT SHGetFolderPathW(HWND w, int csidl, HANDLE token, DWORD flags, LPWSTR out) {
    (void)w; (void)token; (void)flags;
    const char *home = getenv("HOME");
    if (!out || !home) return E_FAIL;
    char buf[PATH_MAX];
    if ((csidl & 0xff) == CSIDL_LOCAL_APPDATA) { const char *x = getenv("XDG_DATA_HOME"); if (x && *x == '/') snprintf(buf, sizeof buf, "%s", x); else snprintf(buf, sizeof buf, "%s/.local/share", home); }
    else { snprintf(buf, sizeof buf, "%s/Documents", home); struct stat st; if (stat(buf, &st) || !S_ISDIR(st.st_mode)) snprintf(buf, sizeof buf, "%s", home); }
    copy_wide(out, MAX_PATH, buf);
    return S_OK;
}
static int is_sep(wchar_t c) { return c == L'\\' || c == L'/'; }
BOOL PathRemoveFileSpecW(LPWSTR path) {
    if (!path) return FALSE;
    wchar_t *last = NULL;
    for (wchar_t *p = path; *p; p++) if (is_sep(*p)) last = p;
    if (!last) { BOOL had = path[0] != 0; path[0] = 0; return had; }
    if (last == path) { if (!last[1]) return FALSE; last[1] = 0; return TRUE; }
    *last = 0;
    return TRUE;
}
LPWSTR PathFindFileNameW(LPCWSTR path) {
    const wchar_t *name = path;
    for (const wchar_t *p = path; p && *p; p++) if (is_sep(*p) && p[1]) name = p + 1;
    return (LPWSTR)name;
}
LPWSTR PathFindExtensionW(LPCWSTR path) {
    const wchar_t *name = PathFindFileNameW(path), *dot = NULL;
    for (const wchar_t *p = name; p && *p; p++) { if (*p == L'.') dot = p; else if (*p == L' ') dot = NULL; }
    return (LPWSTR)(dot ? dot : name + wcslen(name));
}
BOOL PathFileExistsW(LPCWSTR path) { return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES; }
BOOL PathIsRootW(LPCWSTR path) { return path && is_sep(path[0]) && !path[1]; }
BOOL PathCompactPathExW(LPWSTR out, LPCWSTR in, UINT max, DWORD flags) {
    (void)flags;
    if (!out || !in || max < 5) return FALSE;
    size_t n = wcslen(in);
    if (n < max) { wcscpy(out, in); return TRUE; }
    const wchar_t *name = PathFindFileNameW(in);
    size_t keep = wcslen(name);
    if (keep + 4 >= max) { wcsncpy(out, name, max - 4); out[max - 4] = 0; wcscat(out, L"..."); return TRUE; }
    size_t head = max - 1 - keep - 4;
    wcsncpy(out, in, head); out[head] = 0; wcscat(out, L"...\\"); wcscat(out, name);
    return TRUE;
}
BOOL CancelIo(HANDLE f) { (void)f; return TRUE; }
BOOL GetOverlappedResult(HANDLE f, OVERLAPPED *o, DWORD *done, BOOL wait) { (void)f; (void)o; (void)wait; if (done) *done = 0; return FALSE; }

/* ---- Clipboard ---------------------------------------------------------------------------- */
typedef struct { int magic; size_t size; } Global;
#define GLOBAL_MAGIC 0x474C4F42
HGLOBAL GlobalAlloc(UINT flags, SIZE_T size) {
    (void)flags;
    Global *g = (Global *)calloc(1, sizeof *g + size + sizeof(wchar_t));
    if (g) { g->magic = GLOBAL_MAGIC; g->size = size; }
    return (HGLOBAL)g;
}
LPVOID GlobalLock(HGLOBAL g) { Global *x = (Global *)g; return x && x->magic == GLOBAL_MAGIC ? (void *)(x + 1) : NULL; }
BOOL GlobalUnlock(HGLOBAL g) { (void)g; return TRUE; }
HGLOBAL GlobalFree(HGLOBAL g) { Global *x = (Global *)g; if (x && x->magic == GLOBAL_MAGIC) { x->magic = 0; free(x); return NULL; } return g; }
wchar_t *rr_linux_clipboard_text(void) {
    if (!sdl_ready || !SDL_HasClipboardText()) return NULL;
    char *t = SDL_GetClipboardText();
    wchar_t *w = t ? wide_of(t) : NULL;
    SDL_free(t);
    return w;
}
BOOL OpenClipboard(HWND w) { (void)w; return sdl_ready; }
BOOL CloseClipboard(void) { return TRUE; }
BOOL EmptyClipboard(void) { return TRUE; }
HANDLE SetClipboardData(UINT format, HANDLE data) {
    if (format != CF_UNICODETEXT) return NULL;
    char *t = utf8_of((const wchar_t *)GlobalLock(data));
    if (t) SDL_SetClipboardText(t);
    free(t);
    GlobalFree(data);
    return data;
}

/* ---- COM, WinHTTP: not on Linux ----------------------------------------------------------- */
const GUID CLSID_WICImagingFactory, IID_IWICImagingFactory, GUID_WICPixelFormat32bppPBGRA, GUID_WICPixelFormat32bppBGRA,
    GUID_WICPixelFormat32bppBGR, GUID_ContainerFormatPng, CLSID_FileOpenDialog, IID_IFileOpenDialog;
HRESULT CoInitializeEx(LPVOID reserved, DWORD flags) { (void)reserved; (void)flags; return S_OK; }
void CoUninitialize(void) {}
HRESULT CoCreateInstance(REFCLSID cls, IUnknown *outer, DWORD ctx, REFIID iid, void **out) { (void)cls; (void)outer; (void)ctx; (void)iid; if (out) *out = NULL; return E_NOTIMPL; }
BOOL WinHttpCrackUrl(LPCWSTR url, DWORD len, DWORD flags, URL_COMPONENTS *u) { (void)url; (void)len; (void)flags; (void)u; return FALSE; }
HINTERNET WinHttpOpen(LPCWSTR agent, DWORD access, LPCWSTR proxy, LPCWSTR bypass, DWORD flags) { (void)agent; (void)access; (void)proxy; (void)bypass; (void)flags; return NULL; }
HINTERNET WinHttpConnect(HINTERNET s, LPCWSTR host, WORD port, DWORD reserved) { (void)s; (void)host; (void)port; (void)reserved; return NULL; }
HINTERNET WinHttpOpenRequest(HINTERNET c, LPCWSTR verb, LPCWSTR path, LPCWSTR version, LPCWSTR referrer, LPCWSTR *types, DWORD flags) { (void)c; (void)verb; (void)path; (void)version; (void)referrer; (void)types; (void)flags; return NULL; }
BOOL WinHttpSetTimeouts(HINTERNET h, int a, int b, int c, int d) { (void)h; (void)a; (void)b; (void)c; (void)d; return FALSE; }
BOOL WinHttpSetOption(HINTERNET h, DWORD option, LPVOID buffer, DWORD len) { (void)h; (void)option; (void)buffer; (void)len; return FALSE; }
BOOL WinHttpAddRequestHeaders(HINTERNET r, LPCWSTR headers, DWORD len, DWORD modifiers) { (void)r; (void)headers; (void)len; (void)modifiers; return FALSE; }
BOOL WinHttpSendRequest(HINTERNET r, LPCWSTR headers, DWORD len, LPVOID data, DWORD data_len, DWORD total, DWORD_PTR context) { (void)r; (void)headers; (void)len; (void)data; (void)data_len; (void)total; (void)context; return FALSE; }
BOOL WinHttpReceiveResponse(HINTERNET r, LPVOID reserved) { (void)r; (void)reserved; return FALSE; }
BOOL WinHttpQueryHeaders(HINTERNET r, DWORD level, LPCWSTR name, LPVOID buffer, DWORD *len, DWORD *index) { (void)r; (void)level; (void)name; (void)buffer; (void)len; (void)index; return FALSE; }
BOOL WinHttpReadData(HINTERNET r, LPVOID buffer, DWORD size, DWORD *read_) { (void)r; (void)buffer; (void)size; if (read_) *read_ = 0; return FALSE; }
BOOL WinHttpCloseHandle(HINTERNET h) { (void)h; return TRUE; }

/* ---- PNG (the project logo) ---------------------------------------------------------------- */
static unsigned be32(const unsigned char *b) { return (unsigned)b[0] << 24 | (unsigned)b[1] << 16 | (unsigned)b[2] << 8 | b[3]; }
static int paeth(int a, int b, int c) { int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c); return pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
uint32_t *rr_linux_load_png(const wchar_t *path, unsigned *ow, unsigned *oh) {
    char *p = path_of(path);
    FILE *f = p ? fopen(p, "rb") : NULL;
    free(p);
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = size > 33 && size < (64l << 20) ? (unsigned char *)malloc((size_t)size) : NULL;
    if (!b || fread(b, 1, (size_t)size, f) != (size_t)size || memcmp(b, "\x89PNG\r\n\x1a\n", 8)) { free(b); fclose(f); return NULL; }
    fclose(f);
    unsigned w = be32(b + 16), h = be32(b + 20), ch = b[25] == 6 ? 4 : b[25] == 2 ? 3 : 0;
    if (b[24] != 8 || !ch || b[28] != 0 || !w || !h || w > 8192 || h > 8192) { free(b); return NULL; }
    size_t zcap = 0, zn = 0; unsigned char *z = NULL;
    for (long q = 8; q + 12 <= size;) {
        unsigned len = be32(b + q);
        if (q + 12 + (long)len > size) break;
        if (!memcmp(b + q + 4, "IDAT", 4)) {
            if (zn + len > zcap) { zcap = (zn + len) * 2; unsigned char *nz = (unsigned char *)realloc(z, zcap); if (!nz) break; z = nz; }
            memcpy(z + zn, b + q + 8, len); zn += len;
        }
        if (!memcmp(b + q + 4, "IEND", 4)) break;
        q += 12 + (long)len;
    }
    free(b);
    size_t stride = (size_t)w * ch, rawn = (stride + 1) * h;
    unsigned char *raw = (unsigned char *)malloc(rawn);
    uLongf got = (uLongf)rawn;
    if (!z || !raw || uncompress(raw, &got, z, (uLong)zn) != Z_OK || got != rawn) { free(z); free(raw); return NULL; }
    free(z);
    uint32_t *px = (uint32_t *)malloc(sizeof(uint32_t) * w * h);
    unsigned char *prev = (unsigned char *)calloc(stride, 1), *cur = (unsigned char *)malloc(stride);
    if (!px || !prev || !cur) { free(px); free(prev); free(cur); free(raw); return NULL; }
    for (unsigned y = 0; y < h; y++) {
        unsigned char ft = raw[y * (stride + 1)];
        const unsigned char *in = raw + y * (stride + 1) + 1;
        for (size_t i = 0; i < stride; i++) {
            int a = i >= ch ? cur[i - ch] : 0, up = prev[i], c = i >= ch ? prev[i - ch] : 0, v = in[i];
            cur[i] = (unsigned char)(ft == 1 ? v + a : ft == 2 ? v + up : ft == 3 ? v + (a + up) / 2 : ft == 4 ? v + paeth(a, up, c) : v);
        }
        for (unsigned x = 0; x < w; x++) {   /* premultiplied BGRA, as WIC's 32bppPBGRA */
            unsigned r = cur[x * ch], g = cur[x * ch + 1], bl = cur[x * ch + 2], al = ch == 4 ? cur[x * ch + 3] : 255;
            px[(size_t)y * w + x] = al << 24 | (r * al + 127) / 255 << 16 | (g * al + 127) / 255 << 8 | (bl * al + 127) / 255;
        }
        memcpy(prev, cur, stride);
    }
    free(prev); free(cur); free(raw);
    *ow = w; *oh = h;
    return px;
}
