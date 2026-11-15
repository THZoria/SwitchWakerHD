#pragma once
#include <filesystem>
#include <string>

namespace switch2::compiler {
// Conversion only, not a linker, cache publisher, or complete executable
// validator. Input must also pass the runtime SDK's ABI/import/relocation audit.
// All output goes to a caller-owned transaction staging path, never a live NRO.
struct NroPackRequest {
    std::filesystem::path elf, output, icon, nacp;
};
bool pack_nro(const NroPackRequest& request, std::string& error);
}
