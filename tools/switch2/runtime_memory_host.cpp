// Minimal core.cpp host hooks for the loader/memory probe. No detached logger,
// debug server, clock changes or guest threads are started in this target.
#include "platform/switch2_memory.h"
#include "runtime.h"
#include "probe_runtime.h"
#include <cstdarg>
#include <cstdio>
#include <cstdint>

extern "C" { uint8_t* ppc_mem_base_var = nullptr; }
namespace mem {
void init() { switch2::memory::initialize(); }
}
void log_flush() {} // the probe log sink writes synchronously
void log_msg(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    wwhd_probe_log("%s\n", text);
}
void fatal(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    throw std::runtime_error(text);
}
