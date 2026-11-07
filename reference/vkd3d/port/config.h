/* config.h for vkd3d-shader 2.1 built into the UT3 Android port (aarch64, Android bionic, clang).
 * Written by hand for tools/build_vkd3d.py in place of configure's output. */
#define HAVE_ATOMIC_EXCHANGE_N 1
#define HAVE_BUILTIN_ADD_OVERFLOW 1
#define HAVE_BUILTIN_CLZ 1
#define HAVE_BUILTIN_CTZ 1
#define HAVE_BUILTIN_POPCOUNT 1
#define HAVE_DECL_PROGRAM_INVOCATION_NAME 0
#define HAVE_DLFCN_H 1
#define HAVE_GETTID 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIBM 1
#define HAVE_PTHREAD_H 1
#define HAVE_PTHREAD_SETNAME_NP 1
#define HAVE_PTHREAD_SETNAME_NP_2 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_STRTOF_L 1
#define HAVE_SYNC_ADD_AND_FETCH 1
#define HAVE_SYNC_BOOL_COMPARE_AND_SWAP 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_WCHAR_H 1
#define PACKAGE "vkd3d"
#define PACKAGE_BUGREPORT "https://gitlab.winehq.org/wine/vkd3d/-/issues"
#define PACKAGE_NAME "vkd3d"
#define PACKAGE_STRING "vkd3d 2.1"
#define PACKAGE_TARNAME "vkd3d"
#define PACKAGE_URL ""
#define PACKAGE_VERSION "2.1"
#define STDC_HEADERS 1
#define VERSION "2.1"

/* vkd3d's TRACE, WARN and FIXME are calls that test the message level inside, so their arguments
 * (writemask strings; for TRACE_ON() a disassembly of the whole program) were built for every
 * message even with messages off: 13% of a compile of a UE3 material shader. They are compiled
 * out here, before vkd3d_common.h defines them; ERR and MESSAGE stay. (The header's own
 * VKD3D_NO_TRACE_MESSAGES has the uncurried TRACE_(ch, ...) form and does not compile.) */
#define VKD3D_PORT_DISCARD(...) do { } while (0)
#define TRACE VKD3D_PORT_DISCARD
#define TRACE_(ch) VKD3D_PORT_DISCARD
#define TRACE_TEXT(text, size) do { } while (0)
#define TRACE_TEXT_(ch, text, size) do { } while (0)
#define TRACE_ON() (0)
#define TRACE_ON_(ch) (0)
#define WARN VKD3D_PORT_DISCARD
#define WARN_(ch) VKD3D_PORT_DISCARD
#define WARN_ON() (0)
#define WARN_ON_(ch) (0)
#define FIXME VKD3D_PORT_DISCARD
#define FIXME_(ch) VKD3D_PORT_DISCARD
#define FIXME_ONCE VKD3D_PORT_DISCARD
#define FIXME_ONCE_(ch) VKD3D_PORT_DISCARD
