#pragma once

#include <cstdint>
#include <string>

#if defined(__ANDROID__)
#include <vector>
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
    uint32_t GetWorkerCount() const { return WorkerCount; }

#if defined(__ANDROID__)
    VkCommandPool GetPrimaryCommandPool() const { return CommandPool; }
    VkCommandPool GetWorkerCommandPool(uint32_t index) const;
#endif

private:
    bool Initialized;
    std::string Summary;
    uint32_t WorkerCount;

#if defined(__ANDROID__)
    VkInstance Instance;
    VkPhysicalDevice PhysicalDevice;
    VkDevice Device;
    VkQueue GraphicsQueue;
    VkCommandPool CommandPool;
    std::vector<VkCommandPool> WorkerCommandPools;
    uint32_t GraphicsQueueFamily;
#endif
};

bool TVPProbeNativeVulkan(std::string &summary);
