#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace gfxvk {
// Local to one draw: binding writes have no observable effect until the draw.
// Emit consecutive bindings in a single API call; never fill sparse holes with
// null buffers, since they may still be used by the active pipeline.
template<class Buffer, class Offset, size_t Capacity = 32>
class VertexBindBatch {
    static_assert(Capacity > 0 && Capacity <= 32);
    std::array<Buffer, Capacity> buffers_;
    std::array<Offset, Capacity> offsets_;
    uint32_t pending_ = 0;
public:
    bool set(uint32_t binding, Buffer buffer, Offset offset) {
        if (binding >= Capacity) return false;
        buffers_[binding] = buffer;
        offsets_[binding] = offset;
        pending_ |= uint32_t{1} << binding;
        return true;
    }
    template<class Emit>
    void flush(Emit emit) {
        while (pending_) {
            const uint32_t first = std::countr_zero(pending_);
            uint32_t end = first + 1;
            while (end < Capacity && (pending_ & (uint32_t{1} << end))) ++end;
            emit(first, end - first, buffers_.data() + first, offsets_.data() + first);
            // 64-bit mask avoids shifting a 32-bit word by 32 for a full run.
            const uint32_t mask = uint32_t(((uint64_t{1} << (end - first)) - 1) << first);
            pending_ &= ~mask;
        }
    }
};
}
