// Native bridge based on the NWindow/static-Vulkan approach used by Dusklight
// v2.0.3-switch2-1 (hen/platform/hen_vulkan.c and its Aurora fork).
#include "switch2_vulkan.h"
#include "../exception_report.h"
#include <switch.h>
#include <string>

#if !defined(__SWITCH2__) || !defined(VK_USE_PLATFORM_VI_NN)
#error "This bridge requires libounce and VK_USE_PLATFORM_VI_NN"
#endif

// The renderer uses VK_NO_PROTOTYPES and namespace-local function pointers.
// Import just the statically linked OunceVK resolver, not a desktop dlopen loader.
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char*);

namespace switch2::vulkan {
PFN_vkGetInstanceProcAddr entry_point() { return ::vkGetInstanceProcAddr; }
std::array<const char*, 2> instance_extensions() {
    return {VK_KHR_SURFACE_EXTENSION_NAME, VK_NN_VI_SURFACE_EXTENSION_NAME};
}
void configure_window(uint32_t width, uint32_t height) {
    const Result rc = nwindowSetDimensions(nwindowGetDefault(), width, height);
    if (R_FAILED(rc)) exception_report::raise("Switch 2: nwindowSetDimensions failed (" + std::to_string(rc) + ")");
}
VkResult create_surface(VkInstance instance, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) {
    const auto create = reinterpret_cast<PFN_vkCreateViSurfaceNN>(entry_point()(instance, "vkCreateViSurfaceNN"));
    if (!create) return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkViSurfaceCreateInfoNN info{VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN};
    info.window = nwindowGetDefault();
    return create(instance, &info, allocator, surface);
}
}  // namespace switch2::vulkan
