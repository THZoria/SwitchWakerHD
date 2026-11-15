// Switch 2's native Vulkan entry point and NWindow surface. No SDL or Dawn dependency.
#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>

namespace switch2::vulkan {
PFN_vkGetInstanceProcAddr entry_point();
std::array<const char*, 2> instance_extensions();
// Must run before the window is connected to a swapchain. No clock changes.
void configure_window(uint32_t width, uint32_t height);
VkResult create_surface(VkInstance instance, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface);
}  // namespace switch2::vulkan
