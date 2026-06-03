#include "../RenderManager.h"

extern iTVPRenderManager *TVPCreateNativeVulkanRenderManager();

static class __TVPNativeVulkanRenderManagerAutoRegister {
public:
    __TVPNativeVulkanRenderManagerAutoRegister() {
        TVPRegisterRenderManager("vulkan", TVPCreateNativeVulkanRenderManager);
    }
} __TVPNativeVulkanRenderManagerAutoRegister_instance;
