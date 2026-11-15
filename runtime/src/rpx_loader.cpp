// The RPX loader extracted unchanged from core.cpp so platform bring-up can
// validate the real loader without linking guest dispatch/execution.
#include "runtime.h"
#include "crash_memory.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>
#include <zlib.h>

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

bool load_rpx(const std::string& path, LoadedModule& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    uint32_t shoff = be32(&d[0x20]);
    uint16_t shentsize = be16(&d[0x2E]), shnum = be16(&d[0x30]);
    out.entry = be32(&d[0x18]);
    out.data_end = 0;
    std::vector<crash_memory::Section> diagnostic_sections;
    size_t diagnostic_bytes = 0;
    for (int i = 0; i < shnum; i++) {
        const uint8_t* sh = &d[shoff + i * shentsize];
        uint32_t type = be32(sh + 4), flags = be32(sh + 8), addr = be32(sh + 12), off = be32(sh + 16), size = be32(sh + 20);
        std::vector<uint8_t> data;
        if (type != 8 && size) {
            if (flags & 0x08000000) {
                uLongf dlen = be32(&d[off]);
                data.resize(dlen);
                if (uncompress(data.data(), &dlen, &d[off + 4], size - 4) != Z_OK) fatal("zlib error in section %d", i);
            } else {
                data.assign(d.begin() + off, d.begin() + off + size);
            }
        }
        if (type == 0x80000004 && data.size() >= 0x30) {  // RPL file info
            out.sda_base = be32(&data[0x24]);
            out.sda2_base = be32(&data[0x28]);
            out.stack_size = be32(&data[0x2C]);
        }
        if (crash_memory::enabled() && (flags & 2)) {
            crash_memory::Section section{addr, flags, {}, type == 8 ? uint64_t(size) : uint64_t(data.size())};
            if ((flags & 7) == 6 && addr >= 0x02000000 && uint64_t(addr) + data.size() <= 0x10000000 && data.size() <= 16 * 1024 * 1024 - diagnostic_bytes) {
                section.bytes = data; diagnostic_bytes += data.size();
            }
            diagnostic_sections.push_back(std::move(section));
        }
        // allocated sections in the text/data regions
        if ((flags & 2) && addr >= 0x02000000 && addr < 0xC0000000) {
            if (type == 8) {
                memset(mem::ptr(addr), 0, size);
                out.data_end = std::max(out.data_end, addr + size);
            } else {
                memcpy(mem::ptr(addr), data.data(), data.size());
                if (addr >= mem::kMem2Start) out.data_end = std::max(out.data_end, addr + (uint32_t)data.size());
            }
        }
    }
    crash_memory::install(std::move(diagnostic_sections), PPC_MEM_BASE);
    return true;
}
