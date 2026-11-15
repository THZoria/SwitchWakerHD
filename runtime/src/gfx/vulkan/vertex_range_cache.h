#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gfxvk {
// Many immutable vertex snapshots per submission, shared across bindings.
// The key is the guest base address, NOT the requested prefix length. A smaller
// proven fetch prefix can reuse a larger snapshot after comparing its bytes.
// Never reads uncached upload memory or assumes guest addresses are immutable.
template<class Slice, class Device, size_t Sets = 512, size_t Ways = 4,
         size_t MaxRange = 1u << 20, size_t ShadowBudget = 32u << 20>
class VertexRangeCache {
public:
    struct Counters {
        uint64_t checks = 0, hits = 0, reusedBytes = 0, prefixHits = 0;
    } counters;

    template<class Make>
    Slice get(Device device, uint64_t generation, uint32_t address, uint32_t size,
              const void* fresh, bool directReads, Make make) {
        if (!initialized_ || device_ != device || generation_ != generation) {
            reset();
            initialized_ = true;
            device_ = device;
            generation_ = generation;
        }
        if (!size || size > MaxRange || uint64_t(address) + size > 0x100000000ull)
            return make(fresh, size);

        uint32_t hash = address;
        hash ^= hash >> 16;
        hash *= 0x7feb352du;
        hash ^= hash >> 15;
        hash *= 0x846ca68bu;
        hash ^= hash >> 16;
        Entry* set = entries_.data() + (hash % Sets) * Ways;
        Entry* victim = nullptr;
        Entry* candidate = nullptr;
        for (size_t way = 0; way < Ways; ++way) {
            auto& entry = set[way];
            if (entry.valid && entry.address == address) { candidate = &entry; break; }
            if (!victim || (!entry.valid && victim->valid) ||
                (entry.valid == victim->valid && entry.touched < victim->touched))
                victim = &entry;
        }
        if (candidate) {
            victim = candidate;
            // Preserve snapshot's zero-padding contract for payloads <16 bytes.
            if (size <= candidate->size && (size >= 16 || size == candidate->size)) {
                ++counters.checks;
                const void* kept = candidate->direct ? candidate->slice.mapped : candidate->bytes.data();
                if (!std::memcmp(fresh, kept, size)) {
                    ++counters.hits;
                    counters.reusedBytes += size;
                    counters.prefixHits += size < candidate->size;
                    candidate->touched = ++clock_;
                    auto result = candidate->slice;
                    result.size = size < 16 ? 16 : size;
                    return result;
                }
            }
        }
        victim->valid = false;
        const void* source = fresh;
        if (!directReads) {
            if (size > victim->bytes.capacity()) {
                // Keep a hard bound on retained CPU shadow capacity, even as
                // different sets grow over many submissions. GPU slices remain
                // owned by the upload arena, not by these evictable entries.
                std::vector<uint8_t> replacement(static_cast<const uint8_t*>(fresh),
                                                  static_cast<const uint8_t*>(fresh) + size);
                if (replacement.capacity() > ShadowBudget) return make(fresh, size);
                if (retainedBytes_ - victim->bytes.capacity() + replacement.capacity() > ShadowBudget)
                    release_shadows();
                retainedBytes_ -= victim->bytes.capacity();
                victim->bytes.swap(replacement);
                retainedBytes_ += victim->bytes.capacity();
            } else {
                const auto* input = static_cast<const uint8_t*>(fresh);
                victim->bytes.assign(input, input + size);
            }
            source = victim->bytes.data();
        }
        const auto slice = make(source, size);
        victim->slice = slice;
        victim->address = address;
        victim->size = size;
        victim->direct = directReads;
        victim->touched = ++clock_;
        victim->valid = bool(slice.buffer) && slice.mapped && slice.size >= (size < 16 ? 16 : size);
        return slice;
    }
    void reset() {
        for (auto& entry : entries_) { entry.valid = false; entry.slice = {}; }
        initialized_ = false;
        clock_ = 0;
    }
    size_t retained_bytes() const { return retainedBytes_; }

private:
    static_assert(Sets && Ways);
    struct Entry {
        Slice slice{};
        std::vector<uint8_t> bytes;
        uint32_t address = 0, size = 0;
        uint64_t touched = 0;
        bool valid = false, direct = false;
    };
    void release_shadows() {
        for (auto& entry : entries_) {
            entry.valid = false;
            entry.slice = {};
            std::vector<uint8_t>().swap(entry.bytes);
        }
        retainedBytes_ = 0;
    }
    std::array<Entry, Sets * Ways> entries_{};
    Device device_{};
    uint64_t generation_ = 0, clock_ = 0;
    size_t retainedBytes_ = 0;
    bool initialized_ = false;
};
}
