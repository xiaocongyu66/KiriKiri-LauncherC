#include "VulkanRuntime.h"

#if defined(__ANDROID__)
#include <algorithm>
#include <oneapi/tbb/info.h>
#endif

#include <cstdint>
#include <sstream>
#include <vector>

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

bool SelectPhysicalDevice(VkInstance instance, VkPhysicalDevice &physicalDevice,
                          uint32_t &queueFamilyIndex,
                          std::string &summary) {
    uint32_t deviceCount = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if(result != VK_SUCCESS || deviceCount == 0) {
        summary = deviceCount == 0 ? "no Vulkan physical devices"
                                   : "vkEnumeratePhysicalDevices failed";
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    result = vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    if(result != VK_SUCCESS) {
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
        physicalDevice = device;
        queueFamilyIndex = graphicsQueue;
        return true;
    }

    summary = "no Vulkan 1.1 physical device with graphics queue";
    return false;
}

uint32_t DetectWorkerCount() {
    int concurrency = oneapi::tbb::info::default_concurrency();
    if(concurrency <= 0)
        return 1;

    constexpr uint32_t MaxVulkanWorkers = 8;
    return std::max(1u, std::min<uint32_t>(
                            static_cast<uint32_t>(concurrency),
                            MaxVulkanWorkers));
}

bool CreateCommandPool(VkDevice device, uint32_t queueFamilyIndex,
                       VkCommandPoolCreateFlags flags,
                       VkCommandPool &commandPool, std::string &summary,
                       const char *label) {
    VkCommandPoolCreateInfo commandPoolCreateInfo{};
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.queueFamilyIndex = queueFamilyIndex;
    commandPoolCreateInfo.flags = flags;

    VkResult result = vkCreateCommandPool(device, &commandPoolCreateInfo,
                                          nullptr, &commandPool);
    if(result != VK_SUCCESS) {
        summary = std::string(label) + " failed with VkResult " +
                  std::to_string(static_cast<int>(result));
        return false;
    }
    return true;
}

void DestroyCommandPools(VkDevice device, std::vector<VkCommandPool> &pools) {
    for(VkCommandPool pool : pools) {
        if(pool)
            vkDestroyCommandPool(device, pool, nullptr);
    }
    pools.clear();
}
#endif

} // namespace

TVPVulkanRuntime::TVPVulkanRuntime() :
    Initialized(false), WorkerCount(0)
#if defined(__ANDROID__)
    ,
    Instance(VK_NULL_HANDLE), PhysicalDevice(VK_NULL_HANDLE),
    Device(VK_NULL_HANDLE), GraphicsQueue(VK_NULL_HANDLE),
    CommandPool(VK_NULL_HANDLE), GraphicsQueueFamily(0)
#endif
{
}

TVPVulkanRuntime::~TVPVulkanRuntime() { Shutdown(); }

void TVPVulkanRuntime::Shutdown() {
#if defined(__ANDROID__)
    if(Device) {
        vkDeviceWaitIdle(Device);
        DestroyCommandPools(Device, WorkerCommandPools);
        if(CommandPool) {
            vkDestroyCommandPool(Device, CommandPool, nullptr);
            CommandPool = VK_NULL_HANDLE;
        }
        vkDestroyDevice(Device, nullptr);
        Device = VK_NULL_HANDLE;
    }
    GraphicsQueue = VK_NULL_HANDLE;
    PhysicalDevice = VK_NULL_HANDLE;
    if(Instance) {
        vkDestroyInstance(Instance, nullptr);
        Instance = VK_NULL_HANDLE;
    }
#endif
    Initialized = false;
    WorkerCount = 0;
    Summary.clear();
}

bool TVPVulkanRuntime::Initialize(std::string &summary) {
    if(Initialized) {
        summary = Summary;
        return true;
    }

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

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex = 0;
    if(!SelectPhysicalDevice(instance, physicalDevice, queueFamilyIndex,
                             summary)) {
        vkDestroyInstance(instance, nullptr);
        return false;
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};
    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

    VkDevice device = VK_NULL_HANDLE;
    result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device);
    if(result != VK_SUCCESS) {
        vkDestroyInstance(instance, nullptr);
        summary = "vkCreateDevice failed with VkResult " +
                  std::to_string(static_cast<int>(result));
        return false;
    }

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queueFamilyIndex, 0, &graphicsQueue);

    VkCommandPool commandPool = VK_NULL_HANDLE;
    if(!CreateCommandPool(device, queueFamilyIndex,
                          VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                          commandPool, summary,
                          "vkCreateCommandPool(primary)")) {
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return false;
    }

    uint32_t workerCount = DetectWorkerCount();
    std::vector<VkCommandPool> workerCommandPools;
    workerCommandPools.reserve(workerCount);
    for(uint32_t i = 0; i < workerCount; ++i) {
        VkCommandPool workerCommandPool = VK_NULL_HANDLE;
        if(!CreateCommandPool(device, queueFamilyIndex,
                              VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                                  VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                              workerCommandPool,
                              summary, "vkCreateCommandPool(worker)")) {
            DestroyCommandPools(device, workerCommandPools);
            vkDestroyCommandPool(device, commandPool, nullptr);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return false;
        }
        workerCommandPools.push_back(workerCommandPool);
    }

    Instance = instance;
    PhysicalDevice = physicalDevice;
    Device = device;
    GraphicsQueue = graphicsQueue;
    CommandPool = commandPool;
    WorkerCommandPools.swap(workerCommandPools);
    GraphicsQueueFamily = queueFamilyIndex;
    WorkerCount = workerCount;
    Initialized = true;
    Summary = summary + " workerPools=" + std::to_string(WorkerCount);
    summary = Summary;
    return true;
#endif
}

#if defined(__ANDROID__)
VkCommandPool TVPVulkanRuntime::GetWorkerCommandPool(uint32_t index) const {
    if(index >= WorkerCommandPools.size())
        return VK_NULL_HANDLE;
    return WorkerCommandPools[index];
}
#endif

bool TVPProbeNativeVulkan(std::string &summary) {
    TVPVulkanRuntime runtime;
    return runtime.Initialize(summary);
}
