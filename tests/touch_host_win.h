/* Windows host support for fixtures which include the production Android helpers. */
#ifndef TOUCH_HOST_WIN_H
#define TOUCH_HOST_WIN_H
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define VOID_HOST_H
typedef SRWLOCK pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT
static void pthread_mutex_lock(pthread_mutex_t *lock) { AcquireSRWLockExclusive(lock); }
static void pthread_mutex_unlock(pthread_mutex_t *lock) { ReleaseSRWLockExclusive(lock); }

/* Match recomp_types.h's guest-address addition without its ARM64 register model. */
#define VOID_GUEST_BASE 0x2000000000ull
#define ADDR(address) ((uintptr_t)(uint32_t)(address)+(uintptr_t)VOID_GUEST_BASE)
#undef GPTR
#define GPTR(address) ((void *)ADDR(address))
#define MEM32(address) (*(uint32_t *)ADDR(address))

uint32_t void_tick_ms(void);
uint64_t void_monotonic_ns(void);
uint32_t void_window_main(void);
void void_post_message(uint32_t window,uint32_t message,uint32_t value,uint32_t parameter);
void void_log(const char *format,...);
int void_vm_readable(uint32_t address,uint32_t size);
int void_vm_writable(uint32_t address,uint32_t size);
#endif
#endif
