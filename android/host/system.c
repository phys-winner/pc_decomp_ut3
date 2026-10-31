/* advapi32 (registry), shell32/shlwapi (folders, path helpers), winmm (time
 * and multimedia timers), ole32/oleaut32 (COM runtime basics), dbghelp
 * (crash reporting, unavailable) and the small gdi32 surface. */
#define _GNU_SOURCE
#include "host.h"
#include <ctype.h>
#include <errno.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ============================================================== registry */
/* A read-only registry holding exactly the values the game and its
 * libraries read on the validated Windows host. */
#define HKEY_CLASSES_ROOT_ 0x80000000u
#define HKEY_CURRENT_USER_ 0x80000001u
#define HKEY_LOCAL_MACHINE_ 0x80000002u
#define REG_SZ_ 1u
#define REG_DWORD_ 4u

typedef struct { const char *key; const char *name; uint32_t type; uint32_t dword; const char *text; } reg_value;
static const reg_value registry[] = {
    {"HKLM\\Software\\Epic Games\\Unreal Tournament 3", "InstalledDirectory", REG_SZ_, 0, "C:\\Games\\UT3"},
};
static const char *registry_keys[] = {
    "HKLM\\Software\\Epic Games\\Unreal Tournament 3",
    "HKLM\\Software\\Epic Games",
};

/* A registry key has its own handle type. Other native object payloads must
 * never be interpreted as paths or closed by RegCloseKey. */
enum { REGISTRY_KEY_ = 0x524547 };
#define REG_READ_ 0x00020019u
#define REG_VIEWS_ 0x00000300u
#define REG_MAXIMUM_ALLOWED_ 0x02000000u
typedef struct { char path[256]; uint32_t access; } regkey_t;

static const char *root_name(uint32_t root)
{
    switch (root) {
    case HKEY_CLASSES_ROOT_: return "HKCR";
    case HKEY_CURRENT_USER_: return "HKCU";
    case HKEY_LOCAL_MACHINE_: return "HKLM";
    default: return NULL;
    }
}

static const char *reg_path(uint32_t key, uint32_t *access)
{
    const char *root = root_name(key);
    if (root) { if (access) *access = REG_READ_; return root; }
    void_object *object = void_handle_get(key, REGISTRY_KEY_);
    if (!object) return NULL;
    regkey_t *data = object->data;
    if (access) *access = data->access;
    return data->path;
}

static uint32_t reg_string(uint32_t address, int wide, char **text)
{
    *text = NULL;
    if (!address) return 0;
    unsigned width = wide ? 2u : 1u;
    for (uint32_t i = 0; i <= 32767; ++i) {
        uint64_t at = (uint64_t)address + (uint64_t)i * width;
        if (at + width > 0x100000000ull || !void_vm_readable((uint32_t)at, width)) return 998;
        if (wide ? !MEM16((uint32_t)at) : !MEM8((uint32_t)at)) {
            *text = wide ? void_wstr_utf8(address) : void_astr_utf8(address);
            return *text ? 0 : ERROR_NOT_ENOUGH_MEMORY_;
        }
    }
    return ERROR_INVALID_PARAMETER_;
}

static int reg_known_key(const char *path)
{
    if (!strcmp(path, "HKCR") || !strcmp(path, "HKCU") || !strcmp(path, "HKLM")) return 1;
    size_t length = strlen(path);
    for (size_t i = 0; i < sizeof registry_keys / sizeof registry_keys[0]; ++i)
        if (!strncasecmp(registry_keys[i], path, length) &&
            (!registry_keys[i][length] || registry_keys[i][length] == '\\')) return 1;
    return 0;
}

static uint32_t reg_open(uint32_t root, const char *sub, uint32_t out, uint32_t access,
                         int create, uint32_t disposition)
{
    const char *base = reg_path(root, NULL);
    if (!base) return ERROR_INVALID_HANDLE_;
    if (!out || !void_vm_writable(out, 4) || (disposition && !void_vm_writable(disposition, 4))) return 998;
    if ((access & REG_VIEWS_) == REG_VIEWS_) return ERROR_INVALID_PARAMETER_;
    char path[256];
    int length = snprintf(path, sizeof path, "%s%s%s", base, sub && *sub ? "\\" : "", sub ? sub : "");
    if (length < 0 || length >= sizeof path) return 206; /* ERROR_FILENAME_EXCED_RANGE */
    while (length && path[length - 1] == '\\') path[--length] = 0;
    /* This virtual 32-bit registry has one view. Match an entire WOW6432Node
     * component; a key whose name merely starts with it is a different key. */
    char *wow = strcasestr(path, "\\WOW6432Node");
    if (wow && (!wow[12] || wow[12] == '\\')) memmove(wow, wow + 12, strlen(wow + 12) + 1);
    int exists = reg_known_key(path);
    uint32_t requested = access & ~REG_VIEWS_;
    if (requested == REG_MAXIMUM_ALLOWED_) requested = REG_READ_;
    /* No write permission exists in this host's read-only registry. In
     * particular Steam's optional cached-CM cleanup must receive a real
     * access denial and take its original error branch. */
    if ((create && !exists) || (exists && (requested & ~REG_READ_))) {
        VLOG("[void] Reg%sKey(%s, access=%08X): read-only, denied", create ? "Create" : "Open", path, access);
        return ERROR_ACCESS_DENIED_;
    }
    if (!exists) return ERROR_FILE_NOT_FOUND_;
    if (!create && root_name(root) && (!sub || !*sub)) { MEM32(out) = root; return 0; }
    void_object *object = void_object_new(REGISTRY_KEY_, sizeof(regkey_t));
    regkey_t *key = object->data;
    memcpy(key->path, path, strlen(path) + 1); key->access = requested;
    MEM32(out) = void_handle_new(object);
    if (disposition) MEM32(disposition) = 2; /* REG_OPENED_EXISTING_KEY */
    VLOG("[void] Reg%sKey(%s) = %08X", create ? "Create" : "Open", path, MEM32(out));
    return ERROR_SUCCESS_;
}

static uint32_t reg_query(uint32_t key, const char *name, uint32_t type_out, uint32_t data, uint32_t size_ptr, int wide)
{
    uint32_t access;
    const char *path = reg_path(key, &access);
    if (!path) return ERROR_INVALID_HANDLE_;
    if (!(access & 1u)) return ERROR_ACCESS_DENIED_; /* KEY_QUERY_VALUE */
    if (data && !size_ptr) return ERROR_INVALID_PARAMETER_;
    if ((type_out && !void_vm_writable(type_out, 4)) ||
        (size_ptr && (!void_vm_writable(size_ptr, 4) || (data && !void_vm_readable(size_ptr, 4))))) return 998;
    for (size_t i = 0; i < sizeof registry / sizeof registry[0]; ++i) {
        const reg_value *v = &registry[i];
        if (strcasecmp(v->key, path) || strcasecmp(v->name, name ? name : "")) continue;
        uint32_t need = v->type == REG_DWORD_ ? 4u : (uint32_t)((strlen(v->text) + 1) * (wide ? 2 : 1));
        uint32_t cap = data && size_ptr ? MEM32(size_ptr) : 0;
        if (data && cap >= need && !void_vm_writable(data, need)) return 998;
        if (type_out) MEM32(type_out) = v->type;
        if (size_ptr) MEM32(size_ptr) = need;
        if (!data) return ERROR_SUCCESS_;
        if (cap < need) return ERROR_MORE_DATA_;
        if (v->type == REG_DWORD_) MEM32(data) = v->dword;
        else if (wide) void_utf8_to_gw(v->text, data, need / 2);
        else void_utf8_to_ga(v->text, data, need);
        VLOG("[void] RegQueryValue(%s\\%s) served", path, name);
        return ERROR_SUCCESS_;
    }
    return ERROR_FILE_NOT_FOUND_;
}

#define ADV "advapi32.dll"
static uint32_t reg_open_api(int wide, int extended, int create)
{
    if ((create || extended) && ARG(2)) return ERROR_INVALID_PARAMETER_;
    if (create && !ARG(1)) return 1010; /* ERROR_BADKEY, also on native Windows. */
    if (create && (ARG(4) & ~15u)) return ERROR_INVALID_PARAMETER_;
    if (create && ARG(4) > 1u) return ERROR_NOT_SUPPORTED_; /* No link/backup/restore service. */
    char *sub;
    uint32_t result = reg_string(ARG(1), wide, &sub);
    if (!result) result = reg_open(ARG(0), sub, ARG(create ? 7 : extended ? 4 : 2),
        create ? ARG(5) : extended ? ARG(3) : REG_READ_, create, create ? ARG(8) : 0);
    free(sub); return result;
}
WINAPI_EXPORT(ADV, RegOpenKeyExW, 5) { return reg_open_api(1, 1, 0); }
WINAPI_EXPORT(ADV, RegOpenKeyExA, 5) { return reg_open_api(0, 1, 0); }
WINAPI_EXPORT(ADV, RegOpenKeyA, 3) { return reg_open_api(0, 0, 0); }
WINAPI_EXPORT(ADV, RegCreateKeyExA, 9) { return reg_open_api(0, 1, 1); }
WINAPI_EXPORT(ADV, RegCreateKeyExW, 9) { return reg_open_api(1, 1, 1); }
static uint32_t reg_query_api(int wide)
{
    if (ARG(2)) return ERROR_INVALID_PARAMETER_;
    char *name;
    uint32_t result = reg_string(ARG(1), wide, &name);
    if (!result) result = reg_query(ARG(0), name, ARG(3), ARG(4), ARG(5), wide);
    free(name); return result;
}
WINAPI_EXPORT(ADV, RegQueryValueExW, 6) { return reg_query_api(1); }
WINAPI_EXPORT(ADV, RegQueryValueExA, 6) { return reg_query_api(0); }
WINAPI_EXPORT(ADV, RegCloseKey, 1)
{
    if (root_name(ARG(0))) return ERROR_SUCCESS_;
    return void_handle_get(ARG(0), REGISTRY_KEY_) && void_handle_close(ARG(0)) ?
        ERROR_SUCCESS_ : ERROR_INVALID_HANDLE_;
}
WINAPI_EXPORT(ADV, InitializeSecurityDescriptor, 2)
{
    memset(GPTR(ARG(0)), 0, 20);
    MEM8(ARG(0)) = 1;                    /* SECURITY_DESCRIPTOR_REVISION */
    return 1;
}
WINAPI_EXPORT(ADV, SetSecurityDescriptorDacl, 4)
{
    MEM16(ARG(0) + 2) |= 4;              /* SE_DACL_PRESENT */
    MEM32(ARG(0) + 16) = ARG(2);
    return 1;
}

/* ================================================================ shell32 */
#define SH "shell32.dll"
static const char *folder_path(uint32_t csidl)
{
    switch (csidl & 0xFFu) {
    case 0x05: return "C:\\Users\\Player\\Documents";          /* CSIDL_PERSONAL */
    case 0x1A: return "C:\\Users\\Player\\AppData\\Roaming";   /* CSIDL_APPDATA */
    case 0x1C: return "C:\\Users\\Player\\AppData\\Local";     /* CSIDL_LOCAL_APPDATA */
    case 0x23: return "C:\\ProgramData";                       /* CSIDL_COMMON_APPDATA */
    case 0x2E: return "C:\\Users\\Public\\Documents";          /* CSIDL_COMMON_DOCUMENTS */
    case 0x28: return "C:\\Users\\Player";                     /* CSIDL_PROFILE */
    case 0x24: return "C:\\Windows";                           /* CSIDL_WINDOWS */
    case 0x25: return "C:\\Windows\\system32";                 /* CSIDL_SYSTEM */
    case 0x26: return "C:\\Program Files";                     /* CSIDL_PROGRAM_FILES */
    default: return NULL;
    }
}

WINAPI_EXPORT(SH, SHGetFolderPathW, 5)
{
    const char *path = folder_path(ARG(1));
    VLOG("[void] SHGetFolderPathW(%X) = %s", ARG(1), path ? path : "(unsupported)");
    if (!path) return E_INVALIDARG_;
    char *host = void_path_to_host(path);
    if (host) { mkdir(host, 0775); free(host); }
    void_utf8_to_gw(path, ARG(4), 260);
    return S_OK_;
}

WINAPI_EXPORT(SH, SHGetFolderPathA, 5)
{
    const char *path = folder_path(ARG(1));
    if (!path) return E_INVALIDARG_;
    char *host = void_path_to_host(path);
    if (host) { mkdir(host, 0775); free(host); }
    void_utf8_to_ga(path, ARG(4), 260);
    return S_OK_;
}

/* ================================================================ shlwapi */
#define SHL "shlwapi.dll"
/* Path helpers on guest strings; `w` selects UTF-16 (2) or ANSI (1) units. */
static uint32_t unit_at(uint32_t s, uint32_t i, int w) { return w == 2 ? MEM16(s + 2 * i) : MEM8(s + i); }
static void unit_put(uint32_t s, uint32_t i, uint32_t c, int w) { if (w == 2) MEM16(s + 2 * i) = (uint16_t)c; else MEM8(s + i) = (uint8_t)c; }
static uint32_t units_len(uint32_t s, int w) { uint32_t n = 0; while (unit_at(s, n, w)) ++n; return n; }

static char *read_units(uint32_t s, int w) { return w == 2 ? void_wstr_utf8(s) : void_astr_utf8(s); }
static uint32_t write_units(const char *text, uint32_t dst, uint32_t cap, int w)
{
    return w == 2 ? void_utf8_to_gw(text, dst, cap) : void_utf8_to_ga(text, dst, cap);
}

/* PathCanonicalize-style "." / ".." removal, keeping the drive or root. */
static void canonicalize(char *path)
{
    char out[1024];
    size_t n = 0;
    char *parts[128];
    int count = 0;
    int rooted = 0;
    char prefix[8] = "";
    char *p = path;
    if (((p[0] | 0x20) >= 'a' && (p[0] | 0x20) <= 'z') && p[1] == ':') {
        prefix[0] = p[0]; prefix[1] = ':'; prefix[2] = 0;
        p += 2;
    }
    if (*p == '\\') { rooted = 1; ++p; }
    char *save = NULL;
    for (char *tok = strtok_r(p, "\\", &save); tok; tok = strtok_r(NULL, "\\", &save)) {
        if (!strcmp(tok, ".")) continue;
        if (!strcmp(tok, "..")) { if (count) --count; continue; }
        if (count < 128) parts[count++] = tok;
    }
    n += (size_t)snprintf(out + n, sizeof out - n, "%s%s", prefix, rooted ? "\\" : "");
    for (int i = 0; i < count; ++i) n += (size_t)snprintf(out + n, sizeof out - n, "%s%s", i ? "\\" : "", parts[i]);
    strcpy(path, out);
}

static uint32_t path_combine(int w)
{
    uint32_t dst = ARG(0);
    char *dir = read_units(ARG(1), w), *file = read_units(ARG(2), w);
    char combined[1024];
    if (!dir && !file) { free(dir); free(file); return 0; }
    if (!file || !*file) snprintf(combined, sizeof combined, "%s", dir ? dir : "");
    else if (!dir || !*dir || file[0] == '\\' || (file[0] && file[1] == ':'))
        snprintf(combined, sizeof combined, "%s", file);
    else
        snprintf(combined, sizeof combined, "%s%s%s", dir, dir[strlen(dir) - 1] == '\\' ? "" : "\\", file);
    for (char *c = combined; *c; ++c) if (*c == '/') *c = '\\';
    canonicalize(combined);
    free(dir); free(file);
    if (strlen(combined) >= 260) { unit_put(dst, 0, 0, w); return 0; }
    write_units(combined, dst, 260, w);
    return dst;
}

WINAPI_EXPORT(SHL, PathCombineW, 3) { return path_combine(2); }
WINAPI_EXPORT(SHL, PathCombineA, 3) { return path_combine(1); }

static uint32_t find_extension(uint32_t s, int w)
{
    uint32_t n = units_len(s, w), dot = n;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t c = unit_at(s, i, w);
        if (c == '.') dot = i;
        else if (c == '\\' || c == ' ') dot = n;
    }
    return s + dot * (uint32_t)w;
}

static uint32_t find_file_name(uint32_t s, int w)
{
    uint32_t n = units_len(s, w), start = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t c = unit_at(s, i, w);
        if ((c == '\\' || c == ':' || c == '/') && i + 1 < n) start = i + 1;
    }
    return s + start * (uint32_t)w;
}

WINAPI_EXPORT(SHL, PathFindExtensionW, 1) { return find_extension(ARG(0), 2); }
WINAPI_EXPORT(SHL, PathFindExtensionA, 1) { return find_extension(ARG(0), 1); }
WINAPI_EXPORT(SHL, PathFindFileNameW, 1) { return find_file_name(ARG(0), 2); }
WINAPI_EXPORT(SHL, PathFindFileNameA, 1) { return find_file_name(ARG(0), 1); }

WINAPI_EXPORT(SHL, PathRemoveExtensionW, 1)
{
    uint32_t ext = find_extension(ARG(0), 2);
    if (MEM16(ext)) MEM16(ext) = 0;
    return 0;
}

WINAPI_EXPORT(SHL, PathRenameExtensionW, 2)
{
    uint32_t s = ARG(0), ext = find_extension(s, 2);
    uint32_t base_units = (ext - s) / 2, add = units_len(ARG(1), 2);
    if (base_units + add >= 260) return 0;
    for (uint32_t i = 0; i <= add; ++i) MEM16(ext + 2 * i) = MEM16(ARG(1) + 2 * i);
    return 1;
}

static uint32_t remove_file_spec(uint32_t s, int w)
{
    uint32_t n = units_len(s, w);
    int32_t last = -1;
    for (uint32_t i = 0; i < n; ++i) if (unit_at(s, i, w) == '\\') last = (int32_t)i;
    if (last < 0) {
        if (n) { unit_put(s, 0, 0, w); return 1; }
        return 0;
    }
    /* Keep the root separator: "C:\file" -> "C:\". */
    if (last == 2 && unit_at(s, 1, w) == ':') { if (n > 3) { unit_put(s, 3, 0, w); return 1; } return 0; }
    if (last == 0) { if (n > 1) { unit_put(s, 1, 0, w); return 1; } return 0; }
    unit_put(s, (uint32_t)last, 0, w);
    return 1;
}

WINAPI_EXPORT(SHL, PathRemoveFileSpecA, 1) { return remove_file_spec(ARG(0), 1); }
WINAPI_EXPORT(SHL, PathRemoveFileSpecW, 1) { return remove_file_spec(ARG(0), 2); }

/* PathRelativePathTo(out, from, fromAttr, to, toAttr). */
WINAPI_EXPORT(SHL, PathRelativePathToW, 5)
{
    char *from = void_wstr_utf8(ARG(1)), *to = void_wstr_utf8(ARG(3));
    uint32_t from_dir = ARG(2) & 0x10u;
    if (!from || !to) { free(from); free(to); return 0; }
    for (char *c = from; *c; ++c) if (*c == '/') *c = '\\';
    for (char *c = to; *c; ++c) if (*c == '/') *c = '\\';
    if (!from_dir) { char *s = strrchr(from, '\\'); if (s) *s = 0; }
    /* Common prefix by components, case-insensitively. */
    char a[1024], b[1024];
    snprintf(a, sizeof a, "%s\\", from);
    snprintf(b, sizeof b, "%s", to);
    size_t common = 0, i = 0;
    while (a[i] && b[i] && tolower((unsigned char)a[i]) == tolower((unsigned char)b[i])) {
        if (a[i] == '\\') common = i + 1;
        ++i;
    }
    if (!a[i] && (b[i] == '\\' || !b[i])) common = i;
    if (!common) { free(from); free(to); return 0; }
    char out[1024] = "";
    size_t n = 0;
    int ups = 0;
    for (size_t k = common; a[k]; ++k) if (a[k] == '\\') ++ups;
    if (!ups) n += (size_t)snprintf(out + n, sizeof out - n, ".");
    for (int k = 0; k < ups; ++k) n += (size_t)snprintf(out + n, sizeof out - n, "%s..", k ? "\\" : "");
    const char *rest = b + common;
    while (*rest == '\\') ++rest;
    if (*rest) n += (size_t)snprintf(out + n, sizeof out - n, "\\%s", rest);
    void_utf8_to_gw(out, ARG(0), 260);
    free(from); free(to);
    return 1;
}

/* ================================================================== winmm */
#define WINMM "winmm.dll"
WINAPI_EXPORT(WINMM, timeGetTime, 0) { return void_tick_ms(); }
WINAPI_EXPORT(WINMM, timeBeginPeriod, 1) { return 0; }
WINAPI_EXPORT(WINMM, timeEndPeriod, 1) { return 0; }

typedef struct {
    uint32_t id, delay, callback, user, flags;
    volatile int active;
    pthread_t thread;
} mmtimer_t;
static mmtimer_t mm_timers[16];
static pthread_mutex_t mm_lock = PTHREAD_MUTEX_INITIALIZER;

static void *mm_timer_thread(void *arg)
{
    mmtimer_t *t = (mmtimer_t *)arg;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    for (;;) {
        next.tv_nsec += (long)t->delay * 1000000L;
        while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) == EINTR) {}
        if (!t->active) break;
        if (t->flags & 0x30u) {                 /* TIME_CALLBACK_EVENT_SET / PULSE */
            void_object *o = void_handle_get(t->callback, VOID_OBJ_EVENT);
            if (o) void_object_signal(o);
        } else {
            void_machine_enter();
            uint32_t args[5] = { t->id, 0, t->user, 0, 0 };
            void_call_guest(t->callback, 5, args, 1);
            void_machine_leave();
        }
        if (!(t->flags & 1u)) { t->active = 0; break; }   /* TIME_ONESHOT */
    }
    return NULL;
}

WINAPI_EXPORT(WINMM, timeSetEvent, 5)
{
    pthread_mutex_lock(&mm_lock);
    for (uint32_t i = 0; i < 16; ++i) {
        mmtimer_t *t = &mm_timers[i];
        if (t->active) continue;
        t->id = i + 1; t->delay = ARG(0) ? ARG(0) : 1; t->callback = ARG(2); t->user = ARG(3); t->flags = ARG(4);
        t->active = 1;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 8u << 20);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&t->thread, &attr, mm_timer_thread, t);
        pthread_attr_destroy(&attr);
        pthread_mutex_unlock(&mm_lock);
        VLOG("[void] timeSetEvent(%u ms, %08X, flags %X) = %u", t->delay, t->callback, t->flags, t->id);
        return t->id;
    }
    pthread_mutex_unlock(&mm_lock);
    return 0;
}

WINAPI_EXPORT(WINMM, timeKillEvent, 1)
{
    uint32_t id = ARG(0);
    if (!id || id > 16 || !mm_timers[id - 1].active) return 97;   /* MMSYSERR_INVALPARAM */
    mm_timers[id - 1].active = 0;
    return 0;
}

/* ============================================================== ole32 */
#define OLE "ole32.dll"
WINAPI_EXPORT(OLE, CoInitialize, 1) { return S_OK_; }
WINAPI_EXPORT(OLE, CoInitializeEx, 2) { return S_OK_; }
WINAPI_EXPORT(OLE, CoUninitialize, 0) { return 0; }
WINAPI_EXPORT(OLE, CoFreeUnusedLibraries, 0) { return 0; }
WINAPI_EXPORT(OLE, CoTaskMemAlloc, 1) { return void_heap_alloc(void_process_heap(), ARG(0) ? ARG(0) : 1, 0); }
WINAPI_EXPORT(OLE, CoTaskMemFree, 1) { if (ARG(0)) void_heap_free(void_process_heap(), ARG(0)); return 0; }

static void format_guid(uint32_t g, char *out)
{
    sprintf(out, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", MEM32(g), MEM16(g + 4), MEM16(g + 6),
        MEM8(g + 8), MEM8(g + 9), MEM8(g + 10), MEM8(g + 11), MEM8(g + 12), MEM8(g + 13), MEM8(g + 14), MEM8(g + 15));
}

WINAPI_EXPORT(OLE, CoCreateInstance, 5)
{
    char clsid[48], iid[48];
    format_guid(ARG(0), clsid);
    format_guid(ARG(3), iid);
    void_log("[void] CoCreateInstance(%s, iid %s) from %08X: class not registered", clsid, iid, g_cur_func);
    if (ARG(4)) MEM32(ARG(4)) = 0;
    return REGDB_E_CLASSNOTREG_;
}

WINAPI_EXPORT(OLE, CoCreateGuid, 1)
{
    uint32_t g = ARG(0);
    for (int i = 0; i < 16; ++i) MEM8(g + (uint32_t)i) = (uint8_t)(rand() & 0xFF);
    MEM8(g + 7) = (uint8_t)((MEM8(g + 7) & 0x0F) | 0x40);
    MEM8(g + 8) = (uint8_t)((MEM8(g + 8) & 0x3F) | 0x80);
    return S_OK_;
}

#define OLEAUT "oleaut32.dll"
WINAPI_EXPORT_AS(OLEAUT, VariantInit, "VariantInit", 8, 1) { memset(GPTR(ARG(0)), 0, 16); return 0; }
WINAPI_EXPORT_AS(OLEAUT, VariantClear, "VariantClear", 9, 1) { memset(GPTR(ARG(0)), 0, 16); return S_OK_; }

/* ================================================================ dbghelp */
#define DBG "dbghelp.dll"
WINAPI_EXPORT(DBG, SymInitialize, 3) { return 0; }
WINAPI_EXPORT(DBG, SymCleanup, 1) { return 1; }
WINAPI_EXPORT(DBG, SymGetOptions, 0) { return 0; }
WINAPI_EXPORT(DBG, SymSetOptions, 1) { return ARG(0); }
WINAPI_EXPORT(DBG, SymFromAddr, 4) { return 0; }
WINAPI_EXPORT(DBG, SymGetLineFromAddr64, 5) { return 0; }
WINAPI_EXPORT(DBG, SymGetModuleBase64, 3) { g_edx = 0; return 0; }
WINAPI_EXPORT(DBG, SymGetModuleInfo64, 4) { return 0; }
WINAPI_EXPORT(DBG, SymFunctionTableAccess64, 3) { return 0; }
WINAPI_EXPORT(DBG, SymEnumSymbols, 6) { return 0; }
WINAPI_EXPORT(DBG, SymEnumerateModules64, 3) { return 0; }
WINAPI_EXPORT(DBG, SymGetTypeInfo, 6) { return 0; }
WINAPI_EXPORT(DBG, SymSetContext, 3) { return 0; }
WINAPI_EXPORT(DBG, StackWalk64, 9) { return 0; }
WINAPI_EXPORT(DBG, MiniDumpWriteDump, 7)
{
    void_log("[void] MiniDumpWriteDump requested by %08X (unavailable)", g_cur_func);
    void_dump_state("guest crash handler requested a minidump");
    return 0;
}

/* ================================================================== gdi32 */
#define GDI "gdi32.dll"
WINAPI_EXPORT(GDI, GetStockObject, 1) { return 0x00050000u + (ARG(0) & 0xFFu); }
WINAPI_EXPORT(GDI, DeleteObject, 1) { return 1; }
WINAPI_EXPORT(GDI, SelectObject, 2) { return 0x00050010u; }
WINAPI_EXPORT(GDI, SetBkColor, 2) { return 0xFFFFFFu; }
WINAPI_EXPORT(GDI, SetTextColor, 2) { return 0; }
WINAPI_EXPORT(GDI, SetBkMode, 2) { return 1; }
WINAPI_EXPORT(GDI, CreateFontA, 14)
{
    char *face = void_astr_utf8(ARG(13));
    VLOG("[void] CreateFontA(h=%d w=%d weight=%u face=%s)", (int)ARG(0), (int)ARG(1), ARG(4), face ? face : "");
    free(face);
    return 0x00060001u;
}
WINAPI_EXPORT(GDI, ExtTextOutA, 8) { return 1; }
WINAPI_EXPORT(GDI, GetTextExtentPoint32W, 4)
{
    uint32_t n = ARG(2);
    MEM32(ARG(3)) = n * 8u;
    MEM32(ARG(3) + 4) = 16u;
    return 1;
}
WINAPI_EXPORT(GDI, GetTextExtentExPointW, 7)
{
    uint32_t n = ARG(2), max_extent = ARG(3), fit = ARG(4), dx = ARG(5), size = ARG(6);
    uint32_t count = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (dx) MEM32(dx + 4 * i) = (i + 1) * 8u;
        if ((i + 1) * 8u <= max_extent) count = i + 1;
    }
    if (fit) MEM32(fit) = count;
    MEM32(size) = n * 8u;
    MEM32(size + 4) = 16u;
    return 1;
}
