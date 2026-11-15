// Native guest memory for Switch 2. Uses sparse process mappings when permitted,
// otherwise an ordinary 4 GiB data allocation in libounce's heap (no JIT).
#pragma once
#include <cstdint>

namespace switch2::memory {
void initialize();
// Sparse cleanup retains backing on unmap failure (never freed while mapped).
bool release() noexcept;
uint64_t backed_bytes();
}
