#include "install_pipeline.h"
#include "console_compile.h"
#include "console_link.h"
#include "console_python.h"
#include "native_cache.h"
#include "nro_pack.h"
#include "nro_validate.h"
#include "mods/mod_json.h"
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/Parallel.h>
#include <chrono>
#include <fstream>
#include <set>

namespace switch2::compiler {
namespace {
namespace fs = std::filesystem;
using Json = mods::json::Value;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::string read_text(const fs::path& path) {
    require(fs::file_size(path) <= (1 << 20), "Oversized installer manifest: " + path.string());
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "Cannot open installer manifest");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
fs::path relative_file(const std::string& text) {
    require(!text.empty() && text.find_first_of(":\\\r\n") == std::string::npos, "Invalid resource/source path");
    fs::path path(text);
    require(!path.is_absolute(), "Absolute resource/source path is forbidden");
    for (const auto& part : path) require(part != ".." && part != ".", "Resource/source traversal is forbidden");
    return path;
}
void progress(const InstallRequest& request, const std::string& stage, size_t current = 0, size_t total = 1) {
    require(!request.progress || request.progress(stage, current, total), "Installation cancelled; dump and saves preserved");
}
fs::path extract_resources(const InstallRequest& request, const Json& manifest) {
    const auto root = request.cache / "compiler-resources";
    fs::create_directories(root);
    const auto destination = root / request.resources_sha256;
    const bool existing = fs::is_directory(destination);
    const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto staged = root / (request.resources_sha256 + ".pending-" + nonce);
    if (!existing) require(fs::create_directory(staged), "Cannot create resource extraction transaction");
    const auto& files = manifest.get("files");
    require(files.type == Json::Array && !files.array.empty(), "Missing installer resource list");
    std::set<std::string> unique;
    size_t index = 0;
    for (const auto& record : files.array) {
        progress(request, "Preparation des outils", index++, files.array.size());
        const auto relative = relative_file(record.get("path").string());
        require(unique.insert(relative.string()).second, "Duplicate resource manifest path");
        const auto digest = record.get("sha256").string();
        require(sha256_hex(digest), "Invalid resource digest");
        const auto target = (existing ? destination : staged) / relative;
        if (!existing) {
            const auto source = request.embedded_resources / relative;
            require(mods::hash::sha256_file(source) == digest, "Embedded resource hash mismatch: " + relative.string());
            fs::create_directories(target.parent_path());
            copy_file_exclusive(source, target);
        }
        require(fs::is_regular_file(target) && !fs::is_symlink(fs::symlink_status(target)) &&
                mods::hash::sha256_file(target) == digest, "Corrupt extracted resource: " + relative.string());
    }
    if (!existing) fs::rename(staged, destination);
    progress(request, "Preparation des outils", files.array.size(), files.array.size());
    return destination;
}
}

InstallResult install_game(const InstallRequest& request) {
    InstallResult result;
    try {
        // No detached LLVM pool on Horizon; source units are compiled serially
        // and LLD also receives --threads=1. The UI has its own joined worker.
        llvm::parallel::strategy.ThreadsRequested = 1;
        require(request.game.is_absolute() && request.cache.is_absolute() && sha256_hex(request.resources_sha256),
                "Installer requires absolute SD paths and a pinned resource manifest");
        for (const auto* directory : {"code", "content", "meta"})
            require(fs::is_directory(request.game / directory), std::string("Missing game/") + directory);
        require(fs::is_regular_file(request.game / "meta/meta.xml"), "Missing game/meta/meta.xml");
        progress(request, "Verification du dump");
        const auto manifest_text = read_text(request.embedded_resources / "resources.json");
        require(mods::hash::sha256_text(manifest_text) == request.resources_sha256, "Embedded compiler manifest hash mismatch");
        const auto resources = mods::json::parse(manifest_text);
        const auto digest = mods::hash::sha256_file(request.game / "code/cking.rpx");
        bool supported = false;
        for (const auto& build : resources.get("supported_rpx").array) supported |= build.string() == digest;
        require(supported, "Unsupported RPX: use an unmodified USA/EUR version-0 dump");
        progress(request, "Verification du dump", 1, 1);
        const CacheIdentity identity{digest, request.resources_sha256, "static-nro-runtime-v1",
                                     "aarch64-a78c:O3:signed-char:strict-fp:musttail:unwind"};
        const auto key = cache_key(identity);
        NativeCache cache(request.cache / "gamecode");
        const NativeCache::Validator validate = [](const fs::path& path) { return validate_cached_nro(path); };
        progress(request, "Verification du cache");
        if (auto existing = cache.find(key, validate)) {
            progress(request, "Verification du cache", 1, 1);
            result.success = result.reused = true;
            result.nro = *existing;
            return result;
        }
        progress(request, "Verification du cache", 1, 1);
        fs::create_directories(request.cache);
        const auto available = fs::space(request.cache).available;
        require(available >= (2ull << 30), "At least 2 GiB free SD space is required for first compilation");
        const auto extracted = extract_resources(request, resources);
        const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto work = request.cache / (key + ".build-" + nonce);
        require(fs::create_directory(work), "Cannot create game compilation transaction");
        const auto generated = work / "generated", objects = work / "objects";
        fs::create_directory(objects);
        progress(request, "Traduction du RPX PowerPC");
        const auto translated = recompile_rpx(extracted.string(), request.game.string(), generated.string(), request.progress);
        require(translated.success, translated.diagnostics);
        const auto generated_manifest = mods::json::parse(read_text(generated / "compilation.json"));
        require(generated_manifest.get("rpx_sha256").string() == digest, "Recompiler RPX identity mismatch");
        const auto& sources = generated_manifest.get("sources");
        require(sources.type == Json::Array && !sources.array.empty() && sources.array.size() <= 4096,
                "Missing or excessive guest translation units");
        std::vector<std::string> compiled;
        std::set<std::string> unique;
        for (size_t index = 0; index < sources.array.size(); ++index) {
            const auto name = sources.array[index].string();
            const auto relative = relative_file(name);
            require(relative.filename() == relative && relative.extension() == ".c" && unique.insert(name).second,
                    "Invalid generated translation unit");
            progress(request, "Compilation AArch64: " + name, index, sources.array.size());
            Request compilation;
            compilation.source = (generated / relative).string();
            compilation.object = (objects / (name + ".o")).string();
            compilation.resource_directory = (extracted / "clang").string();
            compilation.system_include = (extracted / "libc/include").string();
            compilation.include_directories = {(extracted / "runtime/include").string(), generated.string()};
            const auto object = compile_c_to_object(compilation);
            require(object.success, name + ": " + object.diagnostics);
            compiled.push_back(compilation.object);
            progress(request, "Compilation AArch64: " + name, index + 1, sources.array.size());
        }
        require(mods::hash::sha256_file(request.game / "code/cking.rpx") == digest, "RPX changed during compilation");
        progress(request, "Liaison du jeu et du runtime");
        const auto sdk = extracted / "sdk";
        const auto recipe = mods::json::parse(read_text(sdk / "link-recipe.json"));
        const auto elf = work / "game.elf";
        std::vector<std::string> arguments;
        unsigned guest_slots = 0;
        for (const auto& item : recipe.get("arguments").array) {
            auto value = item.string();
            if (value == "{guest_objects}") {
                ++guest_slots;
                arguments.insert(arguments.end(), compiled.begin(), compiled.end());
                continue;
            }
            for (const auto& [marker, replacement] : std::vector<std::pair<std::string, std::string>>{
                     {"{sdk}", sdk.string()}, {"{output}", elf.string()}}) {
                size_t at = 0;
                while ((at = value.find(marker, at)) != std::string::npos) {
                    value.replace(at, marker.size(), replacement);
                    at += replacement.size();
                }
            }
            require(value.find_first_of("{}") == std::string::npos, "Unexpanded runtime recipe variable");
            arguments.push_back(std::move(value));
        }
        require(guest_slots == 1, "Runtime recipe must have exactly one real-guest input slot");
        const auto linked = link_aarch64_elf(arguments);
        require(linked.success, linked.diagnostics);
        progress(request, "Liaison du jeu et du runtime", 1, 1);
        progress(request, "Creation du NRO prive");
        const auto nro = work / "game.nro";
        std::string error;
        const bool packaged = pack_nro({elf, nro, extracted / "branding/icon.jpg", extracted / "branding/game.nacp"}, error);
        require(packaged, error);
        progress(request, "Creation du NRO prive", 1, 1);
        progress(request, "Validation du NRO prive");
        const bool valid = validate_cached_nro(nro, &error);
        require(valid, error);
        progress(request, "Validation du NRO prive", 1, 1);
        progress(request, "Validation et publication du cache");
        result.nro = cache.publish(key, nro, validate);
        result.success = true;
        if (request.progress) request.progress("Installation terminee", 1, 1);
        // Only this uniquely created transaction, never stale directories,
        // the player's dump, saves or another cache entry.
        std::error_code ignored;
        fs::remove_all(work, ignored);
    } catch (const std::exception& error) {
        result.diagnostics = error.what();
    }
    return result;
}

void shutdown_compiler() { llvm::llvm_shutdown(); }
}
