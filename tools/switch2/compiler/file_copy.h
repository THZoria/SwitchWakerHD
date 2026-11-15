#pragma once
#include <array>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <system_error>
#include <unistd.h>

namespace switch2::compiler {
// libstdc++ copy_file uses host primitives that can return ENOSYS on Horizon.
// Use only libounce's ordinary file I/O, also for RomFS -> SD extraction.
// Never overwrite a destination; remove only our own incomplete output.
inline void copy_file_exclusive(const std::filesystem::path& source,
                                const std::filesystem::path& destination) {
    struct Descriptor {
        int value;
        ~Descriptor() { if (value >= 0) ::close(value); }
    };
    const auto fail = [&](const char* operation, int error) {
        throw std::system_error(error, std::generic_category(),
            std::string(operation) + " [" + source.string() + "] [" + destination.string() + "]");
    };
    int binary = 0;
#ifdef O_BINARY
    binary = O_BINARY;
#endif
    Descriptor input{::open(source.c_str(), O_RDONLY | binary)};
    if (input.value < 0) fail("Cannot open copy source", errno);
    Descriptor output{::open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | binary, 0666)};
    if (output.value < 0) fail("Cannot create copy destination", errno);
    try {
        std::array<char, 64 * 1024> buffer;
        for (;;) {
            ssize_t count;
            do { count = ::read(input.value, buffer.data(), buffer.size()); } while (count < 0 && errno == EINTR);
            if (count < 0) fail("Cannot read copy source", errno);
            if (!count) break;
            ssize_t offset = 0;
            while (offset < count) {
                ssize_t written;
                do { written = ::write(output.value, buffer.data() + offset, size_t(count - offset)); }
                while (written < 0 && errno == EINTR);
                if (written <= 0) fail("Cannot write copy destination", written < 0 ? errno : EIO);
                offset += written;
            }
        }
        const int descriptor = output.value;
        output.value = -1;
        if (::close(descriptor) != 0) fail("Cannot close copy destination", errno);
    } catch (...) {
        if (output.value >= 0) { ::close(output.value); output.value = -1; }
        ::unlink(destination.c_str());
        throw;
    }
}
}
