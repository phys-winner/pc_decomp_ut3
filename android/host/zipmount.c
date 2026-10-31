/* Copy of the launcher base's native/zipmount.c with native/zipmount.h inlined: the host build hashes
 * every header under android/ into each object's cache key, so a new header would rebuild the whole
 * library. files.c declares the same zm_entry and functions. */
#define _FILE_OFFSET_BITS 64
/* A read-only layer over a ZIP whose files are stored without compression: the game reads them in
 * place through the archive's descriptor (pread/mmap at the entry's data offset), and the few
 * compressed ones from the sidecar folder the launcher unpacked them into. The index is written by
 * the launcher (StoredArchive.java, format "ZMI1"); lookups ignore ASCII case and accept '\' and '/'.
 * Thread-safe after zm_open: every read is positional. */
#ifndef ZIPMOUNT_H
#define ZIPMOUNT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZM_SIDECAR 1u

typedef struct zm_entry {
    uint64_t offset;          /* data offset in the archive (stored entries) */
    uint64_t size;            /* the file's size */
    uint32_t crc;             /* CRC-32 of its bytes */
    uint32_t flags;           /* ZM_SIDECAR: read it from the sidecar folder instead */
    uint64_t time;            /* modification time, ms since 1970; 0 unknown */
    const char *path;         /* relative, '/' separated, not terminated: see length */
    uint16_t length;
} zm_entry;

typedef struct zm_mount zm_mount;

/* Opens an index for the archive's descriptor (kept open, never closed here) and the sidecar
 * folder; NULL if the index is unreadable or the archive's size differs from the indexed one. */
zm_mount *zm_open(int fd, const char *index_path, const char *sidecar);

/* The mount of a part the launcher set up (PORT_MOUNT_<ID>, PORT_MOUNT_FD_<ID>, PORT_PART_<ID>);
 * NULL when the part is a plain folder. */
zm_mount *zm_from_env(const char *part);

void zm_close(zm_mount *mount);

const zm_entry *zm_find(const zm_mount *mount, const char *path);

/* Up to size bytes of the entry from offset at; -1 for sidecar entries (open zm_sidecar_path). */
long long zm_pread(const zm_mount *mount, const zm_entry *entry, void *buffer, size_t size, uint64_t at);

/* The sidecar file of an entry; 0 on success. */
int zm_sidecar_path(const zm_mount *mount, const zm_entry *entry, char *out, size_t size);

/* Maps bytes [at, at + size) of a stored entry read-only; base and length receive what munmap needs. */
void *zm_map(const zm_mount *mount, const zm_entry *entry, uint64_t at, size_t size, void **base, size_t *length);

/* Calls visit for every entry under folder ("" for all, "a/b" for that folder's tree) in path order;
 * stops when visit returns non-zero and returns that value. */
int zm_list(const zm_mount *mount, const char *folder, int (*visit)(const zm_entry *entry, void *user), void *user);

size_t zm_count(const zm_mount *mount);

#ifdef __cplusplus
}
#endif

#endif

/* See zipmount.h. The index ("ZMI1", StoredArchive.java) is read whole and kept: one record per
 * file, re-sorted here by the same key the lookups use (ASCII lower case, '\' as '/'). */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
/* The PC test build: positional reads through a seek, no mapping. */
#include <io.h>
static long long zm_read_at(int fd, void *buffer, size_t size, uint64_t offset)
{
    if (_lseeki64(fd, (long long)offset, SEEK_SET) < 0) return -1;
    return _read(fd, buffer, (unsigned)size);
}
static long long zm_file_size(int fd)
{
    struct _stat64 st;
    return _fstat64(fd, &st) ? -1 : st.st_size;
}
#else
#include <sys/mman.h>
#include <unistd.h>
static long long zm_read_at(int fd, void *buffer, size_t size, uint64_t offset)
{
    ssize_t n;
    do n = pread(fd, buffer, size, (off_t)offset); while (n < 0 && errno == EINTR);
    return n;
}
static long long zm_file_size(int fd)
{
    struct stat st;
    return fstat(fd, &st) ? -1 : (long long)st.st_size;
}
#endif

struct zm_mount {
    int fd;
    uint64_t archive_size;
    size_t count;
    zm_entry *entries;
    unsigned char *index;     /* the index file; entries' paths point into it */
    char *sidecar;
};

static uint64_t le(const unsigned char *p, int bytes)
{
    uint64_t v = 0;
    for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

static int key_char(int c)
{
    if (c == '\\') return '/';
    return c < 128 ? tolower(c) : c;
}

/* Compares a path of the index with a lookup key (NUL-terminated), ignoring ASCII case. */
static int compare_key(const char *a, size_t alen, const char *b, size_t blen)
{
    size_t n = alen < blen ? alen : blen;
    for (size_t i = 0; i < n; ++i) {
        int x = key_char((unsigned char)a[i]), y = key_char((unsigned char)b[i]);
        if (x != y) return x < y ? -1 : 1;
    }
    return alen < blen ? -1 : alen > blen ? 1 : 0;
}

static int compare_entries(const void *a, const void *b)
{
    const zm_entry *x = (const zm_entry *)a, *y = (const zm_entry *)b;
    return compare_key(x->path, x->length, y->path, y->length);
}

zm_mount *zm_open(int fd, const char *index_path, const char *sidecar)
{
    if (fd < 0 || !index_path) return NULL;
    FILE *file = fopen(index_path, "rb");
    if (!file) return NULL;
    zm_mount *m = (zm_mount *)calloc(1, sizeof *m);
    long size = -1;
    if (m && !fseek(file, 0, SEEK_END)) size = ftell(file);
    if (!m || size < 48 || fseek(file, 0, SEEK_SET) || !(m->index = (unsigned char *)malloc((size_t)size))
            || fread(m->index, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        zm_close(m);
        return NULL;
    }
    fclose(file);
    const unsigned char *p = m->index, *end = m->index + size;
    if (memcmp(p, "ZMI1", 4) != 0) { zm_close(m); return NULL; }
    m->count = (size_t)le(p + 4, 4);
    m->archive_size = le(p + 8, 8);
    p += 48;
    if (zm_file_size(fd) != (long long)m->archive_size || m->count > (size_t)(end - p) / 34) { zm_close(m); return NULL; }
    m->entries = (zm_entry *)calloc(m->count ? m->count : 1, sizeof *m->entries);
    if (!m->entries) { zm_close(m); return NULL; }
    for (size_t i = 0; i < m->count; ++i) {
        if (end - p < 34) { zm_close(m); return NULL; }
        zm_entry *e = &m->entries[i];
        e->offset = le(p, 8);
        e->size = le(p + 8, 8);
        e->crc = (uint32_t)le(p + 16, 4);
        e->flags = (uint32_t)le(p + 20, 4);
        e->time = le(p + 24, 8);
        e->length = (uint16_t)le(p + 32, 2);
        p += 34;
        if ((size_t)(end - p) < e->length) { zm_close(m); return NULL; }
        e->path = (const char *)p;
        p += e->length;
        if (!(e->flags & ZM_SIDECAR) && (e->offset > m->archive_size || e->size > m->archive_size - e->offset)) {
            zm_close(m);
            return NULL;
        }
    }
    qsort(m->entries, m->count, sizeof *m->entries, compare_entries);
    m->fd = fd;
    if (sidecar && !(m->sidecar = strdup(sidecar))) { zm_close(m); return NULL; }
    return m;
}

zm_mount *zm_from_env(const char *part)
{
    char name[96], upper[64];
    size_t n = 0;
    for (; part[n] && n + 1 < sizeof upper; ++n) upper[n] = (char)toupper((unsigned char)part[n]);
    upper[n] = 0;
    snprintf(name, sizeof name, "PORT_MOUNT_%s", upper);
    const char *index = getenv(name);
    snprintf(name, sizeof name, "PORT_MOUNT_FD_%s", upper);
    const char *fd = getenv(name);
    snprintf(name, sizeof name, "PORT_PART_%s", upper);
    const char *sidecar = getenv(name);
    if (!index || !*index || !fd || !*fd) return NULL;
    return zm_open(atoi(fd), index, sidecar);
}

void zm_close(zm_mount *m)
{
    if (!m) return;
    free(m->entries);
    free(m->index);
    free(m->sidecar);
    free(m);
}

size_t zm_count(const zm_mount *m) { return m ? m->count : 0; }

const zm_entry *zm_find(const zm_mount *m, const char *path)
{
    if (!m || !path) return NULL;
    while (*path == '/' || *path == '\\') ++path;
    size_t length = strlen(path);
    size_t lo = 0, hi = m->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = compare_key(m->entries[mid].path, m->entries[mid].length, path, length);
        if (c == 0) return &m->entries[mid];
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return NULL;
}

long long zm_pread(const zm_mount *m, const zm_entry *e, void *buffer, size_t size, uint64_t at)
{
    if (!m || !e || (e->flags & ZM_SIDECAR)) return -1;
    if (at >= e->size) return 0;
    if (size > e->size - at) size = (size_t)(e->size - at);
    return zm_read_at(m->fd, buffer, size, e->offset + at);
}

int zm_sidecar_path(const zm_mount *m, const zm_entry *e, char *out, size_t size)
{
    if (!m || !e || !m->sidecar || !(e->flags & ZM_SIDECAR)) return -1;
    int n = snprintf(out, size, "%s/%.*s", m->sidecar, (int)e->length, e->path);
    return n < 0 || (size_t)n >= size ? -1 : 0;
}

void *zm_map(const zm_mount *m, const zm_entry *e, uint64_t at, size_t size, void **base, size_t *length)
{
#ifdef _WIN32
    (void)m; (void)e; (void)at; (void)size; (void)base; (void)length;
    return NULL;
#else
    if (!m || !e || (e->flags & ZM_SIDECAR) || at > e->size || size > e->size - at) return NULL;
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) page = 4096;
    uint64_t start = e->offset + at, aligned = start & ~(uint64_t)(page - 1);
    size_t delta = (size_t)(start - aligned);
    void *mapped = mmap(NULL, size + delta, PROT_READ, MAP_PRIVATE, m->fd, (off_t)aligned);
    if (mapped == MAP_FAILED) return NULL;
    *base = mapped;
    *length = size + delta;
    return (char *)mapped + delta;
#endif
}

int zm_list(const zm_mount *m, const char *folder, int (*visit)(const zm_entry *, void *), void *user)
{
    if (!m || !visit) return 0;
    char prefix[1024];
    while (folder && (*folder == '/' || *folder == '\\')) ++folder;
    int n = snprintf(prefix, sizeof prefix, "%s%s", folder ? folder : "", folder && *folder ? "/" : "");
    if (n < 0 || (size_t)n >= sizeof prefix) return 0;
    size_t length = (size_t)n, lo = 0, hi = m->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (compare_key(m->entries[mid].path, m->entries[mid].length, prefix, length) < 0) lo = mid + 1; else hi = mid;
    }
    for (size_t i = lo; i < m->count; ++i) {
        const zm_entry *e = &m->entries[i];
        if (e->length < length || compare_key(e->path, length, prefix, length) != 0) break;
        int result = visit(e, user);
        if (result) return result;
    }
    return 0;
}
