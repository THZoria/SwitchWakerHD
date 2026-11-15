// Initial full-runtime host. This prototype terminates the entire title process
// on exit; it must never unload an NRO while guest/service threads are alive.
#include "main_switch.h"
#include "core_switch.h"
#include "switch2_memory.h"
#include "switch2_paths.h"
#include "switch2_player_defaults.h"
#include "switch2_profile.h"
#include "overlay/extensions.h"
#include "overlay/overlay.h"
#include "input_map.h"
#include "../runtime.h"
#include "host.h"
#include "mods/mod_hash.h"
#include "guest_addr.h"
#include "probe_runtime.h"
#include <switch.h>
#include <exception>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <pthread.h>
#include <cerrno>
#include <cstring>

extern "C" { uint8_t* ppc_mem_base_var = nullptr; }

namespace {
void sink(const char* text, size_t size) {
    // The boot logger serializes network/file writes and always flushes the SD
    // write. No detached log-writer thread is introduced.
    while (size) {
        const size_t n = std::min(size, size_t(480));
        wwhd_probe_log("%.*s%s", int(n), text, n == size ? "\n" : "");
        text += n;
        size -= n;
    }
}
[[noreturn]] void finish_process(int status) {
    wwhd_probe_log("[exit sw2] status=%d; terminating the title process, NOT unloading into hbmenu\n", status);
    // There is intentionally no static destruction, service teardown or guest
    // memory release while worker threads can still touch those objects.
    // Horizon reclaims all threads, mappings, GPU/service handles together.
    svcExitProcess();
    __builtin_unreachable();
}
}  // namespace

__attribute__((constructor(101))) static void install_sink() { log_set_sink(sink); }
void log_flush() { /* native file writes already use FsWriteOption_Flush */ }
size_t heap_never_used_mib() { return size_t(heapGetFreeSize() >> 20); }
void log_heap(const char* when) { LOG("[mem sw2] %s: %llu MiB heap free", when, (unsigned long long)(heapGetFreeSize() >> 20)); }
void core_switch::init_memory() { switch2::memory::initialize(); }

void main_switch::install_crash_handler() {
    std::set_terminate([] {
        try {
            if (auto error = std::current_exception()) std::rethrow_exception(error);
        } catch (const std::exception& error) {
            wwhd_probe_log("[fatal sw2] uncaught exception: %s\n", error.what());
        } catch (...) {
            wwhd_probe_log("[fatal sw2] uncaught nonstandard exception\n");
        }
        finish_process(1);
    });
}
void main_switch::start() {
    if (!wwhd_probe_ready()) finish_process(1);
    install_crash_handler();
    std::filesystem::create_directories(switch2::paths::root);
    if (chdir(switch2::paths::root) != 0) fatal("cannot enter the Switch 2 data folder");
    // Validate both explicit absolute paths and relative paths resolved through
    // getcwd before any save/backup code is allowed to create or rename files.
    const auto cwd = std::filesystem::current_path();
    const auto save = std::filesystem::absolute(std::filesystem::path(switch2::paths::save));
    const auto relative = std::filesystem::absolute("save").lexically_normal();
    if (cwd.lexically_normal() != std::filesystem::path(switch2::paths::root) ||
        save != relative || !save.is_absolute())
        fatal("Switch 2 POSIX path normalization failed");
    LOG("[boot sw2] paths-v2: cwd=%s save=%s (POSIX absolute, no device prefix)",
        cwd.c_str(), save.c_str());
    setenv("WWHD_NO_GAMEPAD", "1", 1);
    setenv("WWHD_DRC_MODE", "gamepad", 1);
    setenv("WWHD_ONE_SCREEN", "1", 1);
    overlay::ext::Features features;
    features.keyboard = false;
    overlay::ext::set_features(features);
    switch2::profile::register_menu();
    overlay::ext::add_shortcut([chord = switch2::player::SettingsChord{}](const overlay::PadValues& pad, bool open) mutable {
        using namespace input_map;
        if (!chord.poll(pad[kPadLB] > 0.5f, pad[kPadRB] > 0.5f, pad[kPadOptions] > 0.5f)) return false;
        overlay::set_open(!open, overlay::OpenReason::API);
        return true;
    });
    LOG("[input sw2] Settings: L + R + Minus; release all three before toggling again; no automatic controller setup");
    LOG("[boot sw2] FULL RUNTIME PROTOTYPE; exit closes the whole title (HOME), not hbmenu");
    LOG("[boot sw2] no clock changes; SDL input/audio, native OunceVK; private USA v0 game code");
}
void main_switch::choose_dirs() {
    config::game_dir = switch2::paths::game;
    config::save_dir = switch2::paths::save;
}
void main_switch::after_boot_line() {
    const std::string rpx = config::game_dir + "/code/cking.rpx";
    LOG("[boot sw2] checking RPX against the compiled game identity");
    if (mods::hash::sha256_file(rpx) != g_guest_build_rpx_sha256)
        fatal("RPX does not match the private recompiled game archive");
    if (!std::filesystem::is_directory(config::game_dir + "/content") ||
        !std::filesystem::is_regular_file(config::game_dir + "/meta/meta.xml"))
        fatal("game/content or game/meta/meta.xml is missing");
    LOG("[boot sw2] RPX hash and content/meta layout verified");
    log_heap("before guest memory");
}
void main_switch::after_mods() {}

extern "C" int __real_main(int, char**);
extern "C" int __wrap_main(int argc, char** argv) {
    if (!wwhd_probe_ready()) finish_process(1);
    try { finish_process(__real_main(argc, argv)); }
    catch (const std::exception& error) { wwhd_probe_log("[fatal sw2] main: %s\n", error.what()); }
    catch (...) { wwhd_probe_log("[fatal sw2] main: unknown exception\n"); }
    finish_process(1);
}
extern "C" [[noreturn]] void __wrap_exit(int status) { finish_process(status); }
extern "C" [[noreturn]] void __wrap__Exit(int status) { finish_process(status); }
extern "C" [[noreturn]] void __wrap__exit(int status) { finish_process(status); }
extern "C" [[noreturn]] void __wrap_abort() {
    wwhd_probe_log("[fatal sw2] abort caller=%p\n", __builtin_return_address(0));
    finish_process(1);
}

// libounce's getcwd includes "sdmc:", which POSIX std::filesystem would join
// back onto a path as a relative component. Keep its view consistently POSIX.
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

// libounce/newlib has no detach implementation. The prototype retains detached
// pthread resources until whole-process termination; never pretend they were
// joined or safe to unload. Replace with a reaper before long-session releases.
extern "C" int __wrap_pthread_detach(pthread_t) { return 0; }
extern "C" int __real_pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
extern "C" int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) {
    if (attr) return __real_pthread_create(thread, attr, fn, arg);
    pthread_attr_t large;
    int rc = pthread_attr_init(&large);
    if (rc) return rc;
    rc = pthread_attr_setstacksize(&large, 4 << 20);
    if (!rc) rc = __real_pthread_create(thread, &large, fn, arg);
    pthread_attr_destroy(&large);
    return rc;
}
