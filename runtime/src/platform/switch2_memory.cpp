#include "switch2_memory.h"
#include "switch2_memory_policy.h"
#include "../runtime.h"
#include <switch.h>
#include <array>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

#ifndef __SWITCH2__
#error "Switch 2 memory mappings require libounce"
#endif

namespace switch2::memory {
namespace {
struct Range { uint32_t start, size; };
constexpr std::array<Range, 6> ranges{{
    {0x00010000, 0x04000000 - 0x00010000},
    {mem::kMem2Start, mem::kMem2End - mem::kMem2Start},
    {mem::kRuntimeStart, mem::kRuntimeEnd - mem::kRuntimeStart},
    {0xC0000000, 0x02100000},
    {mem::kFgBucket, mem::kFgBucketSize},
    {mem::kMem1, mem::kMem1Size},
}};
static_assert([] {
    for (size_t i = 0; i < ranges.size(); ++i) {
        const auto& a = ranges[i];
        const uint64_t end = uint64_t(a.start) + a.size;
        if (a.start < 0x10000 || !a.size || ((a.start | a.size) & 0xfff) || end > 0x100000000ull) return false;
        for (size_t j = 0; j < i; ++j) {
            const auto& b = ranges[j];
            if (uint64_t(a.start) < uint64_t(b.start) + b.size && uint64_t(b.start) < end) return false;
        }
    }
    return true;
}(), "guest ranges must be page-aligned, non-overlapping and preserve the null guard");
struct Mapping { void* backing{}; bool mapped{}; };
std::array<Mapping, ranges.size()> mappings;
uint8_t* window{};
VirtmemReservation* reservation{};
Handle process = INVALID_HANDLE;
uint64_t total{};
bool dense_heap = false;
using policy::guest_bytes;
void checked(Result rc, const char* operation) {
    if (R_FAILED(rc))
        throw std::runtime_error(std::string(operation) + " failed: Result " + std::to_string(rc));
}
}
uint64_t backed_bytes() { return total; }

void initialize() {
    if (window) throw std::runtime_error("guest window already reserved");
    const bool permission_svc = envIsSyscallHinted(0x73);
    const bool map_svc = envIsSyscallHinted(0x77);
    const bool unmap_svc = envIsSyscallHinted(0x78);
    process = envGetOwnProcessHandle();
    LOG("[mem sw2] SVC hints: permission(73)=%d map(77)=%d unmap(78)=%d; own handle=%s",
        permission_svc, map_svc, unmap_svc, process != INVALID_HANDLE ? "present" : "absent");
    const uint64_t heap_free = heapGetFreeSize();
    LOG("[mem sw2] libounce heap: total=%llu MiB, free=%llu MiB",
        static_cast<unsigned long long>(heapGetTotalSize() >> 20),
        static_cast<unsigned long long>(heap_free >> 20));
    const auto backend = policy::choose(permission_svc && map_svc && unmap_svc && process != INVALID_HANDLE, heap_free);
    if (backend != policy::Backend::Sparse) {
        // kaiHEN's game policy intentionally disallows process-code mapping
        // SVCs (libkaihen/JIT.md). The guest RPX is data, not host executable
        // memory: native code is already in the NRO. A dense heap window needs
        // no JIT helper, process handle, alias mappings or permission changes.
        if (backend == policy::Backend::Unavailable)
            throw std::runtime_error("sparse mapping unavailable and heap too small for 4 GiB guest data plus 256 MiB host headroom");
        LOG("[mem sw2] sparse mapping unavailable; allocating 4096 MiB from the ordinary data heap");
        auto* allocation = static_cast<uint8_t*>(aligned_alloc(0x10000, guest_bytes));
        if (!allocation) throw std::runtime_error("4 GiB contiguous heap allocation failed (possible fragmentation)");
        window = allocation;
        dense_heap = true;
        total = guest_bytes;
        LOG("[mem sw2] zeroing 4096 MiB; this can take a moment");
        std::memset(window, 0, guest_bytes);
        ppc_mem_base_var = window;
        LOG("[mem sw2] dense guest window ready at %p, 4096 MiB backed", window);
        LOG("[mem sw2] dense backend: null area and gaps are zero-filled, not protected/unmapped");
        return;
    }
    virtmemLock();
    window = static_cast<uint8_t*>(virtmemFindCodeMemory(0x100000000ull, 0x10000));
    if (window) reservation = virtmemAddReservation(window, 0x100000000ull);
    virtmemUnlock();
    if (!reservation) { window = nullptr; throw std::runtime_error("cannot reserve a 4 GiB guest window"); }
    LOG("[mem sw2] reserved guest window at %p", window);
    try {
        for (size_t i = 0; i < ranges.size(); ++i) {
            const auto& range = ranges[i];
            auto& mapping = mappings[i];
            LOG("[mem sw2] backing %08X +%08X (%u MiB)", range.start, range.size, range.size >> 20);
            mapping.backing = aligned_alloc(0x1000, range.size);
            if (!mapping.backing) throw std::runtime_error("insufficient RAM for guest range");
            std::memset(mapping.backing, 0, range.size);
            checked(svcMapProcessCodeMemory(process, reinterpret_cast<u64>(window + range.start),
                                           reinterpret_cast<u64>(mapping.backing), range.size), "svcMapProcessCodeMemory");
            mapping.mapped = true;
            checked(svcSetProcessMemoryPermission(process, reinterpret_cast<u64>(window + range.start),
                                                  range.size, Perm_Rw), "svcSetProcessMemoryPermission");
            total += range.size;
        }
        ppc_mem_base_var = window;
        LOG("[mem sw2] ready: %llu MiB backed; 64 KiB null guard unmapped",
            static_cast<unsigned long long>(total >> 20));
    } catch (...) {
        release();
        throw;
    }
}

bool release() noexcept {
    if (dense_heap) {
        // No aliases exist in this backend. Clearing the published pointer and
        // freeing the allocation releases it entirely before hbmenu resumes.
        ppc_mem_base_var = nullptr;
        std::free(window);
        window = nullptr;
        dense_heap = false;
        total = 0;
        process = INVALID_HANDLE;
        LOG("[mem sw2] dense guest heap window freed (no process mappings were used)");
        return true;
    }
    bool complete = true;
    for (size_t i = ranges.size(); i-- > 0;) {
        auto& mapping = mappings[i];
        if (mapping.mapped) {
            const auto& range = ranges[i];
            const Result rc = svcUnmapProcessCodeMemory(process, reinterpret_cast<u64>(window + range.start),
                                                       reinterpret_cast<u64>(mapping.backing), range.size);
            if (R_FAILED(rc)) {
                LOG("[mem sw2] unmap %08X failed: 0x%08X; backing retained", range.start, rc);
                complete = false;
                continue;
            }
            mapping.mapped = false;
        }
        std::free(mapping.backing);
        mapping.backing = nullptr;
    }
    if (complete && reservation) {
        virtmemLock();
        virtmemRemoveReservation(reservation);
        virtmemUnlock();
        reservation = nullptr;
        window = nullptr;
        ppc_mem_base_var = nullptr;
        total = 0;
        LOG("[mem sw2] all aliases and backing released");
    }
    return complete;
}
}  // namespace switch2::memory
