#include "compiler/native_cache.h"
#include "compiler/installer_progress.h"
#include <cassert>
#include <cstdio>

int main(int argc, char** argv) {
    assert(argc == 2); // isolated test directory provided by the test runner
    namespace fs = std::filesystem;
    const auto root = fs::absolute(argv[1]);
    fs::create_directories(root);
    assert(fs::is_empty(root));
    assert(switch2::compiler::stage_percent(0, 0) == 0);
    assert(switch2::compiler::stage_percent(1, 3) == 33);
    assert(switch2::compiler::stage_percent(3, 3) == 100);
    assert(switch2::compiler::stage_percent(4, 3) == 100);
    // Exercise the exact RomFS/cache copy implementation without copy_file's
    // unavailable host syscall path. Include binary bytes and multiple chunks.
    const auto binary_source = root / "binary-source";
    const auto binary_copy = root / "binary-copy";
    std::string payload(150000, '\0');
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = char(i % 256);
    { std::ofstream output(binary_source, std::ios::binary); output.write(payload.data(), payload.size()); }
    switch2::compiler::copy_file_exclusive(binary_source, binary_copy);
    assert(mods::hash::sha256_file(binary_copy) == mods::hash::sha256_file(binary_source));
    bool overwrite_rejected = false;
    try { switch2::compiler::copy_file_exclusive(binary_source, binary_copy); }
    catch (const std::system_error&) { overwrite_rejected = true; }
    assert(overwrite_rejected && fs::file_size(binary_copy) == payload.size());
    const auto empty_source = root / "empty-source";
    { std::ofstream output(empty_source); }
    switch2::compiler::copy_file_exclusive(empty_source, root / "empty-copy");
    assert(fs::file_size(root / "empty-copy") == 0);
    bool missing_rejected = false;
    try { switch2::compiler::copy_file_exclusive(root / "missing-source", root / "missing-copy"); }
    catch (const std::system_error&) { missing_rejected = true; }
    assert(missing_rejected && !fs::exists(root / "missing-copy"));
    switch2::compiler::CacheIdentity identity{std::string(64, 'a'), "recompiler-test", "abi-test", "flags-test"};
    const auto key = switch2::compiler::cache_key(identity);
    auto other_identity = identity;
    other_identity.runtime_abi += "-changed";
    assert(key != switch2::compiler::cache_key(other_identity));
    switch2::compiler::NativeCache cache(root / "cache");
    const auto source = root / "synthetic-linked.module";
    { std::ofstream output(source); output << "synthetic-module\npayload-v1\n"; }
    auto validate = [](const fs::path& file) {
        std::ifstream input(file);
        std::string signature;
        return bool(std::getline(input, signature)) && signature == "synthetic-module";
    };
    assert(!cache.find(key, validate));
    assert(!cache.find("../escape", validate));
    assert(!cache.find(key, {}));
    const auto installed = cache.publish(key, source, validate);
    assert(cache.find(key, validate) == installed);
    const auto timestamp = fs::last_write_time(installed);
    assert(cache.publish(key, source, validate) == installed);
    assert(fs::last_write_time(installed) == timestamp); // ordinary reuse does not rebuild/overwrite
    { std::ofstream output(installed, std::ios::app); output << "corruption"; }
    assert(!cache.find(key, validate));
    assert(cache.publish(key, source, validate) == installed);
    assert(cache.find(key, validate));
    // A staged/interrupted installation never counts as completed.
    const auto other_key = switch2::compiler::cache_key(other_identity);
    fs::create_directories(root / "cache" / (other_key + ".pending-interrupted"));
    switch2::compiler::copy_file_exclusive(source, root / "cache" / (other_key + ".pending-interrupted") / "guest.module");
    assert(!cache.find(other_key, validate));
    bool rejected = false;
    try { cache.publish(other_key, source, [](const auto&) { return false; }); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected && !cache.find(other_key, validate));
    assert(!cache.find(key, [](const auto&) { return false; }));
    assert(fs::is_regular_file(source));
    std::puts("native compilation cache: identity, reuse, corruption, transaction recovery and mandatory validation passed");
}
