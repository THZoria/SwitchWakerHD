#pragma once
#include <string>
#include <functional>

namespace switch2::compiler {
struct PythonResult {
    bool success = false;
    std::string diagnostics;
};
// Serialize calls before game threads start. resources has python/Lib and the
// original tools/recomp + runtime source layout used by the ABI-hook auditor.
PythonResult recompile_rpx(const std::string& resources, const std::string& game,
                           const std::string& generated,
                           const std::function<bool(const std::string&, size_t, size_t)>& progress = {});
}
