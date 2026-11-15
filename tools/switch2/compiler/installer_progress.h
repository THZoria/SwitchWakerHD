#pragma once
#include <algorithm>
#include <cstddef>

namespace switch2::compiler {
// Work-unit percentage, never a timer-based estimate. Monolithic stages have
// one unit and remain at 0 until they really return successfully.
inline unsigned stage_percent(size_t current, size_t total) {
    return total ? unsigned(100.0L * std::min(current, total) / total) : 0;
}
}
