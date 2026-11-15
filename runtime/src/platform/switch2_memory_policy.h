#pragma once
#include <cstdint>

namespace switch2::memory::policy {
constexpr uint64_t guest_bytes = 0x100000000ull;
constexpr uint64_t host_headroom = 256ull << 20;
enum class Backend { Sparse, DenseHeap, Unavailable };
constexpr Backend choose(bool sparse_allowed, uint64_t heap_free) {
    if (sparse_allowed) return Backend::Sparse;
    return heap_free >= guest_bytes + host_headroom ? Backend::DenseHeap : Backend::Unavailable;
}
static_assert(choose(true, 0) == Backend::Sparse);
static_assert(choose(false, 0) == Backend::Unavailable);
static_assert(choose(false, guest_bytes) == Backend::Unavailable);
static_assert(choose(false, guest_bytes + host_headroom - 1) == Backend::Unavailable);
static_assert(choose(false, guest_bytes + host_headroom) == Backend::DenseHeap);
}  // namespace switch2::memory::policy
