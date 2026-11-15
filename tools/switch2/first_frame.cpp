// Game-free bring-up: exercise WWHD's real Vulkan function loader on OunceVK.
// Intentionally serialized, not a performance benchmark. Plus returns to hbmenu.
#include "gfx/vulkan/loader.h"
#include "platform/switch2_vulkan.h"
#include "probe_runtime.h"
#include <switch.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
using namespace gfxvk;
constexpr uint64_t kWaitNs = 5'000'000'000ull;
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": VkResult " + std::to_string(result));
}
template<class T> T instance_function(VkInstance instance, const char* name) {
    auto fn = reinterpret_cast<T>(switch2::vulkan::entry_point()(instance, name));
    if (!fn) throw std::runtime_error(std::string("Missing ") + name);
    return fn;
}
struct Probe {
    VkInstance instance{};
    PFN_vkDestroyInstance destroyInstance{};
    VkPhysicalDevice physical{};
    VkSurfaceKHR surface{};
    VkDevice device{};
    PFN_vkDestroyDevice destroyDevice{};
    VkQueue queue{};
    uint32_t family{};
    VkSwapchainKHR swapchain{};
    std::vector<VkImage> images;
    VkCommandPool pool{};
    VkCommandBuffer cmd{};
    VkSemaphore acquired{}, rendered{};
    VkFence done{};
    bool presented = false;
    ~Probe() { cleanup(); }

    // Also safe after partial initialization. Destroy everything before main
    // returns: kaiHEN may run hbmenu in this same process (Dusklight's exit fix).
    void cleanup() noexcept {
        if (device && destroyDevice) {
            wwhd_probe_log("[probe] cleanup: waiting for device, then destroying frame resources\n");
            if (vkDeviceWaitIdle) vkDeviceWaitIdle(device);
            if (done) vkDestroyFence(device, done, nullptr);
            if (rendered) vkDestroySemaphore(device, rendered, nullptr);
            if (acquired) vkDestroySemaphore(device, acquired, nullptr);
            if (pool) vkDestroyCommandPool(device, pool, nullptr);
            if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
            destroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }
        if (surface && vkDestroySurfaceKHR) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (instance && destroyInstance) destroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
        surface = VK_NULL_HANDLE;
    }

    void init() {
        wwhd_probe_log("[probe] loading global Vulkan functions\n");
        load_global_functions(switch2::vulkan::entry_point());
        uint32_t version = VK_API_VERSION_1_0;
        if (vkEnumerateInstanceVersion) check(vkEnumerateInstanceVersion(&version), "instance version");
        wwhd_probe_log("[probe] Vulkan loader %u.%u.%u\n", VK_API_VERSION_MAJOR(version),
                    VK_API_VERSION_MINOR(version), VK_API_VERSION_PATCH(version));
        if (version < VK_API_VERSION_1_1) throw std::runtime_error("Vulkan 1.1 minimum required");
        auto extensions = switch2::vulkan::instance_extensions();
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "WWHD Switch 2 probe";
        app.apiVersion = std::min(version, uint32_t(VK_API_VERSION_1_3));
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = uint32_t(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        wwhd_probe_log("[probe] calling vkCreateInstance\n");
        check(vkCreateInstance(&info, nullptr, &instance), "create instance");
        wwhd_probe_log("[probe] instance created; loading instance functions\n");
        destroyInstance = instance_function<PFN_vkDestroyInstance>(instance, "vkDestroyInstance");
        load_instance_functions(instance);
        wwhd_probe_log("[probe] configuring NWindow at 1920x1080\n");
        switch2::vulkan::configure_window(1920, 1080); // conservative in both modes; no forced 4K
        wwhd_probe_log("[probe] calling vkCreateViSurfaceNN\n");
        check(switch2::vulkan::create_surface(instance, nullptr, &surface), "create NWindow surface");

        uint32_t count = 0;
        wwhd_probe_log("[probe] enumerating physical devices\n");
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "count GPUs");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "enumerate GPUs");
        for (auto candidate : devices) {
            uint32_t families = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
            std::vector<VkQueueFamilyProperties> props(families);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, props.data());
            for (uint32_t i = 0; i < families; ++i) {
                VkBool32 present = VK_FALSE;
                check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present), "present support");
                if (present && props[i].queueCount && (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    physical = candidate;
                    family = i;
                    break;
                }
            }
            if (physical) break;
        }
        if (!physical) throw std::runtime_error("No graphics queue with NWindow presentation");
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physical, &props);
        wwhd_probe_log("[probe] GPU %s, Vulkan %u.%u.%u, queue %u\n", props.deviceName,
                    VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
                    VK_API_VERSION_PATCH(props.apiVersion), family);
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = deviceExtensions;
        wwhd_probe_log("[probe] calling vkCreateDevice\n");
        check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "create device");
        wwhd_probe_log("[probe] device created; loading WWHD device functions\n");
        destroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(vkGetDeviceProcAddr(device, "vkDestroyDevice"));
        if (!destroyDevice) throw std::runtime_error("Missing vkDestroyDevice");
        // Clear-only probe uses render-pass-compatible entry points. This does
        // not establish that WWHD's shader/feature requirements are satisfied.
        load_device_functions(device, false, true);
        vkGetDeviceQueue(device, family, 0, &queue);
        create_swapchain();

        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = family;
        check(vkCreateCommandPool(device, &poolInfo, nullptr, &pool), "create command pool");
        VkCommandBufferAllocateInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmdInfo.commandPool = pool;
        cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdInfo.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &cmdInfo, &cmd), "allocate command buffer");
        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device, &semInfo, nullptr, &acquired), "create acquire semaphore");
        check(vkCreateSemaphore(device, &semInfo, nullptr, &rendered), "create render semaphore");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device, &fenceInfo, nullptr, &done), "create fence");
        wwhd_probe_log("[probe] WWHD Vulkan entry points loaded; Plus exits. Not the game.\n");
    }

    void create_swapchain() {
        wwhd_probe_log("[probe] querying surface capabilities and formats\n");
        VkSurfaceCapabilitiesKHR caps{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps), "surface capabilities");
        if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
            throw std::runtime_error("Surface does not support transfer clears");
        uint32_t count = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr), "count formats");
        if (!count) throw std::runtime_error("No surface formats");
        std::vector<VkSurfaceFormatKHR> formats(count);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data()), "surface formats");
        auto format = formats.front();
        if (format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_B8G8R8A8_UNORM;
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = surface;
        info.minImageCount = std::max(2u, caps.minImageCount);
        if (caps.maxImageCount) info.minImageCount = std::min(info.minImageCount, caps.maxImageCount);
        info.imageFormat = format.format;
        info.imageColorSpace = format.colorSpace;
        info.imageExtent = caps.currentExtent;
        if (info.imageExtent.width == UINT32_MAX) {
            info.imageExtent = {std::clamp(1920u, caps.minImageExtent.width, caps.maxImageExtent.width),
                                std::clamp(1080u, caps.minImageExtent.height, caps.maxImageExtent.height)};
        }
        if (!info.imageExtent.width || !info.imageExtent.height) throw std::runtime_error("Surface has zero extent");
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        for (auto alpha : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                           VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
            if (caps.supportedCompositeAlpha & alpha) { info.compositeAlpha = alpha; break; }
        }
        if (!info.compositeAlpha) throw std::runtime_error("No supported composite alpha");
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        wwhd_probe_log("[probe] calling vkCreateSwapchainKHR\n");
        check(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain), "create swapchain");
        check(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr), "count swapchain images");
        images.resize(count);
        check(vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()), "swapchain images");
        wwhd_probe_log("[probe] Swapchain %ux%u, %u images\n", info.imageExtent.width, info.imageExtent.height, count);
    }

    bool draw(uint32_t frame) {
        check(vkWaitForFences(device, 1, &done, VK_TRUE, kWaitNs), "wait frame fence");
        uint32_t index = 0;
        auto result = vkAcquireNextImageKHR(device, swapchain, 1'000'000'000ull, acquired, VK_NULL_HANDLE, &index);
        if (result == VK_TIMEOUT || result == VK_NOT_READY) return true;
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            wwhd_probe_log("[probe] Surface changed; relaunch for the new mode.\n");
            return false;
        }
        const bool suboptimal = result == VK_SUBOPTIMAL_KHR;
        if (!suboptimal) check(result, "acquire image");
        check(vkResetCommandPool(device, pool, 0), "reset command pool");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(cmd, &begin), "begin commands");
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // discard previous frame
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = images.at(index);
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        const float t = float(frame) / 90.0f;
        const VkClearColorValue color{{0.05f, 0.3f + 0.2f * std::sin(t), 0.6f + 0.2f * std::cos(t), 1.0f}};
        vkCmdClearColorImage(cmd, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color,
                            1, &barrier.subresourceRange);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        check(vkEndCommandBuffer(cmd), "end commands");
        const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &acquired;
        submit.pWaitDstStageMask = &stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &rendered;
        check(vkResetFences(device, 1, &done), "reset fence");
        check(vkQueueSubmit(queue, 1, &submit, done), "submit clear");
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &rendered;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &index;
        result = vkQueuePresentKHR(queue, &present);
        // Diagnostic only: ensure presentation consumed its semaphore before reuse.
        check(vkQueueWaitIdle(queue), "wait present queue");
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR)
            check(result, "present clear");
        if (!presented && (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)) {
            wwhd_probe_log("[probe] First frame submitted and presented.\n");
            presented = true;
        }
        return !suboptimal && result == VK_SUCCESS;
    }
};
}  // namespace

int main() {
    wwhd_probe_log("[probe] Vulkan net-v5: main reached (no game data, no clock changes)\n");
    if (!wwhd_probe_ready()) {
        wwhd_probe_log("[probe] FAIL: platform initialization failed; see preceding boot result\n");
        return 1;
    }
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad{};
    padInitializeDefault(&pad);
    try {
        Probe probe;
        probe.init();
        wwhd_probe_log("[probe] entering frame loop\n");
        const auto mode = appletGetOperationMode();
        for (uint32_t frame = 0; appletMainLoop(); ++frame) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
            if (mode != appletGetOperationMode()) {
                wwhd_probe_log("[probe] Dock mode changed; exiting. Relaunch in the new mode.\n");
                break;
            }
            if (!probe.draw(frame)) break;
        }
        probe.cleanup();
        wwhd_probe_log("[probe] Vulkan destroyed; returning to hbmenu.\n");
        return 0;
    } catch (const std::exception& error) {
        wwhd_probe_log("[probe] FAIL: %s\n", error.what());
        return 1;
    }
}
