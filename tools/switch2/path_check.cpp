// Host-only path regression check. Never reads/writes game or save files.
#include "platform/switch2_paths.h"
#include <cassert>
#include <filesystem>
#include <string>

int main() {
    namespace fs = std::filesystem;
    using namespace switch2::paths;
    assert(!fs::path("sdmc:/switch/wwhd-switch2/save").is_absolute());
    for (const char* raw : {"sdmc:/switch/wwhd-switch2", "sdmc:/switch/wwhd-switch2/",
                            "/switch/wwhd-switch2", "/switch/wwhd-switch2/"}) {
        const fs::path cwd{std::string(posix_cwd(raw))};
        assert(cwd.is_absolute());
        assert((cwd / "save").lexically_normal() == fs::path(save));
        assert(fs::absolute(fs::path(save)).lexically_normal() == fs::path(save));
        const auto pending = cwd / "save-backups" / "restore-pending.json";
        assert(pending == fs::path("/switch/wwhd-switch2/save-backups/restore-pending.json"));
        assert((cwd / fs::path(save)).lexically_normal() == fs::path(save));
    }
    assert(posix_cwd("sdmc:/") == "/");
    assert(posix_cwd("romfs:/assets").empty());
    assert(posix_cwd("save").empty());
}
