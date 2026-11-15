#include <sys/mman.h>
#include <cerrno>

// The frontend emits object files; it does not execute its output. In particular
// do not replace executable mappings with writable malloc buffers and claim RX.
// LLVM's file MemoryBuffer falls back to read() when mapping is unsupported.
extern "C" void* mmap(void*, size_t, int, int, int, off_t) { errno = ENOSYS; return MAP_FAILED; }
extern "C" int munmap(void*, size_t) { errno = ENOSYS; return -1; }
extern "C" int mprotect(void*, size_t, int) { errno = ENOSYS; return -1; }
