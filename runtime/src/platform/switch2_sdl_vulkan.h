// SDL owns input/window events; OunceVK owns graphics. SDL must not load Vulkan
// or create an EGL context for this external-graphics-context window.
#pragma once
#include "switch2_vulkan.h"
#include <SDL3/SDL.h>

namespace switch2::sdl_vulkan {
inline SDL_Window* create_window(const char* title, int width, int height) {
    const SDL_PropertiesID properties = SDL_CreateProperties();
    if (!properties) return nullptr;
    const bool configured = SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title) &&
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width) &&
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height) &&
        SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, true) &&
        SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, true);
    SDL_Window* window = configured ? SDL_CreateWindowWithProperties(properties) : nullptr;
    SDL_DestroyProperties(properties);
    return window;
}
inline bool create_surface(SDL_Window* window, VkInstance instance,
                           const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) {
    if (!window) return SDL_SetError("Switch 2 Vulkan: no SDL window");
    const VkResult rc = vulkan::create_surface(instance, allocator, surface);
    if (rc != VK_SUCCESS) return SDL_SetError("Switch 2 Vulkan: surface creation failed (%d)", int(rc));
    return true;
}
}  // namespace switch2::sdl_vulkan
