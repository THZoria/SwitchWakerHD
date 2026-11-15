// Host equivalence check for the optional specialized Vulkan index path.
#include "gfx/vulkan/index_conversion.h"
#include <cassert>
#include <cstdio>

static std::vector<uint32_t> reference(const void* data, uint32_t prim, uint32_t count,
                                      uint32_t type, bool restart, uint32_t marker) {
    auto read = [&](uint32_t i) -> uint32_t {
        if (!data) return i;
        const auto* bytes = static_cast<const uint8_t*>(data);
        uint32_t value;
        if (type == 0 || type == 4) {
            uint16_t word;
            std::memcpy(&word, bytes + size_t(i) * 2, 2);
            value = type == 4 ? __builtin_bswap16(word) : word;
        } else {
            std::memcpy(&value, bytes + size_t(i) * 4, 4);
            if (type == 9) value = __builtin_bswap32(value);
        }
        return restart && value == marker ? UINT32_MAX : value;
    };
    std::vector<uint32_t> out;
    switch (prim) {
    case 5:
        for (uint32_t i = 1; i + 1 < count; ++i)
            out.insert(out.end(), {read(0), read(i), read(i + 1)});
        break;
    case 0x13:
        for (uint32_t i = 0; i + 3 < count; i += 4)
            out.insert(out.end(), {read(i), read(i+1), read(i+2), read(i), read(i+2), read(i+3)});
        break;
    case 0x14:
        for (uint32_t i = 0; i + 3 < count; i += 2)
            out.insert(out.end(), {read(i), read(i+1), read(i+2), read(i+1), read(i+3), read(i+2)});
        break;
    case 0x12:
        for (uint32_t i = 0; i < count; ++i) out.push_back(read(i));
        out.push_back(read(0));
        break;
    }
    if (out.empty() && data)
        for (uint32_t i = 0; i < count; ++i) out.push_back(read(i));
    return out;
}

int main() {
    unsigned checks = 0;
    for (uint32_t type : {0u, 1u, 4u, 9u})
    for (uint32_t marker : {0u, 7u, 65535u, UINT32_MAX}) {
        std::vector<uint8_t> storage(1 + 72 * 4);
        void* data = storage.data() + 1;  // deliberately unaligned
        const bool narrow = type == 0 || type == 4;
        for (uint32_t i = 0; i < 72; ++i) {
            uint32_t v = i % 5 == 0 ? marker : (i * 1234567u) ^ (i << 17);
            if (narrow) {
                uint16_t word = uint16_t(v);
                if (type == 4) word = __builtin_bswap16(word);
                std::memcpy(static_cast<uint8_t*>(data) + i * 2, &word, 2);
            } else {
                if (type == 9) v = __builtin_bswap32(v);
                std::memcpy(static_cast<uint8_t*>(data) + i * 4, &v, 4);
            }
        }
        for (uint32_t prim : {1u, 2u, 3u, 4u, 5u, 6u, 0x12u, 0x13u, 0x14u})
        for (uint32_t count = 0; count <= 70; ++count)
        for (bool indexed : {false, true})
        for (bool restart : {false, true}) {
            const void* source = indexed ? data : nullptr;
            std::vector<uint32_t> actual;
            gfxvk::vk::convert_indices(source, prim, count, type, restart, marker, actual);
            assert(actual == reference(source, prim, count, type, restart, marker));
            ++checks;
        }
    }
    std::printf("specialized indices: %u equivalence cases passed\n", checks);
}
