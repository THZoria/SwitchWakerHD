#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gfxvk::vk {
// Metadata only: never caches guest bytes. Preserve entry order and merge only
// adjacent 16-byte copies with consecutive source AND destination offsets.
struct UniformCopyPlan {
    struct Span { uint32_t source; size_t destination, size; };
    std::vector<Span> spans;
    static void copy(uint8_t* destination, const void* source, size_t size) {
        // Singleton spans remain fixed-size native loads/stores, not tiny
        // out-of-line libc memcpy calls on the console toolchain.
        if (size == 16) std::memcpy(destination, source, 16);
        else std::memcpy(destination, source, size);
    }
    void append(uint32_t source, int64_t destination, size_t outputSize) {
        if (destination < 0 || uint64_t(destination) + 16 > outputSize) return;
        if (!spans.empty()) {
            auto& last = spans.back();
            if (uint64_t(last.source) + last.size == source &&
                last.destination + last.size == size_t(destination)) {
                last.size += 16;
                return;
            }
        }
        spans.push_back({source, size_t(destination), 16});
    }
    void registers(uint8_t* output, const void* base) const {
        const auto* bytes = static_cast<const uint8_t*>(base);
        for (const auto& span : spans)
            copy(output + span.destination, bytes + span.source, span.size);
    }
    template<class GuestPointer>
    void guest(uint8_t* output, uint32_t address, size_t blockSize,
               const uint8_t* submitted, GuestPointer pointer) const {
        for (const auto& span : spans) {
            if (submitted && uint64_t(span.source) + span.size <= blockSize) {
                copy(output + span.destination, submitted + span.source, span.size);
            } else if ((!submitted || span.source >= blockSize) &&
                       uint64_t(address) + span.source + span.size <= 0x100000000ull) {
                copy(output + span.destination, pointer(address + span.source), span.size);
            } else {
                // Preserve per-entry submitted/guest selection and 32-bit EA
                // wrapping. Never replace these edge cases by a linear read.
                for (size_t i = 0; i < span.size; i += 16) {
                    const uint32_t offset = uint32_t(span.source + i);
                    const void* source = submitted && uint64_t(offset) + 16 <= blockSize
                        ? submitted + offset : pointer(uint32_t(address + offset));
                    std::memcpy(output + span.destination + i, source, 16);
                }
            }
        }
    }
};
}
