// Exact-byte equivalence with the old per-vec4 packer. No Vulkan/console needed.
#include "gfx/vulkan/uniform_copy_plan.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <random>

using gfxvk::vk::UniformCopyPlan;
struct Entry { uint32_t source; int64_t destination; };

int main() {
    std::mt19937 random(0x57574844);
    std::array<uint8_t, 4096> guest{}, submitted{}, high{};
    unsigned checks = 0;
    for (unsigned trial = 0; trial < 1000; ++trial) {
        for (size_t i = 0; i < guest.size(); ++i) {
            guest[i] = uint8_t(random());
            submitted[i] = uint8_t(random());
            high[i] = uint8_t(random());
        }
        std::vector<Entry> entries;
        UniformCopyPlan plan;
        uint32_t source = 0;
        int64_t destination = 0;
        for (unsigned i = 0; i < 50; ++i) {
            if (random() % 4 == 0) {
                source = random() % 512;
                destination = int64_t(random() % 1056) - 16;
            }
            entries.push_back({source, destination});
            plan.append(source, destination, 1024);
            source += 16;
            destination += 16;
        }
        auto valid = [](const Entry& e) {
            return e.destination >= 0 && uint64_t(e.destination) + 16 <= 1024;
        };
        std::array<uint8_t, 1024> actual{}, expected{};
        plan.registers(actual.data(), guest.data());
        for (const auto& e : entries)
            if (valid(e)) std::memcpy(expected.data() + e.destination, guest.data() + e.source, 16);
        assert(actual == expected);
        ++checks;
        for (uint32_t address : {32u, UINT32_MAX - 255u})
        for (size_t blockSize : {0u, 1u, 15u, 16u, 17u, 31u, 32u, 63u, 64u, 127u, 255u, 512u, 2048u})
        for (bool useSubmitted : {false, true}) {
            auto pointer = [&](uint32_t ea) -> const uint8_t* {
                return ea >= UINT32_MAX - 255u ? high.data() + (ea - (UINT32_MAX - 255u))
                                               : guest.data() + ea;
            };
            const auto* frozen = useSubmitted ? submitted.data() : nullptr;
            actual.fill(0);
            expected.fill(0);
            plan.guest(actual.data(), address, blockSize, frozen, pointer);
            for (const auto& e : entries) {
                if (!valid(e)) continue;
                const auto* bytes = frozen && uint64_t(e.source) + 16 <= blockSize
                    ? frozen + e.source : pointer(uint32_t(address + e.source));
                std::memcpy(expected.data() + e.destination, bytes, 16);
            }
            assert(actual == expected);
            ++checks;
        }
    }
    UniformCopyPlan merged;
    merged.append(0, 0, 128);
    merged.append(16, 16, 128);
    merged.append(32, 32, 128);
    assert(merged.spans.size() == 1 && merged.spans[0].size == 48);
    merged.append(64, 48, 128);  // source gap must not be copied
    assert(merged.spans.size() == 2);
    std::array<uint8_t, 128> fresh{}, packed{};
    merged.registers(packed.data(), fresh.data());
    fresh[17] = 42;  // same address/metadata, changed bytes must never be cached
    fresh[65] = 91;
    merged.registers(packed.data(), fresh.data());
    assert(packed[17] == 42 && packed[49] == 91);
    UniformCopyPlan wrap;
    wrap.append(UINT32_MAX - 15, 0, 128);
    wrap.append(0, 16, 128);  // do not coalesce a wrapped source offset
    assert(wrap.spans.size() == 2);
    std::printf("CPU uniform copy plans: %u exact-byte cases passed\n", checks);
}
