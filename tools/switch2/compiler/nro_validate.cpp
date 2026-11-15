#include "nro_validate.h"
#include <cstdint>
#include <fstream>
#include <map>
#include <stdexcept>
#include <vector>

namespace switch2::compiler {
bool validate_cached_nro(const std::filesystem::path& path, std::string* error) {
    try {
        auto check = [](bool condition, const char* message) { if (!condition) throw std::runtime_error(message); };
        const auto size = std::filesystem::file_size(path);
        check(size >= 4096 && size <= (128ull << 20), "NRO size outside cache budget");
        std::ifstream input(path, std::ios::binary);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        check(bool(input), "Cannot read cached NRO");
        auto word = [&](uint64_t offset, unsigned width = 4) -> uint64_t {
            check(offset <= size && width <= size - offset, "NRO field outside file");
            uint64_t value = 0;
            for (unsigned i = 0; i < width; ++i) value |= uint64_t(bytes[size_t(offset + i)]) << (8 * i);
            return value;
        };
        check(word(0x10) == 0x304f524e && word(0x14) == 0 && word(0x1c) == 0, "Invalid NRO header/version/flags");
        const auto image = word(0x18), bss = word(0x38);
        check(image <= size && image + bss <= (128ull << 20) && !(bss & 4095), "Invalid NRO image/BSS");
        uint64_t next = 0, data_start = 0, code_end = 0;
        for (unsigned i = 0; i < 3; ++i) {
            const auto start = word(0x20 + i * 8), length = word(0x24 + i * 8);
            check(start == next && length && !(length & 4095) && length <= image && start <= image - length,
                  "NRO segments overlap or exceed image");
            next += length;
            if (i == 0) code_end = next;
            if (i == 2) data_start = start;
        }
        check(next == image, "NRO image does not match segment extents");
        const auto mod = word(4);
        check(mod >= 0x80 && mod + 28 <= code_end && word(mod) == 0x30444f4d, "Invalid NRO MOD0");
        bool ounce = false;
        for (uint64_t cursor = mod + 28, i = 0; i < 8 && cursor + 12 <= code_end; ++i, cursor += 12) {
            if (word(cursor) == 0x304e554f) {
                ounce = word(cursor + 4) == 2 && word(cursor + 8) == 1;
                break;
            }
            if ((word(cursor) & 0x00ffffff) != 0x00594e4c) break; // LNYx
        }
        check(ounce, "Cached NRO is not a supported Switch 2 libounce program");
        auto relative = [&](uint64_t at) {
            const int64_t address = int64_t(mod) + int32_t(word(at));
            check(address >= 0 && uint64_t(address) <= image + bss, "Invalid MOD0 relative offset");
            return uint64_t(address);
        };
        const auto dynamic = relative(mod + 4);
        const auto bss_start = relative(mod + 8), bss_end = relative(mod + 12);
        check(bss_start >= data_start && bss_start <= image && bss_end >= bss_start && bss_end <= image + bss,
              "MOD0 BSS exceeds RW memory");
        check(dynamic >= data_start && dynamic < image, "Dynamic table is not in RW data");
        std::map<uint64_t, uint64_t> tags;
        bool terminated = false;
        for (uint64_t cursor = dynamic, entries = 0; entries < 4096 && cursor + 16 <= image; ++entries, cursor += 16) {
            const auto tag = word(cursor, 8), value = word(cursor + 8, 8);
            if (!tag) { terminated = true; break; }
            check(tag != 1 && tag != 22, "Dynamic imports/text relocations are forbidden");
            check(tags.emplace(tag, value).second, "Duplicate dynamic table tag");
        }
        check(terminated, "Unterminated dynamic table");
        auto range = [&](uint64_t address, uint64_t length) {
            check(address <= image && length <= image - address, "Dynamic table range outside NRO image");
        };
        auto destination = [&](uint64_t address) {
            check(!(address & 7) && address >= data_start && address <= image + bss - 8,
                  "Relocation writes outside aligned RW memory");
        };
        check(!tags[17] && !tags[18] && !tags[23], "Unexpected REL/PLT relocation tables");
        const auto hash = tags[4], symbols = tags[6], strings = tags[5], string_size = tags[10];
        check(hash && symbols && strings && tags[11] == 24, "Missing bounded static-runtime symbol tables");
        range(hash, 8);
        const auto buckets = word(hash), count = word(hash + 4);
        check(count > 0 && count <= 1000000 && buckets <= 1000000, "Invalid ELF hash symbol count");
        range(hash, 8 + 4 * (count + buckets));
        range(symbols, count * 24);
        range(strings, string_size);
        for (uint64_t i = 1; i < count; ++i) {
            const auto at = symbols + i * 24;
            check(word(at, 4) < string_size && word(at + 6, 2) != 0, "Unresolved dynamic symbol in cached NRO");
        }
        if (tags[8]) { // DT_RELASZ
            check(tags[9] == 24 && !(tags[8] % 24), "Invalid RELA entry size");
            range(tags[7], tags[8]);
            for (uint64_t at = tags[7]; at < tags[7] + tags[8]; at += 24) {
                check(word(at + 8, 8) == 1027, "Only R_AARCH64_RELATIVE relocations are supported");
                destination(word(at, 8));
                check(word(at + 16, 8) <= image + bss, "Relative relocation addend outside image");
            }
        }
        if (tags[35]) { // DT_RELRSZ
            check(tags[37] == 8 && !(tags[35] & 7), "Invalid RELR entry size");
            range(tags[36], tags[35]);
            uint64_t base = 0;
            for (uint64_t at = tags[36]; at < tags[36] + tags[35]; at += 8) {
                const auto entry = word(at, 8);
                if (!(entry & 1)) {
                    destination(entry);
                    base = entry + 8;
                } else {
                    check(base >= data_start && base <= image + bss && image + bss - base >= 63 * 8,
                          "RELR bitmap outside RW memory");
                    for (unsigned bit = 1; bit < 64; ++bit)
                        if (entry & (uint64_t(1) << bit)) destination(base + (bit - 1) * 8);
                    base += 63 * 8;
                }
            }
        }
        if (size > image) {
            check(size - image >= 56 && word(image) == 0x54455341 && word(image + 4) == 0, "Invalid NRO assets");
            for (unsigned i = 0; i < 3; ++i) {
                const auto offset = word(image + 8 + 16 * i, 8), length = word(image + 16 + 16 * i, 8);
                check(!length || (offset >= 56 && offset <= size - image && length <= size - image - offset),
                      "NRO asset exceeds file bounds");
            }
        }
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}
}
