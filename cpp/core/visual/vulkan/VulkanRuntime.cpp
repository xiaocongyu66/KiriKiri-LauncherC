#include "VulkanRuntime.h"

#include <cstdint>
#include <sstream>
#include <vector>

#if defined(__ANDROID__)
#include <vulkan/vulkan.h>
#endif

namespace {

std::string VersionToString(uint32_t version) {
    std::ostringstream out;
    out << ((version >> 22) & 0x3ff) << '.'
        << ((version >> 12) & 0x3ff) << '.'
        << (version & 0xfff);
    return out.str();
}

#if defined(__ANDROID__)
bool HasGraphicsQueue(VkPhysicalDevice device, uint32_t &queueFamilyIndex) {
    uint32_t queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
    if(queueCount == 0)
        return false;

    std::vector<VkQueueFamilyProperties> queues(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, queues.data());
    for(uint32_t i = 0; i < queueCount; ++i) {
        if((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            queueFamilyIndex = i;
            return true;
        }
    }
    return false;
}
#endif

} // namespace

bool TVPProbeNativeVulkan(std::string &summary) {
#if !defined(__ANDROID__)
    summary = "native Vulkan probe is currently enabled only on Android";
    return false;
#else
    uint32_t loaderVersion = VK_API_VERSION_1_0;
    auto enumerateInstanceVersion =
        reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
    if(enumerateInstanceVersion) {
        VkResult result = enumerateInstanceVersion(&loaderVersion);
        if(result != VK_SUCCESS) {
            summary = "vkEnumerateInstanceVersion failed";
            return false;
        }
    }

    if(loaderVersion < VK_API_VERSION_1_1) {
        summary = "Vulkan loader API " + VersionToString(loaderVersion) +
                  " is below required 1.1";
        return false;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "krkr2";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "KiriKiri2";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if(result != VK_SUCCESS) {
        summary = "vkCreateInstance failed with VkResult " +
                  std::to_string(static_cast<int>(result));
        return false;
    }

    uint32_t deviceCount = 0;
    result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if(result != VK_SUCCESS || deviceCount == 0) {
        vkDestroyInstance(instance, nullptr);
        summary = deviceCount == 0 ? "no Vulkan physical devices"
                                   : "vkEnumeratePhysicalDevices failed";
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    result = vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    if(result != VK_SUCCESS) {
        vkDestroyInstance(instance, nullptr);
        summary = "vkEnumeratePhysicalDevices device list failed";
        return false;
    }

    for(VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        if(props.apiVersion < VK_API_VERSION_1_1)
            continue;

        uint32_t graphicsQueue = 0;
        if(!HasGraphicsQueue(device, graphicsQueue))
            continue;

        std::ostringstream out;
        out << props.deviceName << " api=" << VersionToString(props.apiVersion)
            << " queueFamily=" << graphicsQueue;
        summary = out.str();
        vkDestroyInstance(instance, nullptr);
        return true;
    }

    vkDestroyInstance(instance, nullptr);
    summary = "no Vulkan 1.1 physical device with graphics queue";
    return false;
#endif
}
