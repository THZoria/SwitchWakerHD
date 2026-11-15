#include "console_python.h"
#include <Python.h>
#include <memory>

namespace switch2::compiler {
namespace {
struct PyDelete { void operator()(PyObject* value) const { Py_XDECREF(value); } };
using Object = std::unique_ptr<PyObject, PyDelete>;
using Progress = std::function<bool(const std::string&, size_t, size_t)>;
PyObject* report_progress(PyObject* capsule, PyObject* args) {
    auto* progress = static_cast<const Progress*>(PyCapsule_GetPointer(capsule, "wwhd.progress"));
    if (!progress) return nullptr;
    const char* stage = nullptr;
    unsigned long long current = 0, total = 0;
    if (!PyArg_ParseTuple(args, "sKK", &stage, &current, &total)) return nullptr;
    try {
        if (*progress && !(*progress)(stage, size_t(current), size_t(total))) {
            PyErr_SetString(PyExc_RuntimeError, "Installation cancelled; dump and saves preserved");
            return nullptr;
        }
    } catch (const std::exception& error) {
        PyErr_SetString(PyExc_RuntimeError, error.what());
        return nullptr;
    }
    Py_RETURN_NONE;
}
PyMethodDef progress_method = {"report_progress", report_progress, METH_VARARGS, nullptr};
bool append_path(PyConfig& config, const std::string& path) {
    wchar_t* wide = Py_DecodeLocale(path.c_str(), nullptr);
    if (!wide) return false;
    const auto status = PyWideStringList_Append(&config.module_search_paths, wide);
    PyMem_RawFree(wide);
    return !PyStatus_Exception(status);
}
std::string exception_text() {
    // Fetch and format the original traceback, without PyErr_Print's SystemExit
    // process-termination behavior. Failed generation never publishes a cache.
    Object error(PyErr_GetRaisedException());
    if (!error) return "Embedded RPX recompiler failed without a Python exception";
    Object traceback(PyImport_ImportModule("traceback"));
    Object lines(traceback ? PyObject_CallMethod(traceback.get(), "format_exception", "O", error.get()) : nullptr);
    Object separator(PyUnicode_FromString(""));
    Object joined(lines && separator ? PyUnicode_Join(separator.get(), lines.get()) : nullptr);
    if (joined) {
        if (const char* text = PyUnicode_AsUTF8(joined.get())) return text;
    }
    PyErr_Clear();
    Object text(PyObject_Str(error.get()));
    if (text) {
        if (const char* bytes = PyUnicode_AsUTF8(text.get())) return bytes;
    }
    PyErr_Clear();
    return "Cannot format embedded RPX recompiler exception";
}
}

PythonResult recompile_rpx(const std::string& resources, const std::string& game,
                           const std::string& generated, const Progress& progress) {
    PythonResult result;
    if (resources.empty() || game.empty() || generated.empty() || Py_IsInitialized()) {
        result.diagnostics = "Missing recompiler paths or Python is already active";
        return result;
    }
    PyPreConfig preconfig;
    PyPreConfig_InitIsolatedConfig(&preconfig);
    preconfig.utf8_mode = 1;
    const auto preinit = Py_PreInitialize(&preconfig);
    if (PyStatus_Exception(preinit)) {
        result.diagnostics = preinit.err_msg ? preinit.err_msg : "Cannot preinitialize embedded Python";
        return result;
    }
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    config.install_signal_handlers = 0;
    config.site_import = 0;
    config.write_bytecode = 0;
    config.parse_argv = 0;
    config.use_hash_seed = 1;
    config.hash_seed = 0x57574844; // Offline determinism, not a source of entropy.
    config.module_search_paths_set = 1;
    const auto name_status = PyConfig_SetString(&config, &config.program_name, L"wwhd-switch2-compiler");
    bool paths = !PyStatus_Exception(name_status);
    for (const auto& suffix : {"/python/Lib", "/tools", "/tools/recomp", "/tools/switch2/python"})
        paths = append_path(config, resources + suffix) && paths;
    if (!paths) {
        PyConfig_Clear(&config);
        result.diagnostics = "Cannot configure embedded Python paths";
        return result;
    }
    const auto status = Py_InitializeFromConfig(&config);
    if (PyStatus_Exception(status)) {
        result.diagnostics = status.err_msg ? status.err_msg : "Cannot initialize embedded CPython";
        PyConfig_Clear(&config);
        return result;
    }
    PyConfig_Clear(&config);
    {
        Object module(PyImport_ImportModule("console_recompile"));
        Object game_path(PyUnicode_DecodeFSDefault(game.c_str()));
        Object generated_path(PyUnicode_DecodeFSDefault(generated.c_str()));
        Object context(PyCapsule_New(const_cast<Progress*>(&progress), "wwhd.progress", nullptr));
        Object callback(context ? PyCFunction_NewEx(&progress_method, context.get(), nullptr) : nullptr);
        Object output(module && game_path && generated_path && callback ?
            PyObject_CallMethod(module.get(), "run", "OOO", game_path.get(), generated_path.get(), callback.get()) : nullptr);
        result.success = bool(output) && PyDict_Check(output.get());
        if (!result.success) {
            result.diagnostics = exception_text();
            result.diagnostics += "\nRecompiler log (if translation started): " + generated + "/recompiler.log";
        }
    }
    if (Py_FinalizeEx() < 0) {
        result.success = false;
        result.diagnostics += "\nCannot finalize embedded CPython";
    }
    return result;
}
}
