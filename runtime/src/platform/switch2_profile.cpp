// Opt-in scene measurements, emitted through LOG to the existing nxlink sink.
// No listener, clocks, worker thread, automatic capture or persistent setting.
#include "switch2_profile.h"
#include "switch2_profile_window.h"
#include "overlay/extensions.h"
#include "overlay/overlay.h"
#include "render_prof.h"
#include "perf_metrics.h"
#include "interp.h"
#include "gfx/vulkan/backend.h"
#include "gfx/vulkan/shaders.h"
#include "background_shaders.h"
#include "runtime.h"
#include "imgui.h"
#include <string>

namespace switch2::profile {
namespace {
Window window;
bool show_developer_options = false, previous_profiler = false;
bool disable_vertex_reuse = false, disable_index_specialization = false;
bool disable_uniform_plan = false, verify_uniform_plan = false;
const char* scene = "none";
uint64_t sequence = 0;
struct Snapshot {
    uint64_t time = 0, frame = 0, logic = 0, pipelines = 0, pipeline_ns = 0, uploads = 0;
    gfxvk::vk::ShaderStats shaders;
    uint64_t reuse_checks = 0, reuse_hits = 0, reuse_bytes = 0;
    uint64_t vertex_binding_inputs = 0, vertex_binding_commands = 0;
    uint64_t index_conversion_hits = 0, index_upload_bytes_saved = 0;
};
Snapshot before;
Snapshot snapshot() {
    return {rprof::now_ns(), gfxvk::R.frame, interp::logic_steps(), gfxvk::R.pipelineCreates,
        gfxvk::R.pipelineCreateNs, gfxvk::R.uploadBytes, gfxvk::vk::shader_stats(),
        gfxvk::R.vertexReuseChecks, gfxvk::R.vertexReuseHits, gfxvk::R.vertexReuseBytes,
        gfxvk::R.cpuPreparation.vertexBindingInputs, gfxvk::R.cpuPreparation.vertexBindingCommands,
        gfxvk::R.cpuPreparation.indexConversionReuseHits, gfxvk::R.cpuPreparation.indexUploadBytesSaved};
}
void lines(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size()) {
        const size_t end = text.find('\n', begin);
        const size_t length = (end == std::string::npos ? text.size() : end) - begin;
        LOG("[measure sw2] %.*s", int(length), text.data() + begin);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
}
void start(const char* name) {
    if (rprof::env_override() || !window.start(rprof::now_ns())) return;
    scene = name;
    ++sequence;
    previous_profiler = rprof::requested();
    rprof::set_requested(false);  // reset any old profiling window during settling
    LOG("[measure sw2] #%llu %s: settling for 3s, then measuring 15s; keep camera still; opening Settings cancels",
        (unsigned long long)sequence, scene);
    overlay::set_open(false, overlay::OpenReason::API);
}
void menu() {
    ImGui::Checkbox("Show developer options", &show_developer_options);
    if (!show_developer_options) return;
    ImGui::BeginDisabled(active());
    if (ImGui::Checkbox("Disable vertex range reuse (baseline)", &disable_vertex_reuse))
        LOG("[measure sw2] vertex range reuse=%s; frame=%llu", reuse_vertices() ? "on" : "off",
            (unsigned long long)gfxvk::R.frame);
    if (ImGui::Checkbox("Disable specialized indices (baseline)", &disable_index_specialization))
        LOG("[measure sw2] specialized index conversion=%s; frame=%llu", specialize_indices() ? "on" : "off",
            (unsigned long long)gfxvk::R.frame);
    if (ImGui::Checkbox("Disable CPU uniform copy plan (baseline)", &disable_uniform_plan))
        LOG("[measure sw2] CPU uniform copy plan=%s; frame=%llu", uniform_copy_plan() ? "on" : "off",
            (unsigned long long)gfxvk::R.frame);
    if (ImGui::Checkbox("Verify CPU uniform packing (slow)", &verify_uniform_plan))
        LOG("[measure sw2] CPU uniform verification=%s; frame=%llu", verify_uniform_plan ? "on" : "off",
            (unsigned long long)gfxvk::R.frame);
    ImGui::EndDisabled();
    ImGui::TextWrapped("Diagnostics reset at every start. Vertex range reuse, specialized indices and CPU uniform copy plans are "
        "enabled normally; the baseline switches disable them. Change one option at a time. Vertex reuse compares "
        "all source bytes and only reuses immutable uploads in the same submission; no write-watch required. "
        "Check for visual problems as well as timing changes.");
    ImGui::TextWrapped("NetLoader scene measurement: point the camera first, then select a scene. "
        "The menu closes. Stay still until the report appears in nxlink/CMD (3s settling + 15s sample). "
        "Opening Settings cancels. Diagnostics have overhead; compare scenes with the same settings.");
    ImGui::BeginDisabled(active() || rprof::env_override());
    if (ImGui::Button("Measure sea (15s)")) start("sea");
    if (ImGui::Button("Measure village (15s)")) start("village");
    if (ImGui::Button("Measure forest path (15s)")) start("forest-path");
    ImGui::EndDisabled();
    if (rprof::env_override()) ImGui::TextWrapped("Unavailable while WWHD_PROFILE overrides the profiler.");
    ImGui::TextWrapped("Measures CPU phases, memory uploads/checks, shader/pipeline creation and GPU/presentation waits. "
        "GPU timestamps, if supported, describe submission intervals, NOT GPU utilization. "
        "Zero/emulated timestamps cannot establish that the GPU is fast.");
}
}
bool active() { return window.active(); }
bool reuse_vertices() { return !disable_vertex_reuse; }
bool specialize_indices() { return !disable_index_specialization; }
bool uniform_copy_plan() { return !disable_uniform_plan; }
bool verify_uniform_copy_plan() { return verify_uniform_plan; }
void register_menu() { overlay::ext::add_tab({"dev", "Dev", menu}); }
void poll(bool menu_open) {
    switch (window.poll(rprof::now_ns(), menu_open)) {
    case Window::Begin:
        perf::reset();
        before = snapshot();
        rprof::set_requested(true);
        LOG("[measure sw2] #%llu %s BEGIN; mode=%s target=%d; render CPU clock=%s; no clock changes",
            (unsigned long long)sequence, scene, interp::mode_name(), interp::output_fps(),
            rprof::thread_cpu_ns() ? "available" : "UNAVAILABLE (ignore zero CPU times)");
        LOG("[measure sw2] A/B options: vertex reuse=%s; specialized indices=%s (session menu; environment overrides, if any, remain additive)",
            reuse_vertices() ? "on" : "off", specialize_indices() ? "on" : "off");
        LOG("[measure sw2] CPU uniform copy plan=%s; verification=%s (slow when on)",
            uniform_copy_plan() ? "on" : "off", verify_uniform_plan ? "on" : "off");
        break;
    case Window::Finish: {
        const Snapshot after = snapshot();
        const double seconds = double(after.time - before.time) / 1e9;
        const double frames = double(after.frame - before.frame);
        LOG("[measure sw2] #%llu %s END; %.3fs, %.0f output frames, %.2f FPS, %.2f logic steps/s; profiler adds overhead",
            (unsigned long long)sequence, scene, seconds, frames, frames / seconds,
            double(after.logic - before.logic) / seconds);
        const auto& a = after.shaders;
        const auto& b = before.shaders;
        LOG("[measure sw2] CPU uniform plan: %llu packs, %llu copy spans; verification checks=%llu mismatches=%llu",
            (unsigned long long)(a.uniformPlanPacks - b.uniformPlanPacks),
            (unsigned long long)(a.uniformPlanCopies - b.uniformPlanCopies),
            (unsigned long long)(a.uniformPlanChecks - b.uniformPlanChecks),
            (unsigned long long)(a.uniformPlanMismatches - b.uniformPlanMismatches));
        LOG("[measure sw2] CPU command compaction: vertex bindings %.2f -> %.2f API calls/frame; validated index conversion hits %.2f/frame; index uploads avoided %.3f MiB/frame",
            frames ? double(after.vertex_binding_inputs - before.vertex_binding_inputs) / frames : 0.0,
            frames ? double(after.vertex_binding_commands - before.vertex_binding_commands) / frames : 0.0,
            frames ? double(after.index_conversion_hits - before.index_conversion_hits) / frames : 0.0,
            frames ? double(after.index_upload_bytes_saved - before.index_upload_bytes_saved) / 1048576.0 / frames : 0.0);
        LOG("[measure sw2] new shader variants=%llu; SPIR-V compiles=%llu (%.3fms wall); disk hits=%llu; pipelines created=%llu (%.3fms wall)",
            (unsigned long long)(a.compiles - b.compiles), (unsigned long long)(a.spirvCompiles - b.spirvCompiles),
            double(a.spirvCompileNs - b.spirvCompileNs) / 1e6, (unsigned long long)(a.diskHits - b.diskHits),
            (unsigned long long)(after.pipelines - before.pipelines), double(after.pipeline_ns - before.pipeline_ns) / 1e6);
        LOG("[measure sw2] upload arena %.3f MiB/output-frame (not total memory traffic); GPU intervals/waits are NOT utilization",
            frames ? double(after.uploads - before.uploads) / 1048576.0 / frames : 0.0);
        LOG("[measure sw2] vertex reuse: %.2f checks/frame, %.2f hits/frame, %.3f MiB/frame avoided uploads",
            frames ? double(after.reuse_checks - before.reuse_checks) / frames : 0.0,
            frames ? double(after.reuse_hits - before.reuse_hits) / frames : 0.0,
            frames ? double(after.reuse_bytes - before.reuse_bytes) / 1048576.0 / frames : 0.0);
        lines(perf::summary());  // CPU game/render and optional GPU submission intervals
        lines(bgshaders::report_line());
        // Profiler sections are the latest complete 120-swap window, NOT an
        // aggregate of the entire capture; periodic windows also reach nxlink.
        lines("Latest complete 120-swap profiler window follows (not whole-capture totals):");
        lines(rprof::report_section());
        LOG("[measure sw2] #%llu %s COMPLETE; restoring profiler to %s", (unsigned long long)sequence,
            scene, previous_profiler ? "on" : "off");
        rprof::set_requested(previous_profiler);
        break;
    }
    case Window::Cancel:
        LOG("[measure sw2] #%llu %s CANCELLED; menu reopened or clock invalid; no sample reported",
            (unsigned long long)sequence, scene);
        rprof::set_requested(previous_profiler);
        break;
    default: break;
    }
}
}
