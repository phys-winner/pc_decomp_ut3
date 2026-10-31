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
