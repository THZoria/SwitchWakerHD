#pragma once
#include <string>
#include <vector>

namespace switch2::compiler {
struct LinkResult {
    bool success = false, restart_required = false;
    std::string diagnostics;
};
// GNU-style arguments from an audited runtime SDK recipe, without argv[0].
// Called serially during installation, before any guest or render threads exist.
LinkResult link_aarch64_elf(const std::vector<std::string>& recipe_arguments);
}
