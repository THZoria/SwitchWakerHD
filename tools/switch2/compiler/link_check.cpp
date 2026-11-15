// Development-only console ELF to force a real compiler/linker/packager link.
// Not an installer or a playable NRO; never substitute it for the launcher.
#include "console_compile.h"
#include "console_link.h"
#include "nro_pack.h"
#ifdef WWHD_COMPILER_PYTHON
#include "console_python.h"
#endif
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
#ifdef WWHD_COMPILER_PYTHON
    if (argc == 5 && !std::strcmp(argv[1], "--recompile")) {
        const auto result = switch2::compiler::recompile_rpx(argv[2], argv[3], argv[4]);
        std::fputs(result.diagnostics.c_str(), stderr);
        return result.success ? 0 : 1;
    }
#endif
    if (argc == 6 && !std::strcmp(argv[1], "--compile")) {
        switch2::compiler::Request request;
        request.source = argv[2];
        request.object = argv[3];
        request.resource_directory = argv[4];
        request.system_include = argv[5];
        const auto result = switch2::compiler::compile_c_to_object(request);
        std::fputs(result.diagnostics.c_str(), stderr);
        return result.success ? 0 : 1;
    }
    if (argc > 2 && !std::strcmp(argv[1], "--link")) {
        const auto result = switch2::compiler::link_aarch64_elf({argv + 2, argv + argc});
        std::fputs(result.diagnostics.c_str(), stderr);
        return result.success ? 0 : 1;
    }
    if (argc == 4 && !std::strcmp(argv[1], "--pack")) {
        std::string error;
        const bool success = switch2::compiler::pack_nro({argv[2], argv[3], {}, {}}, error);
        std::fputs(error.c_str(), stderr);
        return success ? 0 : 1;
    }
    std::fputs("Compiler development link check only; no RPX installer is connected.\n", stderr);
    return 2;
}
