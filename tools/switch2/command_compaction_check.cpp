#include "gfx/vulkan/vertex_bind_batch.h"
#include "gfx/vulkan/validated_index_cache.h"
#include "gfx/vulkan/index_conversion.h"
#include <cassert>
#include <cstdio>
#include <random>

static void vertex_bindings() {
    std::mt19937 random(0x42494e44);
    unsigned inputCommands = 0, outputCommands = 0;
    for (unsigned draw = 0; draw < 20000; ++draw) {
        gfxvk::VertexBindBatch<uint64_t, uint64_t> batch;
        std::array<uint64_t, 64> expectedBuffers{}, expectedOffsets{}, actualBuffers{}, actualOffsets{};
        for (unsigned i = 0; i < 64; ++i) {
            expectedBuffers[i] = actualBuffers[i] = 100 + i;
            expectedOffsets[i] = actualOffsets[i] = i * 128;
        }
        auto emit = [&](uint32_t first, uint32_t count, const uint64_t* buffers, const uint64_t* offsets) {
            assert(count && first + count <= 64);
            for (uint32_t i = 0; i < count; ++i) {
                actualBuffers[first + i] = buffers[i];
                actualOffsets[first + i] = offsets[i];
            }
            ++outputCommands;
        };
        const unsigned requests = draw % 65;
        for (unsigned i = 0; i < requests; ++i) {
            const uint32_t binding = random() % (draw % 2 ? 40 : 32);
            const uint64_t buffer = random() | uint64_t{1}, offset = random();
            expectedBuffers[binding] = buffer;
            expectedOffsets[binding] = offset;
            if (!batch.set(binding, buffer, offset)) {
                batch.flush(emit);
                emit(binding, 1, &buffer, &offset);
            }
            ++inputCommands;
        }
        batch.flush(emit);
        assert(expectedBuffers == actualBuffers && expectedOffsets == actualOffsets);
        const unsigned previous = outputCommands;
        batch.flush(emit);
        assert(outputCommands == previous); // a second flush is empty
    }
    gfxvk::VertexBindBatch<uint64_t, uint64_t> full;
    for (unsigned i = 0; i < 32; ++i) assert(full.set(i, i + 1, i * 4));
    unsigned calls = 0;
    full.flush([&](uint32_t first, uint32_t count, const uint64_t*, const uint64_t*) {
        assert(first == 0 && count == 32);
        ++calls;
    });
    assert(calls == 1);
    std::printf("vertex binding compaction: 20000 randomized draws preserve final binding state (%u -> %u mock calls)\n",
                inputCommands, outputCommands);
}

struct Slice { uint64_t buffer = 0; };
static void index_cache() {
    using Cache = gfxvk::ValidatedIndexCache<Slice, 1>; // force collisions
    Cache cache;
    std::array<uint8_t, 16> guest{0, 0, 0, 1, 0, 2, 0, 3, 255, 255, 0, 5, 0, 6, 0, 7};
    const Cache::Key key{0x1000, 8, 6, 4, 1, 65535};
    bool hit;
    auto* entry = cache.lookup(key, guest.data(), guest.size(), 8, hit);
    assert(entry && !hit && !entry->ready);
    // Factory must convert the shadow, not reread a mutable guest payload.
    guest[3] = 10;
    assert(entry->source[3] == 1);
    gfxvk::vk::convert_indices(entry->source.data(), 6, 8, 4, true, 65535, entry->indices);
    entry->ready = true;
    guest[3] = 1;
    auto* again = cache.lookup(key, guest.data(), guest.size(), 8, hit);
    assert(hit && again == entry && again->indices[1] == 1 && again->indices[4] == UINT32_MAX);
    unsigned uploads = 0;
    std::vector<std::vector<uint32_t>> gpu;
    auto upload = [&](const void* bytes, size_t size) {
        const auto* values = static_cast<const uint32_t*>(bytes);
        gpu.emplace_back(values, values + size / 4);
        return Slice{++uploads};
    };
    auto a = entry->upload(1, 1, upload);
    auto b = entry->upload(1, 1, upload);
    assert(a.buffer == b.buffer && uploads == 1 && gpu.back() == entry->indices);
    entry->upload(1, 2, upload);
    entry->upload(2, 2, upload);
    assert(uploads == 3); // generations/devices never share GPU slices
    guest[3] = 2;
    entry = cache.lookup(key, guest.data(), guest.size(), 8, hit);
    assert(!hit && !entry->ready && !entry->slice.buffer && !entry->extentReady);
    gfxvk::vk::convert_indices(entry->source.data(), 6, 8, 4, true, 65535, entry->indices);
    entry->ready = true;
    assert(entry->indices[1] == 2 && gpu.front()[1] == 1); // old GPU payload stays immutable
    auto other = key;
    other[5] = 7;
    entry = cache.lookup(other, guest.data(), guest.size(), 8, hit);
    assert(!hit && !entry->slice.buffer);
    // Uncommitted/failed conversions must never turn into hits.
    cache.lookup(other, guest.data(), guest.size(), 8, hit);
    assert(!hit);
    entry->indices = {99};
    entry->ready = true;
    cache.reset();
    cache.lookup(other, guest.data(), guest.size(), 8, hit);
    assert(!hit);
    assert(!cache.lookup(key, nullptr, Cache::maxSourceBytes + 1, 8, hit));
    assert(!cache.lookup(key, nullptr, 0, 8, hit));
    assert(!cache.lookup(key, nullptr, 16, Cache::maxOutputIndices + 1, hit));
    std::puts("validated indices: changed bytes, collisions, pending conversions, immutable uploads, device/generation/reset and bounds passed");
}

static void index_cache_stream() {
    using Cache = gfxvk::ValidatedIndexCache<Slice, 8>;
    Cache cache;
    std::mt19937 random(0x494e4458);
    std::array<std::array<uint8_t, 1024>, 16> sources;
    for (auto& bytes : sources) for (auto& byte : bytes) byte = uint8_t(random());
    std::vector<std::vector<uint32_t>> uploaded;
    unsigned last = 0, hits = 0;
    for (unsigned draw = 0; draw < 10000; ++draw) {
        const unsigned slot = draw % 2 ? last : random() % sources.size();
        last = slot;
        auto& bytes = sources[slot];
        const uint32_t count = 32 + slot * 7;
        const uint32_t type = slot % 2 ? 4 : 9;
        const uint32_t primitives[]{1, 2, 3, 4, 5, 6, 0x12, 0x13, 0x14};
        const uint32_t prim = primitives[slot % 9];
        const bool restart = prim == 3 || prim == 6;
        const uint32_t marker = slot % 3 ? 7 : 65535;
        if (random() % 7 == 0) bytes[random() % (count * (type == 4 ? 2 : 4))] ^= 1;
        if (draw % 199 == 0) cache.reset();
        std::vector<uint32_t> reference;
        gfxvk::vk::convert_indices(bytes.data(), prim, count, type, restart, marker, reference);
        const Cache::Key key{0x1000u + slot * 4096u, count, prim, type, uint32_t(restart), restart ? marker : 0};
        bool hit;
        auto* entry = cache.lookup(key, bytes.data(), count * (type == 4 ? 2 : 4), reference.size(), hit);
        assert(entry);
        hits += hit;
        if (!hit) {
            gfxvk::vk::convert_indices(entry->source.data(), prim, count, type, restart, marker, entry->indices);
            entry->ready = true;
        }
        assert(entry->indices == reference);
        const auto slice = entry->upload(1, draw / 7, [&](const void* data, size_t size) {
            const auto* values = static_cast<const uint32_t*>(data);
            uploaded.emplace_back(values, values + size / 4);
            return Slice{uploaded.size()};
        });
        assert(uploaded.at(slice.buffer - 1) == reference);
    }
    assert(hits > 1000);
    std::printf("validated indices: 10000 mixed primitive/endian/restart draws match fresh conversion (%u validated hits)\n", hits);
}

int main() {
    vertex_bindings();
    index_cache();
    index_cache_stream();
}
