#pragma once
#include "file_copy.h"
#include "mods/mod_hash.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace switch2::compiler {
struct CacheIdentity {
    std::string rpx_sha256, recompiler_revision, runtime_abi, target_flags;
};

inline bool sha256_hex(const std::string& value) {
    return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
inline std::string cache_key(const CacheIdentity& identity) {
    if (!sha256_hex(identity.rpx_sha256) || identity.recompiler_revision.empty() ||
        identity.runtime_abi.empty() || identity.target_flags.empty())
        throw std::invalid_argument("Incomplete native compilation cache identity");
    std::string description = "WWHD-NATIVE-CACHE-1\n";
    for (const auto* part : {&identity.rpx_sha256, &identity.recompiler_revision, &identity.runtime_abi, &identity.target_flags})
        description += std::to_string(part->size()) + ":" + *part;
    return mods::hash::sha256_text(description);
}

// The validator must check a linked module, imports, relocations and ABI, NOT
// just an ELF magic number. No entry can be published or reused without it.
// Digests detect corruption; they do not authenticate untrusted SD-card code.
class NativeCache {
public:
    using Validator = std::function<bool(const std::filesystem::path&)>;
    static constexpr uint64_t max_module_bytes = 128ull << 20;
    explicit NativeCache(std::filesystem::path root) : root_(std::move(root)) {}

    std::optional<std::filesystem::path> find(const std::string& key, const Validator& validate) const {
        namespace fs = std::filesystem;
        if (!sha256_hex(key) || !validate || !safe_path(root_)) return {};
        const auto directory = root_ / key;
        const auto module = directory / "guest.module";
        const auto manifest = directory / "manifest.txt";
        std::error_code error;
        if (!safe_path(directory) || !safe_path(module) || !safe_path(manifest) ||
            !fs::is_regular_file(manifest, error) || error || fs::file_size(manifest, error) > 256 || error ||
            !fs::is_regular_file(module, error) || error) return {};
        std::ifstream input(manifest, std::ios::binary);
        std::string schema, stored_key, digest, size_text, extra;
        if (!std::getline(input, schema) || !std::getline(input, stored_key) ||
            !std::getline(input, digest) || !std::getline(input, size_text) || std::getline(input, extra)) return {};
        if (schema != "WWHD-NATIVE-CACHE-1" || stored_key != key || !sha256_hex(digest) ||
            size_text.empty() || size_text.size() > 12 || size_text.find_first_not_of("0123456789") != std::string::npos)
            return {};
        const auto size = std::stoull(size_text);
        const auto actual_size = fs::file_size(module, error);
        if (error || !size || size > max_module_bytes || actual_size != size) return {};
        if (mods::hash::sha256_file(module) != digest || !validate(module)) return {};
        return module;
    }

    // Serialized first-launch use. Source is produced by the real local linker;
    // this routine does not compile and cannot turn an object/stamp into a game.
    std::filesystem::path publish(const std::string& key, const std::filesystem::path& source,
                                  const Validator& validate) {
        namespace fs = std::filesystem;
        if (!sha256_hex(key) || !validate || !safe_path(root_) || !safe_path(source))
            throw std::runtime_error("Unsafe native cache path or missing module validator");
        if (auto existing = find(key, validate)) return *existing;
        if (!fs::is_regular_file(source)) throw std::runtime_error("Local compilation produced no linked module");
        const auto size = fs::file_size(source);
        if (!size || size > max_module_bytes || !validate(source))
            throw std::runtime_error("Local compiler module failed validation");
        fs::create_directories(root_);
        if (!safe_path(root_)) throw std::runtime_error("Native cache root became a symlink");
        static std::atomic<uint64_t> serial{0};
        const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(serial.fetch_add(1));
        const auto staged = root_ / (key + ".pending-" + nonce);
        const auto target = root_ / key;
        const auto backup = root_ / (key + ".invalid-" + nonce);
        if (!safe_path(target) || !fs::create_directory(staged))
            throw std::runtime_error("Cannot create native cache staging directory");
        bool backed_up = false, committed = false;
        try {
            const auto module = staged / "guest.module";
            copy_file_exclusive(source, module);
            if (fs::file_size(module) != size || !validate(module))
                throw std::runtime_error("Staged native module failed validation");
            const auto digest = mods::hash::sha256_file(module);
            {
                std::ofstream output(staged / "manifest.txt", std::ios::binary);
                output << "WWHD-NATIVE-CACHE-1\n" << key << '\n' << digest << '\n' << size << '\n';
                output.flush();
                if (!output) throw std::runtime_error("Cannot write native cache manifest");
            }
            if (fs::exists(target)) { fs::rename(target, backup); backed_up = true; }
            fs::rename(staged, target);
            committed = true;
            auto result = find(key, validate);
            if (!result) throw std::runtime_error("Published native module failed revalidation");
            return *result;
        } catch (...) {
            std::error_code ignored;
            if (!committed) {
                // Only files this transaction created; never recursively delete
                // a cache tree, a dump, saves or unexpected files.
                fs::remove(staged / "guest.module", ignored);
                fs::remove(staged / "manifest.txt", ignored);
                fs::remove(staged, ignored);
                if (backed_up) fs::rename(backup, target, ignored);
            }
            throw;
        }
    }
private:
    static bool safe_path(const std::filesystem::path& path) {
        if (!path.is_absolute()) return false;
        std::filesystem::path current;
        for (const auto& component : path) {
            if (component == "..") return false;
            current /= component;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(current, error);
            if (error && error != std::errc::no_such_file_or_directory) return false;
            if (!error && std::filesystem::is_symlink(status)) return false;
        }
        return true;
    }
    std::filesystem::path root_;
};
}
