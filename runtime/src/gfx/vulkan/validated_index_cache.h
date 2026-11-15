#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gfxvk {
// Bounded CPU conversion memo. Guest addresses are NOT an invalidation scheme:
// every lookup compares every input byte with the immutable conversion source.
// GPU slices are reused only within the same device/submission generation.
template<class Slice, size_t Slots = 128>
class ValidatedIndexCache {
public:
    using Key = std::array<uint32_t, 6>; // address, count, primitive, type, restart, marker
    static constexpr size_t maxSourceBytes = 16 * 1024;
    static constexpr size_t maxOutputIndices = 16 * 1024;
    struct Entry {
        Key key{};
        std::vector<uint8_t> source;
        std::vector<uint32_t> indices;
        bool ready = false, extentReady = false, extentValid = false;
        uint32_t minimum = 0, maximum = 0;
        Slice slice{};
        uintptr_t device = 0;
        uint64_t generation = ~uint64_t{0};
        template<class Make>
        Slice upload(uintptr_t nextDevice, uint64_t nextGeneration, Make make) {
            if (!slice.buffer || device != nextDevice || generation != nextGeneration) {
                slice = make(indices.data(), indices.size() * sizeof(uint32_t));
                device = nextDevice;
                generation = nextGeneration;
            }
            return slice;
        }
    };
    Entry* lookup(const Key& key, const void* bytes, size_t size, size_t outputIndices, bool& hit) {
        hit = false;
        if (!size || size > maxSourceBytes || outputIndices > maxOutputIndices) return nullptr;
        uint32_t hash = 0x9e3779b9;
        for (uint32_t word : key) hash = (hash ^ word) * 16777619u;
        hash ^= hash >> 16;
        hash *= 0x7feb352du;
        hash ^= hash >> 15;
        hash *= 0x846ca68bu;
        hash ^= hash >> 16;
        auto& entry = entries_[hash % Slots];
        if (entry.ready && entry.key == key && entry.source.size() == size &&
            !std::memcmp(entry.source.data(), bytes, size)) {
            hit = true;
            return &entry;
        }
        entry.ready = entry.extentReady = false;
        entry.slice = {};
        entry.key = key;
        const auto* input = static_cast<const uint8_t*>(bytes);
        entry.source.assign(input, input + size);
        entry.indices.clear();
        return &entry; // caller converts this stable source, THEN sets ready
    }
    void reset() {
        for (auto& entry : entries_) {
            entry.ready = entry.extentReady = false;
            entry.slice = {};
        }
    }
private:
    static_assert(Slots > 0);
    std::array<Entry, Slots> entries_{};
};
}
