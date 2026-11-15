// Windows-native, statically linked host utility. No MSYS/Visual C++ install
// is needed on the user's PC. The existing converter and validator are shared.
#include "../compiler/nro_pack.h"
#include "../compiler/nro_validate.h"
#include <cstdio>
#include <cwchar>

int wmain(int argc, wchar_t** argv) {
    std::string error;
    if (argc == 3 && !std::wcscmp(argv[1], L"--validate")) {
        if (switch2::compiler::validate_cached_nro(argv[2], &error)) return 0;
    } else if (argc == 5) {
        const switch2::compiler::NroPackRequest request{argv[1], argv[2], argv[3], argv[4]};
        if (switch2::compiler::pack_nro(request, error) &&
            switch2::compiler::validate_cached_nro(request.output, &error)) return 0;
    } else {
        std::fputs("Expected ELF, new NRO, icon and NACP paths, or --validate NRO.\n", stderr);
        return 2;
    }
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
}
