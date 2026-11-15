#include "gfx/vulkan/vertex_range_cache.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <memory>
#include <random>

struct Slice { uint64_t buffer = 0; void* mapped = nullptr; size_t size = 0; };
struct Arena {
    bool poison = true;
    std::vector<std::unique_ptr<uint8_t[]>> mapped;
    std::vector<std::vector<uint8_t>> gpu;
    Slice make(const void* bytes, size_t size) {
        const size_t allocated = std::max<size_t>(size, 16);
        mapped.emplace_back(new uint8_t[allocated]{});
        auto* destination = mapped.back().get();
        if (size) std::memcpy(destination, bytes, size);
        gpu.emplace_back(destination, destination + allocated);
        if (poison) std::memset(destination, 0xcd, allocated);
        return {gpu.size(), destination, allocated};
    }
    void check(const Slice& slice, const void* bytes, size_t size) const {
        const auto& actual = gpu.at(slice.buffer - 1);
        assert(!std::memcmp(actual.data(), bytes, size));
        if (size < 16)
            for (size_t i = size; i < 16; ++i) assert(actual[i] == 0);
    }
};

static void basic(bool direct) {
    Arena arena;
    arena.poison = !direct;
    gfxvk::VertexRangeCache<Slice, int, 1, 4> cache;
    std::array<uint8_t, 256> guest;
    for (size_t i = 0; i < guest.size(); ++i) guest[i] = uint8_t(i);
    auto make = [&](const void* bytes, size_t size) { return arena.make(bytes, size); };
    auto get = [&](int device, uint64_t generation, uint32_t address, uint32_t size) {
        auto slice = cache.get(device, generation, address, size, guest.data(), direct, make);
        arena.check(slice, guest.data(), size);
        return slice;
    };
    auto first = get(1, 1, 0x1000, 256);
    assert(get(1, 1, 0x1000, 64).buffer == first.buffer);
    assert(cache.counters.prefixHits == 1);
    get(1, 1, 0x2000, 256);
    assert(get(1, 1, 0x1000, 128).buffer == first.buffer); // interleaved addresses retained
    guest[200] ^= 1; // outside the requested prefix: no upload necessary
    assert(get(1, 1, 0x1000, 64).buffer == first.buffer);
    auto changed = get(1, 1, 0x1000, 256);
    assert(changed.buffer != first.buffer);
    assert(get(1, 1, 0x1000, 256).buffer == changed.buffer);
    auto small = get(1, 1, 0x1000, 4); // cannot expose old payload in zero padding
    assert(small.buffer != changed.buffer);
    assert(get(1, 1, 0x1000, 4).buffer == small.buffer);
    auto grown = get(1, 1, 0x1000, 128);
    assert(grown.buffer != small.buffer);
    assert(get(1, 1, 0x1000, 64).buffer == grown.buffer);
    auto generation = get(1, 2, 0x1000, 128);
    assert(generation.buffer != grown.buffer);
    auto device = get(2, 2, 0x1000, 128);
    assert(device.buffer != generation.buffer);
    cache.reset();
    assert(get(2, 2, 0x1000, 128).buffer != device.buffer);
    // LRU replacement with four colliding addresses.
    cache.reset();
    auto a = get(2, 2, 0x1000, 128);
    auto b = get(2, 2, 0x2000, 128);
    get(2, 2, 0x3000, 128);
    get(2, 2, 0x4000, 128);
    assert(get(2, 2, 0x1000, 128).buffer == a.buffer);
    get(2, 2, 0x5000, 128);
    assert(get(2, 2, 0x2000, 128).buffer != b.buffer);
    std::printf("vertex range cache (%s): interleaving, prefixes, mutation, growth, padding, generation/device/reset and LRU passed\n",
                direct ? "direct" : "shadow/poisoned uploads");
}

static void budget_and_snapshot() {
    Arena arena;
    gfxvk::VertexRangeCache<Slice, int, 8, 4, 128, 256> cache;
    std::array<uint8_t, 192> guest{};
    for (unsigned i = 0; i < 1000; ++i) {
        guest[i % guest.size()] ^= 1;
        auto slice = cache.get(1, 1, i * 256, 128, guest.data(), false,
            [&](const void* bytes, size_t size) { return arena.make(bytes, size); });
        arena.check(slice, guest.data(), 128);
        assert(cache.retained_bytes() <= 256);
    }
    const auto before = arena.gpu.size();
    cache.get(1, 1, 0x8000, 129, guest.data(), false,
        [&](const void* bytes, size_t size) { return arena.make(bytes, size); });
    cache.get(1, 1, 0x8000, 129, guest.data(), false,
        [&](const void* bytes, size_t size) { return arena.make(bytes, size); });
    assert(arena.gpu.size() == before + 2); // oversize bypasses, never pins memory
    cache.reset();
    guest.fill(3);
    auto slice = cache.get(1, 1, 0x1000, 128, guest.data(), false,
        [&](const void* bytes, size_t size) {
            assert(bytes != guest.data());
            guest[0] = 99; // shadow and GPU must still contain the same original data
            return arena.make(bytes, size);
        });
    assert(arena.gpu.at(slice.buffer - 1)[0] == 3);
    auto next = cache.get(1, 1, 0x1000, 128, guest.data(), false,
        [&](const void* bytes, size_t size) { return arena.make(bytes, size); });
    assert(next.buffer != slice.buffer);
    arena.check(next, guest.data(), 128);
    std::puts("vertex range cache: retained-memory budget, oversize bypass and stable shadow-to-GPU snapshot passed");
}

static void randomized(bool direct) {
    Arena arena;
    arena.poison = !direct;
    gfxvk::VertexRangeCache<Slice, int, 8, 4, 512, 4096> cache;
    std::mt19937 random(0x56455254);
    std::array<std::array<uint8_t, 512>, 32> sources{};
    for (auto& source : sources) for (auto& byte : source) byte = uint8_t(random());
    for (unsigned draw = 0; draw < 20000; ++draw) {
        auto& source = sources[random() % sources.size()];
        const uint32_t address = uint32_t(&source - sources.data()) * 256;
        const uint32_t size = 1 + random() % 512;
        if (random() % 9 == 0) source[random() % size] ^= 1;
        auto slice = cache.get(1 + int(draw / 10000), draw / 1000, address, size, source.data(), direct,
            [&](const void* bytes, size_t n) { return arena.make(bytes, n); });
        arena.check(slice, source.data(), size);
        assert(cache.retained_bytes() <= 4096);
    }
    std::printf("vertex range cache (%s): 20000 randomized requests match immutable reference payloads\n",
                direct ? "direct" : "shadow");
}

int main() {
    basic(false);
    basic(true);
    budget_and_snapshot();
    randomized(false);
    randomized(true);
}
