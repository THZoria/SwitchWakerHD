#pragma once
#include <string>
#include <vector>

namespace switch2::compiler {
// A console-hosted compiler. Inputs are source generated from the user's dump;
// no subprocess, downloaded guest object, or precompiled game fallback is used.
struct Request {
    std::string source, object, resource_directory, system_include;
    std::vector<std::string> include_directories;
};
struct Result {
    bool success = false;
    std::string diagnostics;
};
Result compile_c_to_object(const Request& request);
}
