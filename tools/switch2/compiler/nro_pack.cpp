#include "nro_pack.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <vector>

namespace switch2::compiler {
namespace {
using Bytes = std::vector<uint8_t>;
constexpr uint64_t max_image = 128ull << 20;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
uint64_t le(const Bytes& bytes, size_t offset, size_t width) {
    require(offset <= bytes.size() && width <= bytes.size() - offset, "Truncated binary field");
    uint64_t value = 0;
    for (size_t i = 0; i < width; ++i) value |= uint64_t(bytes[offset + i]) << (8 * i);
    return value;
}
void put(Bytes& bytes, size_t offset, uint64_t value, size_t width = 4) {
    require(offset <= bytes.size() && width <= bytes.size() - offset, "Invalid NRO header field");
    for (size_t i = 0; i < width; ++i) bytes[offset + i] = uint8_t(value >> (8 * i));
}
uint64_t page(uint64_t value) {
    require(value <= max_image, "NRO segment exceeds image budget");
    return (value + 4095) & ~uint64_t(4095);
}
struct Input {
    std::ifstream stream;
    uint64_t size;
    explicit Input(const std::filesystem::path& path) : stream(path, std::ios::binary) {
        require(bool(stream), "Cannot open NRO packaging input");
        stream.seekg(0, std::ios::end);
        const auto end = stream.tellg();
        require(end >= 0, "Cannot determine packaging input size");
        size = uint64_t(end);
    }
    Bytes read(uint64_t offset, uint64_t count) {
        require(offset <= size && count <= size - offset && count <= max_image, "ELF/file range outside input");
        Bytes bytes(static_cast<size_t>(count));
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(offset));
        if (count) stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count));
        require(bool(stream), "Cannot read packaging input");
        return bytes;
    }
};
struct Segment { uint64_t offset, address, size, memory; uint32_t flags; };
Bytes asset(const std::filesystem::path& path, uint64_t limit) {
    if (path.empty()) return {};
    Input input(path);
    require(input.size && input.size <= limit, "Invalid NRO asset size");
    return input.read(0, input.size);
}
}

bool pack_nro(const NroPackRequest& request, std::string& error) {
    error.clear();
    try {
        require(!request.elf.empty() && !request.output.empty(), "Missing ELF or staged NRO path");
        // A caller must choose a fresh staging filename; never truncate an
        // existing installation, user file or symlink (including dangling ones).
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(request.output, ec);
        require((!ec && status.type() == std::filesystem::file_type::not_found) ||
                ec == std::errc::no_such_file_or_directory, "NRO output already exists or is inaccessible");
        Input input(request.elf);
        const auto header = input.read(0, 64);
        require(le(header, 0, 4) == 0x464c457f && header[4] == 2 && header[5] == 1 && header[6] == 1,
                "Expected little-endian ELF64");
        require(le(header, 16, 2) == 3 && le(header, 18, 2) == 183 && le(header, 20, 4) == 1 &&
                le(header, 24, 8) == 0 && le(header, 52, 2) == 64, "Expected linked AArch64 homebrew ELF");
        const auto phoff = le(header, 32, 8), phnum = le(header, 56, 2);
        require(le(header, 54, 2) == 56 && phnum > 0 && phnum <= 64, "Invalid ELF program headers");
        const auto ph = input.read(phoff, phnum * 56);
        std::vector<Segment> segments;
        for (size_t i = 0; i < phnum; ++i) {
            const auto base = i * 56;
            if (le(ph, base, 4) != 1) continue;
            Segment s{le(ph, base + 8, 8), le(ph, base + 16, 8), le(ph, base + 32, 8),
                      le(ph, base + 40, 8), uint32_t(le(ph, base + 4, 4))};
            require(s.size && s.size <= s.memory && s.memory <= max_image &&
                    s.offset <= input.size && s.size <= input.size - s.offset,
                    "Invalid ELF load segment bounds");
            segments.push_back(s);
        }
        require(segments.size() == 3, "Expected exactly RX, R and RW load segments");
        uint64_t total = 0;
        constexpr uint32_t flags[] = {5, 4, 6};
        for (size_t i = 0; i < 3; ++i) {
            const auto& s = segments[i];
            require(s.flags == flags[i] && s.address == total && (i == 2 || s.size == s.memory),
                    "Unsupported ELF segment permissions, gaps, order or zero-fill");
            total += page(s.size);
            require(total <= max_image, "NRO exceeds image budget");
        }
        require(segments[0].size >= 0x80, "ELF lacks reserved NRO header space");
        const auto& data = segments[2];
        const auto bss = data.memory > page(data.size) ? page(data.memory - page(data.size)) : 0;
        require(total + bss <= max_image, "NRO plus BSS exceeds image budget");
        Bytes image(static_cast<size_t>(total));
        for (const auto& s : segments) {
            const auto bytes = input.read(s.offset, s.size);
            std::copy(bytes.begin(), bytes.end(), image.begin() + static_cast<size_t>(s.address));
        }
        const auto mod = le(image, 4, 4);
        require(mod >= 0x80 && mod <= segments[0].size - 28 && le(image, size_t(mod), 4) == 0x30444f4d,
                "Missing MOD0 startup metadata");
        std::fill(image.begin() + 0x10, image.begin() + 0x80, 0);
        put(image, 0x10, 0x304f524e); // NRO0
        put(image, 0x18, total);
        for (size_t i = 0; i < 3; ++i) {
            put(image, 0x20 + 8 * i, segments[i].address);
            put(image, 0x24 + 8 * i, page(segments[i].size));
        }
        put(image, 0x38, bss);
        // GNU build ID is metadata, not recomputed from the private output.
        const auto shoff = le(header, 40, 8), shnum = le(header, 60, 2);
        if (shnum) {
            require(le(header, 58, 2) == 64, "Invalid ELF section header size");
            const auto sections = input.read(shoff, shnum * 64);
            for (size_t i = 0; i < shnum; ++i) {
                if (le(sections, i * 64 + 4, 4) != 7) continue;
                const auto notes = input.read(le(sections, i * 64 + 24, 8), le(sections, i * 64 + 32, 8));
                size_t cursor = 0;
                while (cursor < notes.size()) {
                    require(notes.size() - cursor >= 12, "Truncated ELF note");
                    const auto names = le(notes, cursor, 4), desc = le(notes, cursor + 4, 4);
                    const auto type = le(notes, cursor + 8, 4);
                    const auto padded_name = (names + 3) & ~uint64_t(3);
                    const auto padded_desc = (desc + 3) & ~uint64_t(3);
                    const auto remaining = notes.size() - cursor - 12;
                    require(padded_name <= remaining && padded_desc <= remaining - padded_name,
                            "ELF note exceeds section bounds");
                    if (type == 3 && names == 4 && le(notes, cursor + 12, 4) == 0x00554e47) {
                        std::copy_n(notes.begin() + cursor + 12 + size_t(padded_name),
                                    size_t(std::min<uint64_t>(desc, 32)), image.begin() + 0x40);
                    }
                    cursor += 12 + size_t(padded_name + padded_desc);
                }
            }
        }
        const auto icon = asset(request.icon, 1 << 20), nacp = asset(request.nacp, 0x4000);
        require(nacp.empty() || nacp.size() == 0x4000, "NACP must be exactly 16 KiB");
        if (!icon.empty() || !nacp.empty()) {
            Bytes assets(56);
            put(assets, 0, 0x54455341); // ASET
            uint64_t offset = assets.size();
            if (!icon.empty()) { put(assets, 8, offset, 8); put(assets, 16, icon.size(), 8); offset += icon.size(); }
            if (!nacp.empty()) { put(assets, 24, offset, 8); put(assets, 32, nacp.size(), 8); }
            const auto start = image.size();
            image.resize(start + assets.size() + icon.size() + nacp.size());
            std::copy(assets.begin(), assets.end(), image.begin() + start);
            std::copy(icon.begin(), icon.end(), image.begin() + start + assets.size());
            std::copy(nacp.begin(), nacp.end(), image.begin() + start + assets.size() + icon.size());
        }
        int open_flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_BINARY
        open_flags |= O_BINARY;
#endif
#ifdef _WIN32
        const int fd = ::_wopen(request.output.c_str(), open_flags, 0600);
#else
        const int fd = ::open(request.output.string().c_str(), open_flags, 0600);
#endif
        require(fd >= 0, "Cannot exclusively create staged NRO");
#ifdef _WIN32
        FILE* raw = ::_fdopen(fd, "wb");
        if (!raw) { ::_close(fd); throw std::runtime_error("Cannot open staged NRO stream"); }
#else
        FILE* raw = ::fdopen(fd, "wb");
        if (!raw) { ::close(fd); throw std::runtime_error("Cannot open staged NRO stream"); }
#endif
        std::unique_ptr<FILE, decltype(&std::fclose)> output(raw, &std::fclose);
        require(std::fwrite(image.data(), 1, image.size(), output.get()) == image.size() &&
                std::fflush(output.get()) == 0, "Cannot write staged NRO");
        require(std::fclose(output.release()) == 0, "Cannot close staged NRO");
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
}
