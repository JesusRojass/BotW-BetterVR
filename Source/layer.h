#pragma once
#include "pch.h"

// single global lock, for simplicity
extern std::mutex global_lock;
typedef std::lock_guard<std::mutex> scoped_lock;

// use the loader's dispatch table pointer as a key for dispatch map lookups
template<typename DispatchableType>
void* GetKey(DispatchableType inst) {
	return *(void**)inst;
}

static std::vector<const char*> modifyExtensions(uint32_t* extCount, const char** origExt, std::vector<std::string>* mutateableNewExts) {
	for (uint32_t i = 0; i < *extCount; i++) {
		if (std::find(mutateableNewExts->begin(), mutateableNewExts->end(), origExt[i]) == mutateableNewExts->end()) {
			mutateableNewExts->emplace_back(origExt[i]);
		}
	}

	std::vector<const char*> cstrArray(mutateableNewExts->size());
	*extCount = (uint32_t)mutateableNewExts->size();
	for (std::string& extensionStr : *mutateableNewExts) {
		cstrArray.push_back(extensionStr.data());
	}

	origExt = cstrArray.data();
}

// layer book-keeping information, to store dispatch tables by key
extern std::map<void*, VkLayerInstanceDispatchTable> instance_dispatch;
extern std::map<void*, VkLayerDispatchTable> device_dispatch;

// Shared variables
// todo: Remove this shortcut, should be reworked into classes
extern VkInstance vkSharedInstance;
extern VkPhysicalDevice vkSharedPhysicalDevice;
extern VkDevice vkSharedDevice;

extern XrInstance xrSharedInstance;
extern XrSystemId xrSharedSystemId;
extern XrSession xrSharedSession;

extern std::list<VkInstance> steamInstances;
extern VkInstance topVkInstance;
extern std::list<VkDevice> steamDevices;
extern VkDevice topVkDevice;

// hook functions
//VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass);
//VK_LAYER_EXPORT void VKAPI_CALL Layer_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents);
//VK_LAYER_EXPORT void VKAPI_CALL Layer_CmdEndRenderPass(VkCommandBuffer commandBuffer);
//VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
//VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence);

// internal functions of all the subsystems
bool cemuInitialize();

void logInitialize();
void logShutdown();
void logPrint(const char* message);
void logPrint(const std::string_view& message_view);
void logTimeElapsed(char* prefixMessage, LARGE_INTEGER time);
void checkXRResult(XrResult result, const char* errorMessage = nullptr);
void checkVkResult(VkResult result, const char* errorMessage = nullptr);

// xr functions
void XR_GetSupportedVulkanVersions(XrVersion* minVulkanVersion, XrVersion* maxVulkanVersion);
VkResult XR_CreateCompatibleVulkanInstance(PFN_vkGetInstanceProcAddr getInstanceProcAddr, const VkInstanceCreateInfo* vulkanCreateInfo, const VkAllocationCallbacks* vulkanAllocator, VkInstance* vkInstancePtr);
VkPhysicalDevice XR_GetPhysicalDevice(VkInstance vkInstance);
VkResult XR_CreateCompatibleVulkanDevice(PFN_vkGetInstanceProcAddr getInstanceProcAddr, VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* createInfo, const VkAllocationCallbacks* allocator, VkDevice* vkDevicePtr);
std::array<XrViewConfigurationView, 2> XR_CreateViewConfiguration();
XrSession XR_CreateSession(VkInstance vkInstance, VkDevice vkDevice, VkPhysicalDevice vkPhysicalDevice);

// rendering functions
void RND_InitRendering();

// SteamVR hooks
void SteamVRHook_initialize();
PFN_vkVoidFunction VKAPI_CALL SteamVRHook_GetInstanceProcAddr(VkInstance instance, const char* pName);
PFN_vkVoidFunction VKAPI_CALL SteamVRHook_GetDeviceProcAddr(VkDevice device, const char* pName);
PFN_vkVoidFunction VKAPI_CALL SteamVRHook_GetDeviceProcAddrExtra(VkDevice device, const char* pName);

// framebuffer functions
VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage);
VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView);
VK_LAYER_EXPORT void VKAPI_CALL Layer_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies);
VK_LAYER_EXPORT void VKAPI_CALL Layer_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets);
VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass);
VK_LAYER_EXPORT void VKAPI_CALL Layer_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents);
VK_LAYER_EXPORT void VKAPI_CALL Layer_CmdEndRenderPass(VkCommandBuffer commandBuffer);

static bool initializeLayer() {
	if (!cemuInitialize()) {
		// Vulkan layer is hooking something that's not Cemu
		return false;
	}
	logInitialize();
	return true;
}

static void shutdownLayer() {
	logShutdown();
}