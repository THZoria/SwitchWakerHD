#pragma once
#include <filesystem>
#include <string>

namespace switch2::compiler {
// Strict static-runtime profile: Switch 2 OUN0, bounded segments/MOD0, no
// imports, no text relocations, and only in-range AArch64 relative relocations.
bool validate_cached_nro(const std::filesystem::path& path, std::string* error = nullptr);
}
