#pragma once

#include <cstdint>
#include <string>

#if defined(__ANDROID__)
#include <vulkan/vulkan.h>
#endif

class TVPVulkanRuntime {
public:
    TVPVulkanRuntime();
    ~TVPVulkanRuntime();

    TVPVulkanRuntime(const TVPVulkanRuntime &) = delete;
    TVPVulkanRuntime &operator=(const TVPVulkanRuntime &) = delete;

    bool Initialize(std::string &summary);
    void Shutdown();

    bool IsInitialized() const { return Initialized; }
    const std::string &GetSummary() const { return Summary; }

private:
    bool Initialized;
    std::string Summary;

#if defined(__ANDROID__)
    VkInstance Instance;
    VkPhysicalDevice PhysicalDevice;
    VkDevice Device;
    VkQueue GraphicsQueue;
    VkCommandPool CommandPool;
    uint32_t GraphicsQueueFamily;
#endif
};

bool TVPProbeNativeVulkan(std::string &summary);
