#pragma once
#include <filesystem>
#include <functional>
#include <string>

namespace switch2::compiler {
struct InstallRequest {
    std::filesystem::path embedded_resources, game, cache;
    std::string resources_sha256;
    // Return false to cancel at a safe stage/file boundary. Never kill a
    // running compiler or publish partial output when cancellation is pending.
    std::function<bool(const std::string&, size_t, size_t)> progress;
};
struct InstallResult {
    bool success = false, reused = false;
    std::filesystem::path nro;
    std::string diagnostics;
};
InstallResult install_game(const InstallRequest& request);
// Call once, only after all installation work/threads have completed, before
// asking the homebrew loader to chainload. No future compilation may follow.
void shutdown_compiler();
}
