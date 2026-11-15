#include "console_link.h"
#include <lld/Common/Driver.h>
#include <llvm/Support/raw_ostream.h>

LLD_HAS_DRIVER(elf)

namespace switch2::compiler {
LinkResult link_aarch64_elf(const std::vector<std::string>& recipe_arguments) {
    static bool healthy = true; // lld explicitly reports whether it can be reentered
    LinkResult result;
    if (!healthy) {
        result.restart_required = true;
        result.diagnostics = "The linker cannot safely run again; restart installation.\n";
        return result;
    }
    unsigned outputs = 0;
    for (size_t i = 0; i < recipe_arguments.size(); ++i) {
        const auto& arg = recipe_arguments[i];
        if (arg == "-o" && i + 1 < recipe_arguments.size() && !recipe_arguments[i + 1].empty()) ++outputs;
        if (arg.empty() || arg.front() == '@' || arg == "-plugin" || arg == "--plugin" ||
            arg.rfind("-plugin-opt", 0) == 0 || arg.rfind("--plugin=", 0) == 0) {
            result.diagnostics = "Link recipe must be expanded and must not load compiler plugins.\n";
            return result;
        }
    }
    if (outputs != 1) {
        result.diagnostics = "Link recipe must specify exactly one output ELF.\n";
        return result;
    }
    std::vector<std::string> storage{"ld.lld", "-m", "aarch64elf", "--threads=1", "--no-mmap-output-file"};
    storage.insert(storage.end(), recipe_arguments.begin(), recipe_arguments.end());
    std::vector<const char*> arguments;
    for (const auto& arg : storage) arguments.push_back(arg.c_str());
    llvm::raw_string_ostream diagnostics(result.diagnostics);
    const lld::DriverDef drivers[] = {{lld::Gnu, &lld::elf::link}};
    const auto linked = lld::lldMain(arguments, diagnostics, diagnostics, drivers);
    healthy = linked.canRunAgain;
    result.restart_required = !healthy;
    result.success = linked.retCode == 0 && healthy;
    return result;
}
}
