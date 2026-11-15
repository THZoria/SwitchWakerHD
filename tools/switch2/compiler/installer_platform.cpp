#include "platform/switch2_paths.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>

// CPython/LLVM use POSIX absolute paths. libounce's raw getcwd includes sdmc:,
// which those libraries otherwise treat as a relative path component.
extern "C" char* __real_getcwd(char*, size_t);
extern "C" char* __wrap_getcwd(char* buffer, size_t size) {
    char* result = __real_getcwd(buffer, size);
    if (!result) return nullptr;
    const auto normalized = switch2::paths::posix_cwd(result);
    if (normalized.empty()) {
        if (!buffer) std::free(result);
        errno = EXDEV;
        return nullptr;
    }
    std::memmove(result, normalized.data(), normalized.size());
    result[normalized.size()] = '\0';
    return result;
}
