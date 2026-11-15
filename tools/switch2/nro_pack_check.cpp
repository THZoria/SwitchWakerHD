#include "compiler/nro_pack.h"
#include "compiler/nro_validate.h"
#include <cstdio>
#include <cstring>

// Host verification CLI; this executable is never bundled on the console.
int main(int argc, char** argv) {
    if (argc == 3 && !std::strcmp(argv[1], "--validate")) {
        std::string error;
        if (switch2::compiler::validate_cached_nro(argv[2], &error)) return 0;
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    if (argc < 3 || argc > 5) return 2;
    switch2::compiler::NroPackRequest request{argv[1], argv[2], {}, {}};
    if (argc > 3) request.icon = argv[3];
    if (argc > 4) request.nacp = argv[4];
    std::string error;
    if (!switch2::compiler::pack_nro(request, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
}
