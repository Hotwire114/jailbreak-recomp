/* Linux only: the Windows calls declared in linux-compat/windows.h, on POSIX.
 * LGPL-2.1-or-later. Original author code. */
#define _GNU_SOURCE
#include "windows.h"
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static __thread DWORD last_error;
DWORD GetLastError(void) { return last_error; }
void SetLastError(DWORD e) { last_error = e; }
static DWORD from_errno(int e) {
    switch (e) {
    case 0: return ERROR_SUCCESS;
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EACCES: case EPERM: case EROFS: return ERROR_ACCESS_DENIED;
    case EEXIST: return ERROR_ALREADY_EXISTS;
    default: return 31; /* ERROR_GEN_FAILURE */
    }
}
static BOOL fail_errno(void) { last_error = from_errno(errno); return FALSE; }

/* ---- UTF-8 <-> UTF-32 ---------------------------------------------------------------- */
static size_t put_utf8(uint32_t c, char *o) {
    if (c >= 0xD800 && c < 0xE000) c = 0xFFFD;
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | c >> 6); o[1] = (char)(0x80 | (c & 63)); return 2; }
    if (c < 0x10000) { o[0] = (char)(0xE0 | c >> 12); o[1] = (char)(0x80 | (c >> 6 & 63)); o[2] = (char)(0x80 | (c & 63)); return 3; }
    if (c > 0x10FFFF) c = 0xFFFD;
    if (c < 0x10000) return put_utf8(c, o);
    o[0] = (char)(0xF0 | c >> 18); o[1] = (char)(0x80 | (c >> 12 & 63)); o[2] = (char)(0x80 | (c >> 6 & 63)); o[3] = (char)(0x80 | (c & 63));
    return 4;
}
/* One code point from s (n bytes left); *used = bytes eaten (at least 1). */
static uint32_t get_utf8(const unsigned char *s, size_t n, size_t *used) {
    uint32_t c = s[0];
    size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (!len || len > n) { *used = 1; return c < 0x80 ? c : 0xFFFD; }
    if (len == 1) { *used = 1; return c; }
    c &= 0x3F >> (len - 1);
    for (size_t i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) { *used = i; return 0xFFFD; }
        c = c << 6 | (s[i] & 63);
    }
    *used = len;
    return c;
}
int MultiByteToWideChar(UINT cp, DWORD flags, const char *in, int in_len, wchar_t *out, int out_len) {
    (void)cp; (void)flags;
    if (!in) { last_error = 87; return 0; }
    size_t n = in_len < 0 ? strlen(in) + 1 : (size_t)in_len;
    const unsigned char *s = (const unsigned char *)in;
    int count = 0;
    for (size_t i = 0; i < n;) {
        size_t used; uint32_t c = get_utf8(s + i, n - i, &used); i += used;
        if (out_len) {
            if (count >= out_len) { last_error = 122; return 0; } /* ERROR_INSUFFICIENT_BUFFER */
            out[count] = (wchar_t)c;
        }
        count++;
    }
    return count;
}
int WideCharToMultiByte(UINT cp, DWORD flags, const wchar_t *in, int in_len, char *out, int out_len, const char *def, BOOL *used_def) {
    (void)cp; (void)flags; (void)def;
    if (used_def) *used_def = FALSE;
    if (!in) { last_error = 87; return 0; }
    size_t n = in_len < 0 ? wcslen(in) + 1 : (size_t)in_len;
    int count = 0;
    for (size_t i = 0; i < n; i++) {
        char b[4]; size_t k = put_utf8((uint32_t)in[i], b);
        if (out_len) {
            if (count + (int)k > out_len) { last_error = 122; return 0; }
            memcpy(out + count, b, k);
        }
        count += (int)k;
    }
    return count;
}
static char *utf8_dup(const wchar_t *w) {
    if (!w) return NULL;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = (char *)malloc((size_t)n + 1);
    if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}
static wchar_t *wide_dup(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

/* ---- Paths ----------------------------------------------------------------------------- */
/* Windows-style wide path -> POSIX UTF-8 ('\\' -> '/'). "Local\\x"/"Global\\x" are not paths. */
void rr_linux_path(const wchar_t *in, char *out, size_t cap) {
    if (!cap) return;
    out[0] = 0;
    if (!in) return;
    size_t o = 0;
    for (size_t i = 0; in[i]; i++) {
        char b[4]; size_t k = put_utf8(in[i] == L'\\' ? L'/' : (uint32_t)in[i], b);
        if (o + k >= cap) break;
        memcpy(out + o, b, k); o += k;
    }
    out[o] = 0;
}
static char *posix_path(const wchar_t *in) {
    char *s = utf8_dup(in);
    if (s) for (char *p = s; *p; p++) if (*p == '\\') *p = '/';
    return s;
}
/* POSIX UTF-8 -> wide with '\\' separators, into out[size]. Returns length (or needed size). */
static DWORD wide_windows_path(const char *in, LPWSTR out, DWORD size) {
    wchar_t *w = wide_dup(in);
    if (!w) return 0;
    size_t n = wcslen(w);
    for (size_t i = 0; i < n; i++) if (w[i] == L'/') w[i] = L'\\';
    if (n + 1 > size) { free(w); last_error = 122; return (DWORD)(n + 1); }
    memcpy(out, w, (n + 1) * sizeof(wchar_t));
    free(w);
    return (DWORD)n;
}

DWORD GetModuleFileNameW(HMODULE module, LPWSTR out, DWORD size) {
    (void)module;
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0 || !size) { if (size) out[0] = 0; last_error = from_errno(errno); return 0; }
    buf[n] = 0;
    wchar_t *w = wide_dup(buf);
    if (!w) return 0;
    size_t len = wcslen(w);
    for (size_t i = 0; i < len; i++) if (w[i] == L'/') w[i] = L'\\';
    /* Like Windows: truncate to size-1 characters, return size and set the error. */
    if (len + 1 > size) { memcpy(out, w, (size - 1) * sizeof(wchar_t)); out[size - 1] = 0; free(w); last_error = 122; return size; }
    memcpy(out, w, (len + 1) * sizeof(wchar_t));
    free(w);
    return (DWORD)len;
}
HMODULE GetModuleHandleW(LPCWSTR name) { (void)name; return (HMODULE)(uintptr_t)1; }
void *GetProcAddress(HMODULE module, const char *name) { (void)module; (void)name; return NULL; }

DWORD GetFullPathNameW(LPCWSTR in, DWORD size, LPWSTR out, LPWSTR *file_part) {
    if (!in || !in[0]) { last_error = 123; return 0; }
    char *rel = posix_path(in);
    if (!rel) return 0;
    char *abs;
    if (rel[0] == '/') abs = rel;
    else {
        char cwd[4096];
        if (!getcwd(cwd, sizeof cwd)) { free(rel); return fail_errno(); }
        abs = (char *)malloc(strlen(cwd) + strlen(rel) + 2);
        if (!abs) { free(rel); return 0; }
        sprintf(abs, "%s/%s", cwd, rel);
        free(rel);
    }
    /* Fold "//", "." and ".." the way Windows does (no symlink lookups). */
    size_t n = strlen(abs);
    char *norm = (char *)malloc(n + 2);
    if (!norm) { free(abs); return 0; }
    size_t o = 0;
    norm[o++] = '/';
    for (size_t i = 0; i < n;) {
        while (i < n && abs[i] == '/') i++;
        size_t j = i;
        while (j < n && abs[j] != '/') j++;
        size_t len = j - i;
        if (len == 1 && abs[i] == '.') {}
        else if (len == 2 && abs[i] == '.' && abs[i + 1] == '.') {
            if (o > 1) { o--; while (o > 1 && norm[o - 1] != '/') o--; }
        } else if (len) {
            if (o > 1 && norm[o - 1] != '/') norm[o++] = '/';
            memcpy(norm + o, abs + i, len); o += len;
        }
        i = j;
    }
    /* A trailing separator in the input stays, as on Windows; one left by a final ".." goes. */
    if (o > 1 && norm[o - 1] == '/' && !(n && abs[n - 1] == '/')) o--;
    if (n && abs[n - 1] == '/' && o > 1 && norm[o - 1] != '/') norm[o++] = '/';
    norm[o] = 0;
    free(abs);
    DWORD r = wide_windows_path(norm, out, size);
    free(norm);
    if (r < size && file_part) {
        wchar_t *slash = wcsrchr(out, L'\\');
        *file_part = slash && slash[1] ? slash + 1 : NULL;
    }
    return r;
}

static void to_filetime(const struct timespec *t, FILETIME *f) {
    uint64_t v = ((uint64_t)t->tv_sec + 11644473600ull) * 10000000ull + (uint64_t)t->tv_nsec / 100;
    f->dwLowDateTime = (DWORD)v; f->dwHighDateTime = (DWORD)(v >> 32);
}
static DWORD attributes_of(const struct stat *st) {
    DWORD a = S_ISDIR(st->st_mode) ? FILE_ATTRIBUTE_DIRECTORY : 0;
    if (!(st->st_mode & S_IWUSR)) a |= FILE_ATTRIBUTE_READONLY;
    return a ? a : FILE_ATTRIBUTE_NORMAL;
}
DWORD GetFileAttributesW(LPCWSTR path) {
    char *p = posix_path(path);
    struct stat st;
    int r = p ? stat(p, &st) : -1;
    free(p);
    if (r) { fail_errno(); return INVALID_FILE_ATTRIBUTES; }
    return attributes_of(&st);
}
BOOL GetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS level, void *out) {
    (void)level;
    char *p = posix_path(path);
    struct stat st;
    int r = p ? stat(p, &st) : -1;
    free(p);
    if (r) return fail_errno();
    WIN32_FILE_ATTRIBUTE_DATA *d = (WIN32_FILE_ATTRIBUTE_DATA *)out;
    memset(d, 0, sizeof *d);
    d->dwFileAttributes = attributes_of(&st);
    to_filetime(&st.st_mtim, &d->ftLastWriteTime);
    to_filetime(&st.st_atim, &d->ftLastAccessTime);
    to_filetime(&st.st_ctim, &d->ftCreationTime);
    d->nFileSizeLow = (DWORD)st.st_size; d->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
    return TRUE;
}
#define PATH_CALL(expr, path) do { char *p_ = posix_path(path); if (!p_) return FALSE; int r_ = (expr); free(p_); if (r_) return fail_errno(); return TRUE; } while (0)
BOOL CreateDirectoryW(LPCWSTR path, void *security) { (void)security; PATH_CALL(mkdir(p_, 0777), path); }
BOOL RemoveDirectoryW(LPCWSTR path) { PATH_CALL(rmdir(p_), path); }
BOOL DeleteFileW(LPCWSTR path) { PATH_CALL(unlink(p_), path); }
BOOL SetCurrentDirectoryW(LPCWSTR path) { PATH_CALL(chdir(p_), path); }
int _wremove(const wchar_t *path) { char *p = posix_path(path); int r = p ? remove(p) : -1; free(p); return r; }
int _wrename(const wchar_t *from, const wchar_t *to) {
    char *a = posix_path(from), *b = posix_path(to);
    struct stat st;
    int r = -1;
    /* Windows rename refuses an existing target. */
    if (a && b) { if (!stat(b, &st)) errno = EEXIST; else r = rename(a, b); }
    free(a); free(b);
    return r;
}
static BOOL copy_file(const char *from, const char *to, BOOL fail_if_exists) {
    int in = open(from, O_RDONLY | O_CLOEXEC);
    if (in < 0) return fail_errno();
    struct stat st, dst;
    if (fstat(in, &st)) { close(in); return fail_errno(); }
    /* Onto itself: Windows refuses (sharing violation); O_TRUNC here would empty the file. */
    if (!stat(to, &dst) && dst.st_dev == st.st_dev && dst.st_ino == st.st_ino) { close(in); last_error = 32; return FALSE; }
    int out = open(to, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | (fail_if_exists ? O_EXCL : 0), st.st_mode & 0777);
    if (out < 0) { close(in); return fail_errno(); }
    char buf[65536];
    BOOL ok = TRUE;
    for (;;) {
        ssize_t n = read(in, buf, sizeof buf);
        if (n < 0) { if (errno == EINTR) continue; ok = FALSE; break; }
        if (!n) break;
        for (ssize_t done = 0; done < n;) {
            ssize_t w = write(out, buf + done, (size_t)(n - done));
            if (w < 0) { if (errno == EINTR) continue; ok = FALSE; break; }
            done += w;
        }
        if (!ok) break;
    }
    if (!ok) fail_errno();
    if (close(out)) ok = fail_errno();
    close(in);
    return ok;
}
BOOL CopyFileW(LPCWSTR from, LPCWSTR to, BOOL fail_if_exists) {
    char *a = posix_path(from), *b = posix_path(to);
    BOOL ok = a && b ? copy_file(a, b, fail_if_exists) : FALSE;
    free(a); free(b);
    return ok;
}
BOOL MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags) {
    char *a = posix_path(from), *b = posix_path(to);
    BOOL ok = FALSE;
    if (a && b) {
        struct stat st;
        /* Write-through: the moved file's data, then its new folder entry, reach the disk
         * (fsync of the two, never a whole-system sync). */
        if (flags & MOVEFILE_WRITE_THROUGH) { int fd = open(a, O_RDONLY | O_CLOEXEC); if (fd >= 0) { fsync(fd); close(fd); } }
        if (!(flags & MOVEFILE_REPLACE_EXISTING) && !stat(b, &st)) last_error = ERROR_ALREADY_EXISTS;
        else if (!rename(a, b)) ok = TRUE;
        else if (errno == EXDEV && (flags & MOVEFILE_COPY_ALLOWED)) ok = copy_file(a, b, FALSE) && !unlink(a);
        else fail_errno();
        if (ok && (flags & MOVEFILE_WRITE_THROUGH)) {
            char *slash = strrchr(b, '/');
            if (slash) { *slash = 0; int dir = open(slash == b ? "/" : b, O_RDONLY | O_DIRECTORY | O_CLOEXEC); if (dir >= 0) { fsync(dir); close(dir); } *slash = '/'; }
        }
    }
    free(a); free(b);
    return ok;
}
BOOL MoveFileW(LPCWSTR from, LPCWSTR to) { return MoveFileExW(from, to, MOVEFILE_COPY_ALLOWED); }

typedef struct { DIR *dir; char *folder; char *pattern; } Find;
static BOOL find_fill(Find *f, WIN32_FIND_DATAW *out) {
    struct dirent *e;
    while ((e = readdir(f->dir))) {
        if (fnmatch(f->pattern, e->d_name, FNM_CASEFOLD)) continue;
        memset(out, 0, sizeof *out);
        size_t need = strlen(f->folder) + strlen(e->d_name) + 2;
        char *full = (char *)malloc(need);
        struct stat st;
        if (full) {
            snprintf(full, need, "%s/%s", f->folder, e->d_name);
            if (!stat(full, &st)) {
                out->dwFileAttributes = attributes_of(&st);
                to_filetime(&st.st_mtim, &out->ftLastWriteTime);
                out->nFileSizeLow = (DWORD)st.st_size; out->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
            }
            free(full);
        }
        MultiByteToWideChar(CP_UTF8, 0, e->d_name, -1, out->cFileName, MAX_PATH);
        out->cFileName[MAX_PATH - 1] = 0;
        return TRUE;
    }
    last_error = 18; /* ERROR_NO_MORE_FILES */
    return FALSE;
}
HANDLE FindFirstFileW(LPCWSTR pattern, WIN32_FIND_DATAW *out) {
    char *p = posix_path(pattern);
    if (!p) return INVALID_HANDLE_VALUE;
    Find *f = (Find *)calloc(1, sizeof *f);
    char *slash = strrchr(p, '/');
    if (slash) { *slash = 0; f->folder = strdup(slash == p ? "/" : p); f->pattern = strdup(slash + 1); }
    else { f->folder = strdup("."); f->pattern = strdup(p); }
    free(p);
    /* "*.*" on Windows also matches names without a dot. */
    if (!strcmp(f->pattern, "*.*")) f->pattern[1] = 0;
    f->dir = opendir(f->folder);
    if (!f->dir || !find_fill(f, out)) {
        if (!f->dir) fail_errno(); else last_error = ERROR_FILE_NOT_FOUND;
        if (f->dir) closedir(f->dir);
        free(f->folder); free(f->pattern); free(f);
        return INVALID_HANDLE_VALUE;
    }
    return (HANDLE)f;
}
BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW *out) { return h && h != INVALID_HANDLE_VALUE ? find_fill((Find *)h, out) : FALSE; }
BOOL FindClose(HANDLE h) {
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    Find *f = (Find *)h;
    closedir(f->dir); free(f->folder); free(f->pattern); free(f);
    return TRUE;
}
DWORD GetTempPathW(DWORD size, LPWSTR out) {
    const char *t = getenv("TMPDIR");
    char buf[4096];
    snprintf(buf, sizeof buf, "%s/", t && *t ? t : "/tmp");
    return wide_windows_path(buf, out, size);
}
DWORD GetCurrentDirectoryW(DWORD size, LPWSTR out) {
    char buf[4096];
    if (!getcwd(buf, sizeof buf)) { fail_errno(); return 0; }
    return wide_windows_path(buf, out, size);
}

FILE *_wfopen(const wchar_t *path, const wchar_t *mode) {
    char *p = posix_path(path);
    char m[16];
    size_t o = 0;
    /* "ccs=UTF-8" and other Microsoft extras are dropped; 'b' and 't' mean nothing here. */
    for (size_t i = 0; mode && mode[i] && mode[i] != L',' && o + 1 < sizeof m; i++)
        if (mode[i] != L't' && mode[i] != L' ') m[o++] = (char)mode[i];
    m[o] = 0;
    FILE *f = p ? fopen(p, m) : NULL;
    free(p);
    return f;
}
wchar_t *_wgetenv(const wchar_t *name) {
    /* One slot per variable name. A pointer handed out stays valid for the process lifetime
     * (as on Windows): an unchanged value returns the same copy, a changed one gets a new copy
     * and the old one is kept (the PC code reads a few variables, rarely). */
    static struct { char *name; wchar_t *value; } slots[32];
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    char *n = utf8_dup(name);
    if (!n) return NULL;
    const char *v = getenv(n);
    wchar_t *result = NULL;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < 32; i++) {
        if (slots[i].name && strcmp(slots[i].name, n)) continue;
        if (!v) break;
        if (!slots[i].name) slots[i].name = strdup(n);
        wchar_t *now = wide_dup(v);
        if (slots[i].value && now && !wcscmp(slots[i].value, now)) free(now);
        else if (now) slots[i].value = now;
        result = slots[i].value;
        break;
    }
    pthread_mutex_unlock(&lock);
    free(n);
    return result;
}
static wchar_t *command_line;
static void make_command_line(void);
LPWSTR GetCommandLineW(void) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, make_command_line);
    return command_line;
}
static void make_command_line(void) {
    wchar_t *line = NULL;
    FILE *f = fopen("/proc/self/cmdline", "rb");
    char buf[32768];
    size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    if (f) fclose(f);
    buf[n] = 0;   /* a longer command line is cut: the last argument still ends */
    char out[65536];
    size_t o = 0;
    for (size_t i = 0; i < n && o + 4 < sizeof out;) {
        const char *arg = buf + i;
        size_t len = strlen(arg);
        int quote = !len || strpbrk(arg, " \t") != NULL;
        if (o) out[o++] = ' ';
        if (quote) out[o++] = '"';
        for (size_t k = 0; k < len && o + 3 < sizeof out; k++) out[o++] = arg[k];
        if (quote) out[o++] = '"';
        i += len + 1;
    }
    out[o] = 0;
    line = wide_dup(out);
    if (!line) { static wchar_t empty[1]; line = empty; }
    command_line = line;
}

/* ---- Handles: plain files and shared memory --------------------------------------------- */
enum { H_FILE = 0x46494C45, H_MAP = 0x4D415020 };
typedef struct { int kind; int fd; size_t size; } Handle;
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, void *security, DWORD disposition, DWORD flags, HANDLE tmpl) {
    (void)share; (void)security; (void)flags; (void)tmpl;
    char *p = posix_path(path);
    if (!p) return INVALID_HANDLE_VALUE;
    int mode = (access & GENERIC_WRITE) ? ((access & GENERIC_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
    if (disposition == CREATE_ALWAYS) mode |= O_CREAT | O_TRUNC;
    else if (disposition == 1) mode |= O_CREAT | O_EXCL;      /* CREATE_NEW */
    else if (disposition == 4) mode |= O_CREAT;               /* OPEN_ALWAYS */
    else if (disposition == 5) mode |= O_TRUNC;               /* TRUNCATE_EXISTING */
    int fd = open(p, mode | O_CLOEXEC, 0666);
    free(p);
    if (fd < 0) { fail_errno(); return INVALID_HANDLE_VALUE; }
    Handle *h = (Handle *)calloc(1, sizeof *h);
    if (!h) { close(fd); return INVALID_HANDLE_VALUE; }
    h->kind = H_FILE; h->fd = fd;
    return (HANDLE)h;
}
static Handle *file_of(HANDLE x) { Handle *h = (Handle *)x; return x && x != INVALID_HANDLE_VALUE && h->kind == H_FILE ? h : NULL; }
BOOL ReadFile(HANDLE file, void *buffer, DWORD size, DWORD *done, OVERLAPPED *ov) {
    (void)ov;
    Handle *h = file_of(file);
    if (done) *done = 0;
    if (!h) { last_error = 6; return FALSE; }
    ssize_t n;
    do n = read(h->fd, buffer, size); while (n < 0 && errno == EINTR);
    if (n < 0) return fail_errno();
    if (done) *done = (DWORD)n;
    return TRUE;
}
BOOL WriteFile(HANDLE file, const void *buffer, DWORD size, DWORD *done, OVERLAPPED *ov) {
    (void)ov;
    Handle *h = file_of(file);
    if (done) *done = 0;
    if (!h) { last_error = 6; return FALSE; }
    size_t total = 0;
    while (total < size) {
        ssize_t n = write(h->fd, (const char *)buffer + total, size - total);
        if (n < 0) { if (errno == EINTR) continue; return fail_errno(); }
        total += (size_t)n;
    }
    if (done) *done = (DWORD)total;
    return TRUE;
}
DWORD GetFileSize(HANDLE file, DWORD *high) {
    Handle *h = file_of(file);
    struct stat st;
    if (!h || fstat(h->fd, &st)) { if (high) *high = 0; return 0xFFFFFFFFu; }
    if (high) *high = (DWORD)((uint64_t)st.st_size >> 32);
    return (DWORD)st.st_size;
}
/* "Local\\Name" / "Global\\Name" -> "/Name" (POSIX shared memory). */
static void shm_name(LPCWSTR name, char *out, size_t cap) {
    const wchar_t *n = name, *slash = wcsrchr(name, L'\\');
    if (slash) n = slash + 1;
    char tmp[256];
    rr_linux_path(n, tmp, sizeof tmp);
    for (char *p = tmp; *p; p++) if (*p == '/') *p = '_';
    snprintf(out, cap, "/%s", tmp);
}
static HANDLE mapping(int fd, size_t size) {
    Handle *h = (Handle *)calloc(1, sizeof *h);
    if (!h) { close(fd); return NULL; }
    h->kind = H_MAP; h->fd = fd; h->size = size;
    return (HANDLE)h;
}
HANDLE CreateFileMappingW(HANDLE file, void *security, DWORD protect, DWORD size_high, DWORD size_low, LPCWSTR name) {
    (void)security; (void)protect;
    size_t size = (size_t)((uint64_t)size_high << 32 | size_low);
    int fd;
    if (name) {
        char n[300];
        shm_name(name, n, sizeof n);
        fd = shm_open(n, O_RDWR | O_CREAT, 0600);
        if (fd < 0) { fail_errno(); return NULL; }
        struct stat st;
        last_error = !fstat(fd, &st) && st.st_size > 0 ? ERROR_ALREADY_EXISTS : 0;
        /* New, or smaller than asked (a stale object from an older version): grow it, so a view of
         * the whole size never reaches past its end (SIGBUS). */
        if ((!last_error || (size_t)st.st_size < size) && ftruncate(fd, (off_t)size)) { close(fd); fail_errno(); return NULL; }
    } else if (file == INVALID_HANDLE_VALUE) {
        fd = memfd_create("rr-map", MFD_CLOEXEC);
        if (fd < 0 || ftruncate(fd, (off_t)size)) { if (fd >= 0) close(fd); fail_errno(); return NULL; }
    } else {
        Handle *f = file_of(file);
        if (!f) { last_error = 6; return NULL; }
        fd = dup(f->fd);
        if (!size) { struct stat st; if (!fstat(fd, &st)) size = (size_t)st.st_size; }
    }
    return mapping(fd, size);
}
HANDLE OpenFileMappingW(DWORD access, BOOL inherit, LPCWSTR name) {
    (void)access; (void)inherit;
    if (!name) return NULL;
    char n[300];
    shm_name(name, n, sizeof n);
    int fd = shm_open(n, O_RDWR, 0600);
    if (fd < 0) { fail_errno(); return NULL; }
    struct stat st;
    size_t size = !fstat(fd, &st) ? (size_t)st.st_size : 0;
    return mapping(fd, size);
}
typedef struct View { void *at; size_t size; struct View *next; } View;
static View *views;
static pthread_mutex_t views_lock = PTHREAD_MUTEX_INITIALIZER;
void *MapViewOfFile(HANDLE m, DWORD access, DWORD offset_high, DWORD offset_low, SIZE_T bytes) {
    Handle *h = (Handle *)m;
    if (!h || h->kind != H_MAP) { last_error = 6; return NULL; }
    off_t offset = (off_t)((uint64_t)offset_high << 32 | offset_low);
    size_t size = bytes ? bytes : h->size - (size_t)offset;
    int prot = PROT_READ | ((access & FILE_MAP_WRITE) || access == FILE_MAP_ALL_ACCESS ? PROT_WRITE : 0);
    void *at = mmap(NULL, size, prot, MAP_SHARED, h->fd, offset);
    if (at == MAP_FAILED) { fail_errno(); return NULL; }
    View *v = (View *)malloc(sizeof *v);
    if (v) {
        v->at = at; v->size = size;
        pthread_mutex_lock(&views_lock); v->next = views; views = v; pthread_mutex_unlock(&views_lock);
    }
    return at;
}
BOOL UnmapViewOfFile(const void *at) {
    pthread_mutex_lock(&views_lock);
    for (View **p = &views; *p; p = &(*p)->next) {
        if ((*p)->at != at) continue;
        View *v = *p; *p = v->next;
        pthread_mutex_unlock(&views_lock);
        munmap(v->at, v->size);
        free(v);
        return TRUE;
    }
    pthread_mutex_unlock(&views_lock);
    return FALSE;
}
BOOL CloseHandle(HANDLE x) {
    if (!x || x == INVALID_HANDLE_VALUE || x == GetCurrentProcess() || x == GetCurrentThread()) return x != NULL && x != INVALID_HANDLE_VALUE;
    Handle *h = (Handle *)x;
    if (h->kind != H_FILE && h->kind != H_MAP) { last_error = 6; return FALSE; }
    close(h->fd);
    h->kind = 0;
    free(h);
    return TRUE;
}

/* ---- Time and process ------------------------------------------------------------------ */
ULONGLONG GetTickCount64(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (ULONGLONG)t.tv_sec * 1000u + (ULONGLONG)t.tv_nsec / 1000000u;
}
DWORD GetTickCount(void) { return (DWORD)GetTickCount64(); }
void Sleep(DWORD ms) {
    struct timespec t = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
    while (nanosleep(&t, &t) && errno == EINTR) {}
}
BOOL QueryPerformanceCounter(LARGE_INTEGER *out) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    out->QuadPart = (LONGLONG)t.tv_sec * 1000000000LL + t.tv_nsec;
    return TRUE;
}
BOOL QueryPerformanceFrequency(LARGE_INTEGER *out) { out->QuadPart = 1000000000LL; return TRUE; }
static void fill_systemtime(const struct tm *tm, long ns, SYSTEMTIME *s) {
    s->wYear = (WORD)(tm->tm_year + 1900); s->wMonth = (WORD)(tm->tm_mon + 1); s->wDayOfWeek = (WORD)tm->tm_wday;
    s->wDay = (WORD)tm->tm_mday; s->wHour = (WORD)tm->tm_hour; s->wMinute = (WORD)tm->tm_min;
    s->wSecond = (WORD)tm->tm_sec; s->wMilliseconds = (WORD)(ns / 1000000L);
}
void GetLocalTime(SYSTEMTIME *s) {
    struct timespec t; struct tm tm;
    clock_gettime(CLOCK_REALTIME, &t); localtime_r(&t.tv_sec, &tm); fill_systemtime(&tm, t.tv_nsec, s);
}
void GetSystemTime(SYSTEMTIME *s) {
    struct timespec t; struct tm tm;
    clock_gettime(CLOCK_REALTIME, &t); gmtime_r(&t.tv_sec, &tm); fill_systemtime(&tm, t.tv_nsec, s);
}
DWORD GetCurrentProcessId(void) { return (DWORD)getpid(); }
HANDLE GetCurrentProcess(void) { return (HANDLE)(intptr_t)-1; }
HANDLE GetCurrentThread(void) { return (HANDLE)(intptr_t)-2; }
BOOL GetProcessAffinityMask(HANDLE p, DWORD_PTR *a, DWORD_PTR *s) { (void)p; if (a) *a = 0; if (s) *s = 0; return FALSE; }

/* ---- INI files ------------------------------------------------------------------------- */
/* The whole file as wide text (UTF-8, or UTF-16 LE/BE with a BOM). NULL when missing. */
static wchar_t *ini_load(LPCWSTR path, size_t *len) {
    char *p = posix_path(path);
    FILE *f = p ? fopen(p, "rb") : NULL;
    free(p);
    if (!f) return NULL;
    size_t cap = 4096, n = 0;
    unsigned char *b = (unsigned char *)malloc(cap);
    for (size_t r; b && (r = fread(b + n, 1, cap - n, f)) > 0;) {
        n += r;
        if (n == cap) { unsigned char *nb = (unsigned char *)realloc(b, cap *= 2); if (!nb) { free(b); b = NULL; } else b = nb; }
    }
    fclose(f);
    if (!b) return NULL;
    wchar_t *w = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
    size_t o = 0;
    if (w) {
        if (n >= 2 && ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF))) {
            int le = b[0] == 0xFF;
            for (size_t i = 2; i + 1 < n; i += 2) {
                uint32_t c = le ? (uint32_t)(b[i] | b[i + 1] << 8) : (uint32_t)(b[i] << 8 | b[i + 1]);
                if (c >= 0xD800 && c < 0xDC00 && i + 3 < n) {
                    uint32_t d = le ? (uint32_t)(b[i + 2] | b[i + 3] << 8) : (uint32_t)(b[i + 2] << 8 | b[i + 3]);
                    if (d >= 0xDC00 && d < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); i += 2; }
                }
                w[o++] = (wchar_t)c;
            }
        } else {
            size_t i = n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF ? 3 : 0;
            while (i < n) { size_t used; w[o++] = (wchar_t)get_utf8(b + i, n - i, &used); i += used; }
        }
        w[o] = 0;
    }
    free(b);
    *len = o;
    return w;
}
typedef struct { const wchar_t *s; size_t n; } Span;
static Span trim(const wchar_t *s, size_t n) {
    while (n && iswspace((wint_t)*s)) { s++; n--; }
    while (n && iswspace((wint_t)s[n - 1])) n--;
    Span r = { s, n };
    return r;
}
static int span_is(Span a, const wchar_t *b) { size_t n = wcslen(b); return a.n == n && !wcsncasecmp(a.s, b, n); }
/* Only the first [section] of a name counts (as on Windows): 0 not reached, 1 inside, 2 past. */
static int section_step(int matched, const wchar_t *section, Span name) {
    if (matched == 1) return 2;
    return !matched && section && span_is(name, section) ? 1 : matched;
}
/* Line i of the text: [start, end) without the line break; returns the next line's start. */
static size_t line_at(const wchar_t *t, size_t len, size_t i, size_t *end) {
    size_t e = i;
    while (e < len && t[e] != L'\n' && t[e] != L'\r') e++;
    *end = e;
    if (e < len && t[e] == L'\r') e++;
    if (e < len && t[e] == L'\n') e++;
    return e;
}
static int line_section(const wchar_t *t, size_t a, size_t b, Span *name) {
    Span l = trim(t + a, b - a);
    if (!l.n || l.s[0] != L'[') return 0;
    const wchar_t *close = NULL;
    for (size_t i = 1; i < l.n; i++) if (l.s[i] == L']') { close = l.s + i; break; }
    if (!close) return 0;
    *name = trim(l.s + 1, (size_t)(close - l.s - 1));
    return 1;
}
static int line_key(const wchar_t *t, size_t a, size_t b, Span *key, Span *value) {
    Span l = trim(t + a, b - a);
    if (!l.n || l.s[0] == L';' || l.s[0] == L'[') return 0;
    size_t eq = 0;
    while (eq < l.n && l.s[eq] != L'=') eq++;
    if (eq == l.n) { *key = l; value->s = l.s + l.n; value->n = 0; return 2; } /* a line without '=' */
    *key = trim(l.s, eq);
    *value = trim(l.s + eq + 1, l.n - eq - 1);
    return 1;
}
typedef struct { wchar_t *s; size_t n, cap; } Text;
static void text_insert(Text *t, size_t at, const wchar_t *s, size_t n) {
    if (t->n + n + 1 > t->cap) {
        size_t cap = (t->n + n + 1) * 2;
        wchar_t *ns = (wchar_t *)realloc(t->s, cap * sizeof(wchar_t));
        if (!ns) return;
        t->s = ns; t->cap = cap;
    }
    wmemmove(t->s + at + n, t->s + at, t->n - at);
    wmemcpy(t->s + at, s, n);
    t->n += n;
    t->s[t->n] = 0;
}
static void text_add(Text *t, const wchar_t *s, size_t n) { text_insert(t, t->n, s, n); }
static void text_name(Text *t, Span s) { text_add(t, s.s, s.n); text_add(t, L"", 1); } /* name + 0 */
/* A double-zero list into out[size] with the Windows rules: if it does not fit, size-2
 * characters are kept, the buffer ends in two zeros and the result is size-2. */
static DWORD list_out(const Text *t, wchar_t *out, DWORD size) {
    if (size < 2) { if (size) out[0] = 0; return 0; }
    if (!t->n) { out[0] = 0; return 0; }
    if (t->n + 1 < size) { wmemcpy(out, t->s, t->n); out[t->n] = 0; return (DWORD)t->n; } /* an exact fit counts as cut */
    wmemcpy(out, t->s, size - 2);
    out[size - 2] = 0; out[size - 1] = 0;
    return size - 2;
}
static DWORD ini_get_string(LPCWSTR section, LPCWSTR key, LPCWSTR def, LPWSTR out, DWORD size, LPCWSTR path) {
    if (!out || !size) return 0;
    size_t len = 0;
    wchar_t *t = path ? ini_load(path, &len) : NULL;
    Text list = { 0, 0, 0 };
    int in_section = 0, found = 0, matched = 0;
    Span value = { 0, 0 };
    for (size_t i = 0, e; t && i < len && !found;) {
        size_t next = line_at(t, len, i, &e);
        Span name, k, v;
        if (line_section(t, i, e, &name)) {
            matched = section_step(matched, section, name);
            in_section = matched == 1;
            if (!section) text_name(&list, name);
        } else if (in_section && line_key(t, i, e, &k, &v) == 1) {
            if (!key) text_name(&list, k);
            else if (span_is(k, key)) { value = v; found = 1; }
        }
        i = next;
    }
    /* A list from a missing file, or the keys of a missing section, is the default. */
    if ((!section || !key) && t && (!section || matched != 0)) {
        DWORD r = list_out(&list, out, size);
        free(list.s); free(t);
        return r;
    }
    if (found) {
        if (value.n >= 2 && (value.s[0] == L'"' || value.s[0] == L'\'') && value.s[value.n - 1] == value.s[0]) { value.s++; value.n -= 2; }
    } else {
        const wchar_t *d = def ? def : L"";
        size_t n = wcslen(d);
        while (n && d[n - 1] == L' ') n--;
        value.s = d; value.n = n;
    }
    size_t n = value.n < size - 1 ? value.n : size - 1;
    wmemcpy(out, value.s, n);
    out[n] = 0;
    free(t);
    return (DWORD)n;
}
static UINT ini_get_int(LPCWSTR section, LPCWSTR key, INT def, LPCWSTR path) {
    wchar_t buf[64];
    static const wchar_t missing[] = L"\x1F\x1Fmissing";
    ini_get_string(section, key, missing, buf, 64, path);
    if (!wcscmp(buf, missing) || !buf[0]) return (UINT)def; /* an empty value is the default too */
    const wchar_t *s = buf;
    while (iswspace((wint_t)*s)) s++;
    int neg = 0;
    if (*s == L'-' || *s == L'+') neg = *s++ == L'-';
    unsigned long v = s[0] == L'0' && (s[1] == L'x' || s[1] == L'X') ? wcstoul(s + 2, NULL, 16) : wcstoul(s, NULL, 10);
    return neg ? (UINT)0 - (UINT)v : (UINT)v;
}
static DWORD ini_get_section(LPCWSTR section, LPWSTR out, DWORD size, LPCWSTR path) {
    if (!out || !size) return 0;
    size_t len = 0;
    wchar_t *t = path ? ini_load(path, &len) : NULL;
    Text list = { 0, 0, 0 };
    int in_section = 0, matched = 0;
    for (size_t i = 0, e; t && i < len;) {
        size_t next = line_at(t, len, i, &e);
        Span name;
        if (line_section(t, i, e, &name)) { matched = section_step(matched, section, name); in_section = matched == 1; }
        else if (in_section) {
            /* Windows hands back "key=value" with the spaces round both dropped. */
            Span k, v;
            int kind = line_key(t, i, e, &k, &v);
            if (kind == 1) { text_add(&list, k.s, k.n); text_add(&list, L"=", 1); text_add(&list, v.s, v.n); text_add(&list, L"", 1); }
            else if (kind == 2) text_name(&list, k);
        }
        i = next;
    }
    DWORD r = list_out(&list, out, size);
    free(list.s); free(t);
    return r;
}
static int ini_save(LPCWSTR path, const wchar_t *t, size_t len) {
    char *p = posix_path(path);
    if (!p) return 0;
    size_t pn = strlen(p);
    char *tmp = (char *)malloc(pn + 32);
    if (!tmp) { free(p); return 0; }
    static unsigned serial;   /* a temp name per write: several processes may write the same file */
    snprintf(tmp, pn + 32, "%s.%d.%u.tmp", p, (int)getpid(), __atomic_add_fetch(&serial, 1, __ATOMIC_RELAXED));
    FILE *f = fopen(tmp, "wb");
    int ok = f != NULL;
    for (size_t i = 0; ok && i < len; i++) { char b[4]; size_t k = put_utf8((uint32_t)t[i], b); ok = fwrite(b, 1, k, f) == k; }
    if (f && fclose(f)) ok = 0;
    if (ok) ok = !rename(tmp, p);
    if (!ok) { fail_errno(); unlink(tmp); }
    free(tmp); free(p);
    return ok;
}
/* Windows rules: a NULL key deletes the section, a NULL value deletes the key, a new key
 * goes after the last line of its section, a new section at the end of the file. */
static BOOL ini_write(LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR path) {
    if (!section || !path) { last_error = 87; return FALSE; }
    size_t len = 0;
    wchar_t *t = ini_load(path, &len);
    Text out = { 0, 0, 0 };
    int in_section = 0, seen = 0, done = 0, matched = 0;
    size_t section_end = 0; /* in out: just after the last non-blank line of our section */
    for (size_t i = 0, e; t && i < len;) {
        size_t next = line_at(t, len, i, &e);
        Span name, k, v;
        if (line_section(t, i, e, &name)) {
            matched = section_step(matched, section, name);
            in_section = matched == 1;
            if (in_section) seen = 1;
            if (!(in_section && !key)) {
                text_add(&out, t + i, e - i); text_add(&out, L"\n", 1);
                if (in_section) section_end = out.n;
            }
        } else if (in_section && !key) {
            /* a line of the section being deleted; Windows keeps its blank lines */
            if (!trim(t + i, e - i).n) { text_add(&out, t + i, e - i); text_add(&out, L"\n", 1); }
        } else if (in_section && !done && key && line_key(t, i, e, &k, &v) == 1 && span_is(k, key)) {
            if (value) {
                /* Windows swaps what follows the '=' up to the old value's end; the rest stays. */
                const wchar_t *eq = t + i;
                while (*eq != L'=') eq++;
                text_add(&out, t + i, (size_t)(eq + 1 - (t + i))); text_add(&out, value, wcslen(value));
                text_add(&out, v.s + v.n, (size_t)(t + e - (v.s + v.n))); text_add(&out, L"\n", 1);
                section_end = out.n;
            }
            done = 1;
        } else {
            text_add(&out, t + i, e - i); text_add(&out, L"\n", 1);
            /* A new key goes after the section's last key line (not after notes or blanks). */
            if (in_section && line_key(t, i, e, &k, &v) == 1) section_end = out.n;
        }
        i = next;
    }
    const int existed = t != NULL;
    free(t);
    if ((!key && !seen) || (key && !value && !done)) { /* nothing to delete; Windows still makes the file */
        free(out.s);
        return existed ? TRUE : ini_save(path, L"", 0);
    }
    if (key && value && !done) {
        Text line = { 0, 0, 0 };
        text_add(&line, key, wcslen(key)); text_add(&line, L"=", 1); text_add(&line, value, wcslen(value)); text_add(&line, L"\n", 1);
        /* After the section's last key line; a section that runs to the end of the file
         * gets it at the very end (blank lines and all), as Windows does. */
        if (seen) text_insert(&out, matched == 1 ? out.n : section_end, line.s, line.n);
        else {
            text_add(&out, L"[", 1); text_add(&out, section, wcslen(section)); text_add(&out, L"]\n", 2);
            text_add(&out, line.s, line.n);
        }
        free(line.s);
    }
    int ok = ini_save(path, out.s ? out.s : L"", out.n);
    free(out.s);
    return ok;
}

/* Windows serialises the profile calls: one lock here, so two threads updating the same
 * file never lose a change (each call reads, edits and writes the whole file). */
static pthread_mutex_t ini_lock = PTHREAD_MUTEX_INITIALIZER;
DWORD GetPrivateProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR def, LPWSTR out, DWORD size, LPCWSTR path) {
    pthread_mutex_lock(&ini_lock); DWORD r = ini_get_string(section, key, def, out, size, path); pthread_mutex_unlock(&ini_lock); return r;
}
UINT GetPrivateProfileIntW(LPCWSTR section, LPCWSTR key, INT def, LPCWSTR path) {
    pthread_mutex_lock(&ini_lock); UINT r = ini_get_int(section, key, def, path); pthread_mutex_unlock(&ini_lock); return r;
}
DWORD GetPrivateProfileSectionW(LPCWSTR section, LPWSTR out, DWORD size, LPCWSTR path) {
    pthread_mutex_lock(&ini_lock); DWORD r = ini_get_section(section, out, size, path); pthread_mutex_unlock(&ini_lock); return r;
}
BOOL WritePrivateProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR path) {
    pthread_mutex_lock(&ini_lock); BOOL r = ini_write(section, key, value, path); pthread_mutex_unlock(&ini_lock); return r;
}

/* ---- SHA-256 ----------------------------------------------------------------------------- */
typedef struct { uint32_t h[8]; uint64_t bits; unsigned char block[64]; size_t used; } Sha256;
static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
#define ROR(x, n) ((x) >> (n) | (x) << (32 - (n)))
static void sha_block(Sha256 *s, const unsigned char *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ w[i - 15] >> 3, s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ w[i - 2] >> 10;
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
enum { H_ALG = 0x414C4720, H_HASH = 0x48415348 };
typedef struct { int kind; Sha256 sha; } HashObj;
static int alg_token = H_ALG;
NTSTATUS BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE *alg, LPCWSTR id, LPCWSTR impl, ULONG flags) {
    (void)impl; (void)flags;
    if (!alg || !id || wcscmp(id, BCRYPT_SHA256_ALGORITHM)) return (NTSTATUS)0xC00000BB; /* STATUS_NOT_SUPPORTED */
    *alg = (BCRYPT_ALG_HANDLE)&alg_token;
    return 0;
}
NTSTATUS BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE alg, ULONG flags) { (void)alg; (void)flags; return 0; }
NTSTATUS BCryptCreateHash(BCRYPT_ALG_HANDLE alg, BCRYPT_HASH_HANDLE *hash, PUCHAR object, ULONG object_size, PUCHAR secret, ULONG secret_size, ULONG flags) {
    (void)object; (void)object_size; (void)flags;
    if (alg != (BCRYPT_ALG_HANDLE)&alg_token || !hash || (secret && secret_size)) return (NTSTATUS)0xC000000D; /* STATUS_INVALID_PARAMETER */
    HashObj *o = (HashObj *)calloc(1, sizeof *o);
    if (!o) return (NTSTATUS)0xC0000017; /* STATUS_NO_MEMORY */
    static const uint32_t init[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    o->kind = H_HASH;
    memcpy(o->sha.h, init, sizeof init);
    *hash = (BCRYPT_HASH_HANDLE)o;
    return 0;
}
NTSTATUS BCryptHashData(BCRYPT_HASH_HANDLE hash, PUCHAR data, ULONG size, ULONG flags) {
    (void)flags;
    HashObj *o = (HashObj *)hash;
    if (!o || o->kind != H_HASH) return (NTSTATUS)0xC0000008; /* STATUS_INVALID_HANDLE */
    Sha256 *s = &o->sha;
    s->bits += (uint64_t)size * 8;
    for (ULONG i = 0; i < size; i++) {
        s->block[s->used++] = data[i];
        if (s->used == 64) { sha_block(s, s->block); s->used = 0; }
    }
    return 0;
}
NTSTATUS BCryptFinishHash(BCRYPT_HASH_HANDLE hash, PUCHAR out, ULONG out_size, ULONG flags) {
    (void)flags;
    HashObj *o = (HashObj *)hash;
    if (!o || o->kind != H_HASH) return (NTSTATUS)0xC0000008;
    if (out_size != 32) return (NTSTATUS)0xC000000D;
    Sha256 *s = &o->sha;
    uint64_t bits = s->bits;
    unsigned char pad = 0x80;
    s->block[s->used++] = pad;
    if (s->used > 56) { while (s->used < 64) s->block[s->used++] = 0; sha_block(s, s->block); s->used = 0; }
    while (s->used < 56) s->block[s->used++] = 0;
    for (int i = 7; i >= 0; i--) s->block[s->used++] = (unsigned char)(bits >> (i * 8));
    sha_block(s, s->block);
    for (int i = 0; i < 8; i++) { out[i * 4] = (UCHAR)(s->h[i] >> 24); out[i * 4 + 1] = (UCHAR)(s->h[i] >> 16); out[i * 4 + 2] = (UCHAR)(s->h[i] >> 8); out[i * 4 + 3] = (UCHAR)s->h[i]; }
    return 0;
}
NTSTATUS BCryptDestroyHash(BCRYPT_HASH_HANDLE hash) {
    HashObj *o = (HashObj *)hash;
    if (!o || o->kind != H_HASH) return (NTSTATUS)0xC0000008;
    o->kind = 0;
    free(o);
    return 0;
}
NTSTATUS BCryptGenRandom(BCRYPT_ALG_HANDLE alg, PUCHAR out, ULONG size, ULONG flags) {
    (void)alg; (void)flags;
    for (ULONG done = 0; done < size;) {
        ssize_t n = getrandom(out + done, size - done, 0);
        if (n < 0) { if (errno == EINTR) continue; return (NTSTATUS)0xC0000001; }
        done += (ULONG)n;
    }
    return 0;
}
