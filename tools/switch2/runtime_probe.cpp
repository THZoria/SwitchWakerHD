// Load the checked personal RPX through WWHD's actual runtime/rpx_loader.cpp.
// This does not start the game or execute any recompiled PowerPC functions.
#include "probe_runtime.h"
#include "runtime.h"
#include "platform/switch2_memory.h"
#include "mods/mod_hash.h"
#include "game_identity.h"
#include <exception>

namespace {
struct GuestWindow {
    ~GuestWindow() { switch2::memory::release(); }
};
}

int main(int argc, char** argv) {
    LOG("[runtime probe] memory net-v6: main reached; real WWHD loader, no guest execution");
    if (!wwhd_probe_ready()) { LOG("[runtime probe] FAIL: platform initialization"); return 1; }
    const char* rpx = argc > 1 ? argv[1] : "sdmc:/switch/wwhd-switch2/game/code/cking.rpx";
    try {
        LOG("[runtime probe] checking %s (%s version 0)", rpx, kProbeBuild);
        if (mods::hash::sha256_file(rpx) != kProbeRpxSha256) {
            LOG("[runtime probe] FAIL: RPX does not match the prepared personal dump");
            return 1;
        }
        LOG("[runtime probe] SHA-256 matches; reserving and backing guest memory");
        GuestWindow lifetime;
        mem::init();
        LOG("[runtime probe] calling WWHD load_rpx");
        LoadedModule module{};
        if (!load_rpx(rpx, module)) throw std::runtime_error("load_rpx failed");
        if (module.entry != kProbeEntry) throw std::runtime_error("unexpected RPX entry point");
        LOG("[runtime probe] RPX loaded: entry=%08X SDA=%08X SDA2=%08X data_end=%08X stack=%X",
            module.entry, module.sda_base, module.sda2_base, module.data_end, module.stack_size);
        // Exercise the same generated-code endian helpers against a mapped
        // scratch word, then restore it. Do not read or print any game bytes.
        const uint32_t scratch = mem::kHostStart;
        const uint32_t previous = ld32(scratch);
        st32(scratch, 0x12345678);
        if (ld32(scratch) != 0x12345678) throw std::runtime_error("guest endian helpers failed");
        st32(scratch, previous);
        if (!switch2::memory::release()) throw std::runtime_error("guest mappings could not all be released");
        LOG("[runtime probe] PASS: checked RPX loaded; guest window released; returning to hbmenu");
        return 0;
    } catch (const std::exception& error) {
        LOG("[runtime probe] FAIL: %s", error.what());
        return 1;
    }
}
