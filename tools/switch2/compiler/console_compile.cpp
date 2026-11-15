#include "console_compile.h"
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Driver/Compilation.h>
#include <clang/Driver/Driver.h>
#include <clang/Driver/Job.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/CodeGen/CodeGenAction.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <cstring>
#include <memory>

namespace switch2::compiler {
Result compile_c_to_object(const Request& request) {
    Result result;
    if (request.source.empty() || request.object.empty() || request.resource_directory.empty() ||
        request.system_include.empty() || request.source == request.object) {
        result.diagnostics = "Compiler requires distinct source/object paths, resource headers and target libc headers.\n";
        return result;
    }
    static const bool targets_initialized = [] {
        llvm::InitializeAllTargetInfos();
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmPrinters();
        return true;
    }();
    (void)targets_initialized;
    llvm::raw_string_ostream output(result.diagnostics);
    llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> options(new clang::DiagnosticOptions());
    clang::TextDiagnosticPrinter printer(output, options.get());
    clang::CompilerInstance instance;
    instance.createDiagnostics(&printer, false);
    if (!instance.hasDiagnostics()) {
        output << "Cannot initialize compiler diagnostics.\n";
        return result;
    }
    auto& diagnostics = instance.getDiagnostics();
    clang::driver::Driver driver("/switch/wwhd-switch2/compiler/clang", "aarch64-none-elf", diagnostics);
    // Let Clang's actual driver derive cc1 options. Keeping the normal guest
    // flags preserves mandatory tails, signed char, FP behavior and unwinding.
    std::vector<std::string> storage{
        "clang", "--target=aarch64-none-elf", "-mcpu=cortex-a78c+crypto", "-x", "c",
        "-std=gnu11", "-O3", "-fPIC", "-ffunction-sections", "-fdata-sections", "-fsigned-char",
        "-ffp-contract=off", "-fno-strict-aliasing", "-fwrapv", "-fomit-frame-pointer",
        "-fexceptions", "-funwind-tables", "-D__SWITCH__", "-D__SWITCH2__",
        "-Werror=unknown-attributes", "-ferror-limit=8", "-resource-dir", request.resource_directory,
        "-nostdinc", "-isystem", request.resource_directory + "/include",
        "-isystem", request.system_include, "-c", request.source, "-o", request.object};
    for (const auto& directory : request.include_directories) {
        storage.emplace_back("-I");
        storage.push_back(directory);
    }
    std::vector<const char*> arguments;
    for (const auto& value : storage) arguments.push_back(value.c_str());
    std::unique_ptr<clang::driver::Compilation> compilation(driver.BuildCompilation(arguments));
    if (!compilation || diagnostics.hasErrorOccurred()) return result;
    if (compilation->getJobs().size() != 1) {
        output << "Expected exactly one integrated compiler job; external compiler/linker execution is forbidden.\n";
        return result;
    }
    const auto& job = *compilation->getJobs().begin();
    const auto& frontend_arguments = job.getArguments();
    if (frontend_arguments.empty() || std::strcmp(frontend_arguments.front(), "-cc1")) {
        output << "Clang did not produce an integrated cc1 compilation.\n";
        return result;
    }
    if (!clang::CompilerInvocation::CreateFromArgs(instance.getInvocation(),
            llvm::ArrayRef<const char*>(frontend_arguments).drop_front(), diagnostics))
        return result;
    clang::EmitObjAction action;
    result.success = instance.ExecuteAction(action) && !diagnostics.hasErrorOccurred();
    return result;
}
}
