#include "install_pipeline.h"
#include "installer_progress.h"
#include <switch.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <pthread.h>
#include <string>
#include <unistd.h>

#ifndef WWHD_RESOURCES_SHA256
#error "Pin the bundled resource manifest before building the installer"
#endif

namespace {
std::atomic<bool> cancelled{false}, finished{false}, needs_display{false};
std::mutex status_mutex;
std::string stage = "Verification du jeu";
size_t done = 0, total = 1;
switch2::compiler::InstallResult installation;
FILE* log_file = nullptr;

bool report(const std::string& text, size_t current, size_t count) {
    std::lock_guard<std::mutex> guard(status_mutex);
    const bool changed = stage != text || switch2::compiler::stage_percent(done, total) !=
        switch2::compiler::stage_percent(current, count);
    stage = text;
    done = current;
    total = count;
    needs_display = true;
    if (log_file && changed) {
        std::fprintf(log_file, "%s : %u%% [%zu/%zu]\n", text.c_str(),
            switch2::compiler::stage_percent(current, count), current, count);
        std::fflush(log_file);
    }
    return !cancelled.load();
}
void* compile_worker(void*) {
    switch2::compiler::InstallRequest request;
    request.embedded_resources = "romfs:/";
    request.game = "/switch/wwhd-switch2/game";
    request.cache = "/switch/wwhd-switch2/cache";
    request.resources_sha256 = WWHD_RESOURCES_SHA256;
    request.progress = report;
    installation = switch2::compiler::install_game(request);
    // Managed LLVM pools must join before loader return, not svcExitProcess.
    switch2::compiler::shutdown_compiler();
    if (!installation.success) needs_display = true;
    finished.store(true, std::memory_order_release);
    return nullptr;
}
}

int main() {
    SDL_SetMainReady();
    // Refuse unsupported chainload environments BEFORE a lengthy installation.
    if (!envHasNextLoad()) {
        std::fputs("The homebrew loader must support next-load chainloading.\n", stderr);
        return 1;
    }
    if (R_FAILED(romfsInit())) {
        std::fputs("Cannot mount bundled compiler/runtime resources.\n", stderr);
        return 1;
    }
    // Default libounce startup already mounted sdmc and initialized HID/time.
    if (chdir("sdmc:/") != 0) { romfsExit(); return 1; }
    std::error_code ignored;
    std::filesystem::create_directories("/switch/wwhd-switch2", ignored);
    log_file = std::fopen("/switch/wwhd-switch2/installer.log", "ab");
    if (log_file) std::fputs("\nWWHD Switch 2 installer 0.0.1 - Zoria\n", log_file);

    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    // LLVM and the Python parser cannot run safely on libnx's tiny default
    // pthread stack. This is compiler RAM, not the game's 4 GiB guest heap.
    const int stack_result = pthread_attr_setstacksize(&attributes, 16 << 20);
    pthread_t worker{};
    const int started = stack_result ? stack_result : pthread_create(&worker, &attributes, compile_worker, nullptr);
    pthread_attr_destroy(&attributes);
    if (started) {
        if (log_file) { std::fprintf(log_file, "Cannot start compiler thread: %d\n", started); std::fclose(log_file); }
        romfsExit();
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    bool sdl_ready = false, display_failed = false;
    while (!finished.load(std::memory_order_acquire)) {
        if (!appletMainLoop()) cancelled = true;
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) cancelled = true;
        if (needs_display && !sdl_ready && !display_failed) {
            sdl_ready = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
            if (sdl_ready) {
                window = SDL_CreateWindow("Wind Waker HD - compilation locale", 1280, 720, 0);
                if (window) renderer = SDL_CreateRenderer(window, "opengles2");
            }
            if (!renderer) { display_failed = true; cancelled = true; }
        }
        if (sdl_ready) {
            SDL_Event event;
            while (SDL_PollEvent(&event))
                if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                    event.gbutton.button == SDL_GAMEPAD_BUTTON_START)) cancelled = true;
        }
        if (renderer) {
            std::lock_guard<std::mutex> guard(status_mutex);
            SDL_SetRenderDrawColor(renderer, 8, 18, 30, 255);
            SDL_RenderClear(renderer);
            SDL_SetRenderScale(renderer, 2, 2);
            SDL_SetRenderDrawColor(renderer, 235, 230, 210, 255);
            SDL_RenderDebugText(renderer, 24, 28, "Wind Waker HD / Switch 2 - 0.0.1");
            SDL_RenderDebugText(renderer, 24, 56, "Compilation locale - Zoria");
            SDL_RenderDebugText(renderer, 24, 100, stage.c_str());
            char progress[128];
            const auto percent = switch2::compiler::stage_percent(done, total);
            std::snprintf(progress, sizeof(progress), "Avancement de l'etape : %u%% (%zu / %zu)", percent, done, total);
            SDL_RenderDebugText(renderer, 24, 124, progress);
            const SDL_FRect track{24, 148, 560, 12};
            const SDL_FRect fill{24, 148, 560.0f * percent / 100.0f, 12};
            SDL_SetRenderDrawColor(renderer, 50, 65, 75, 255);
            SDL_RenderFillRect(renderer, &track);
            SDL_SetRenderDrawColor(renderer, 105, 205, 155, 255);
            SDL_RenderFillRect(renderer, &fill);
            SDL_RenderDebugText(renderer, 24, 180, cancelled ? "Annulation a la prochaine etape sure..." :
                "Premier lancement : ne retirez pas la carte SD.");
            SDL_RenderDebugText(renderer, 24, 208, "Les prochains lancements reutiliseront le cache.");
            SDL_RenderDebugText(renderer, 24, 236, "Pourcentage du travail, pas du temps restant.");
            if (total <= 1 && !done) {
                const char spinner[] = "|/-\\";
                char activity[80];
                std::snprintf(activity, sizeof(activity), "%c Operation en cours...", spinner[(SDL_GetTicks() / 250) % 4]);
                SDL_RenderDebugText(renderer, 24, 264, activity);
            }
            SDL_RenderPresent(renderer);
        }
        svcSleepThread(16666667);
    }
    pthread_join(worker, nullptr);
    if (!installation.success && !renderer && !display_failed) {
        sdl_ready = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
        if (sdl_ready) {
            window = SDL_CreateWindow("Wind Waker HD - erreur", 1280, 720, 0);
            if (window) renderer = SDL_CreateRenderer(window, "opengles2");
        }
    }
    if (!installation.success && renderer) {
        if (log_file) { std::fprintf(log_file, "FAILED: %s\n", installation.diagnostics.c_str()); std::fflush(log_file); }
        // A failure is never shown as "installed". Keep the explanation visible
        // until the player explicitly exits; their dump and saves are untouched.
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad)) break;
            SDL_Event event;
            bool exit = false;
            while (SDL_PollEvent(&event))
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_KEY_DOWN ||
                    event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) exit = true;
            if (exit) break;
            SDL_SetRenderDrawColor(renderer, 35, 8, 8, 255);
            SDL_RenderClear(renderer);
            SDL_SetRenderDrawColor(renderer, 255, 230, 210, 255);
            SDL_RenderDebugText(renderer, 20, 24, "Installation interrompue - aucun cache publie");
            SDL_RenderDebugText(renderer, 20, 64, installation.diagnostics.substr(0, 500).c_str());
            SDL_RenderDebugText(renderer, 20, 260, "Details : /switch/wwhd-switch2/installer.log");
            SDL_RenderDebugText(renderer, 20, 290, "Appuyez sur un bouton pour quitter.");
            SDL_RenderPresent(renderer);
            svcSleepThread(16666667);
        }
    }
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    if (sdl_ready) SDL_Quit();
    if (log_file) {
        std::fprintf(log_file, "%s: %s\n", installation.success ? "READY" : "FAILED",
            installation.success ? installation.nro.string().c_str() : installation.diagnostics.c_str());
        std::fclose(log_file);
    }
    romfsExit();
    if (!installation.success || cancelled) return 1;
    const auto next = "sdmc:" + installation.nro.string();
    const auto arguments = "\"" + next + "\"";
    return R_SUCCEEDED(envSetNextLoad(next.c_str(), arguments.c_str())) ? 0 : 1;
}
