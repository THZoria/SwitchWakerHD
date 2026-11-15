#pragma once
#include <string_view>

namespace switch2::paths {
// std::filesystem follows POSIX syntax: "sdmc:/..." is relative, not absolute.
// libounce accepts /... on its default SD device. Native FsFileSystem calls
// already use volume-relative /... paths and do not need this conversion.
inline constexpr char root[] = "/switch/wwhd-switch2";
inline constexpr char game[] = "/switch/wwhd-switch2/game";
inline constexpr char save[] = "/switch/wwhd-switch2/save";
constexpr std::string_view posix_cwd(std::string_view cwd) {
    if (cwd.starts_with("sdmc:/")) cwd.remove_prefix(5);
    else if (!cwd.starts_with("/")) return {};  // never redirect a different device
    while (cwd.size() > 1 && cwd.back() == '/') cwd.remove_suffix(1);
    return cwd;
}
static_assert(posix_cwd("sdmc:/") == "/");
static_assert(posix_cwd("sdmc:/switch/wwhd-switch2/") == root);
static_assert(posix_cwd("/switch/wwhd-switch2") == root);
static_assert(posix_cwd("romfs:/assets").empty());
static_assert(posix_cwd("relative/path").empty());
}  // namespace switch2::paths
