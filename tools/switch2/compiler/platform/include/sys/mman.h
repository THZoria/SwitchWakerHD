#pragma once
#include <stddef.h>
#include <sys/types.h>

// LLVM's POSIX source needs these declarations even when mmap is unavailable.
// The compiler port reports ENOSYS, allowing MemoryBuffer to use ordinary reads.
// These flags do NOT grant executable-memory permissions on the console.
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FAILED ((void*)-1)

#ifdef __cplusplus
extern "C" {
#endif
void* mmap(void*, size_t, int, int, int, off_t);
int munmap(void*, size_t);
int mprotect(void*, size_t, int);
#ifdef __cplusplus
}
#endif
