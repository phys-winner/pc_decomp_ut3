/* native/zipmount.c against the archive and index tests/run_tests.py and LauncherSelfcheck wrote:
 * every stored file reads back whole (CRC-32), lookups ignore case and slashes, listing walks a
 * folder's tree, compressed files point to the sidecar. */
#include "../native/zipmount.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#define open _open
#define O_FLAGS (_O_RDONLY | _O_BINARY)
#else
#include <unistd.h>
#define O_FLAGS O_RDONLY
#endif

static int failures, checks;

static void check(int condition, const char *what)
{
    ++checks;
    if (!condition) { ++failures; printf("FAIL %s\n", what); }
}

static uint32_t crc32(const unsigned char *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static int count(const zm_entry *e, void *user) { (void)e; ++*(int *)user; return 0; }

int main(int argc, char **argv)
{
    if (argc < 3) { printf("usage: zipmount_test ARCHIVE INDEX\n"); return 2; }
    int fd = open(argv[1], O_FLAGS);
    zm_mount *m = zm_open(fd, argv[2], "sidecar");
    check(m != NULL, "the index opens for its archive");
    if (!m) return 1;
    check(zm_count(m) == 4, "four indexed files");
    static unsigned char buffer[1 << 20];
    const zm_entry *big = zm_find(m, "DATA\\Big.BIN");
    check(big != NULL && big->size == 300000, "case-insensitive lookup with backslashes");
    if (big) {
        long long n = zm_pread(m, big, buffer, sizeof buffer, 0);
        check(n == 300000 && crc32(buffer, (size_t)n) == big->crc, "a stored file reads back whole");
        n = zm_pread(m, big, buffer, 10, big->size - 4);
        check(n == 4, "reads stop at the end of the file");
        check(zm_pread(m, big, buffer, 10, big->size) == 0, "reading at the end gives nothing");
    }
    const zm_entry *small = zm_find(m, "/data/small.txt");
    check(small != NULL && zm_pread(m, small, buffer, sizeof buffer, 0) == (long long)small->size
        && crc32(buffer, small->size) == small->crc, "a leading slash is ignored; small file reads whole");
    const zm_entry *packed = zm_find(m, "packed.txt");
    char path[256];
    check(packed != NULL && (packed->flags & ZM_SIDECAR) && zm_pread(m, packed, buffer, 10, 0) == -1
        && zm_sidecar_path(m, packed, path, sizeof path) == 0 && strcmp(path, "sidecar/packed.txt") == 0,
        "a compressed file is read from the sidecar");
    check(zm_find(m, "data") == NULL && zm_find(m, "missing.bin") == NULL, "folders and absent files are not files");
    int in_data = 0, all = 0;
    zm_list(m, "Data", count, &in_data);
    zm_list(m, "", count, &all);
    check(in_data == 3 && all == 4, "listing a folder's tree");
    zm_close(m);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
