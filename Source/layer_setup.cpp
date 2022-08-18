#include "layer.h"

constexpr const char* const_BetterVR_Layer_Name = "VK_LAYER_BetterVR_Layer";
constexpr const char* const_BetterVR_Layer_Description = "Vulkan layer used to breath some VR into BotW for Cemu, using OpenXR!";

// Shared global variables
std::mutex global_lock;
std::map<void*, VkLayerInstanceDispatchTable> instance_dispatch;
std::map<void*, VkLayerDispatchTable> device_dispatch;

VkInstance vkSharedInstance = VK_NULL_HANDLE;
VkPhysicalDevice vkSharedPhysicalDevice = VK_NULL_HANDLE;
VkDevice vkSharedDevice = VK_NULL_HANDLE;

// Create methods
PFN_vkGetInstanceProcAddr saved_GetInstanceProcAddr = nullptr;
PFN_vkGetDeviceProcAddr saved_GetDeviceProcAddr = nullptr;

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
	// Get link info from pNext
	VkLayerInstanceCreateInfo* const chain_info = find_layer_info<VkLayerInstanceCreateInfo>(pCreateInfo->pNext, VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO, VK_LAYER_LINK_INFO);
	if (chain_info == nullptr) {
		return VK_ERROR_INITIALIZATION_FAILED;;
	}

	// Get next function from current chain and then move chain to next layer
	PFN_vkGetInstanceProcAddr next_GetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
	chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;
	
	// Initialize layer-specific stuff
	logInitialize();
	SteamVRHook_initialize();

	// Call vkCreateInstance
	saved_GetInstanceProcAddr = next_GetInstanceProcAddr;
	VkResult result = XR_CreateCompatibleVulkanInstance(SteamVRHook_GetInstanceProcAddr, pCreateInfo, pAllocator, pInstance);
	if (result != VK_SUCCESS)
		return result;

	vkSharedInstance = *pInstance;
	logPrint("Created Vulkan instance successfully!");
	
	VkLayerInstanceDispatchTable dispatchTable = {};
	dispatchTable.GetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)next_GetInstanceProcAddr(*pInstance, "vkGetInstanceProcAddr");
	dispatchTable.DestroyInstance = (PFN_vkDestroyInstance)next_GetInstanceProcAddr(*pInstance, "vkDestroyInstance");
	dispatchTable.EnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)next_GetInstanceProcAddr(*pInstance, "vkEnumeratePhysicalDevices");
	dispatchTable.EnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties)next_GetInstanceProcAddr(*pInstance, "vkEnumerateDeviceExtensionProperties");
	dispatchTable.GetPhysicalDeviceQueueFamilyProperties = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)next_GetInstanceProcAddr(*pInstance, "vkGetPhysicalDeviceQueueFamilyProperties");

	dispatchTable.GetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)next_GetInstanceProcAddr(*pInstance, "vkGetPhysicalDeviceMemoryProperties");
	dispatchTable.GetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2)next_GetInstanceProcAddr(*pInstance, "vkGetPhysicalDeviceProperties2");
	dispatchTable.GetPhysicalDeviceProperties2KHR = (PFN_vkGetPhysicalDeviceProperties2KHR)next_GetInstanceProcAddr(*pInstance, "vkGetPhysicalDeviceProperties2KHR");
	dispatchTable.CreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR)next_GetInstanceProcAddr(*pInstance, "vkCreateWin32SurfaceKHR");
	dispatchTable.GetPhysicalDeviceSurfaceFormatsKHR = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)next_GetInstanceProcAddr(*pInstance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
	dispatchTable.GetPhysicalDeviceImageFormatProperties2 = (PFN_vkGetPhysicalDeviceImageFormatProperties2)next_GetInstanceProcAddr(*pInstance, "vkGetPhysicalDeviceImageFormatProperties2");

	{
		scoped_lock l(global_lock);
		instance_dispatch[GetKey(*pInstance)] = dispatchTable;
	}

	return VK_SUCCESS;
}

VK_LAYER_EXPORT void VKAPI_CALL Layer_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
	//scoped_lock l(global_lock);
	//instance_dispatch.erase(GetKey(instance));
}

bool in_XR_GetPhysicalDevice = false;
VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) {
	if (in_XR_GetPhysicalDevice) {
		VkResult result = VK_SUCCESS;
		{
			scoped_lock l(global_lock);
			result = instance_dispatch[GetKey(instance)].EnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
		}
		return result;
	}
	else {
		uint32_t physicalCount = 0;
		{
			scoped_lock l(global_lock);
			instance_dispatch[GetKey(vkSharedInstance)].EnumeratePhysicalDevices(vkSharedInstance, &physicalCount, nullptr);
		}
		std::vector<VkPhysicalDevice> physicalDevicesLocal;
		physicalDevicesLocal.resize(physicalCount);
		{
			scoped_lock l(global_lock);
			instance_dispatch[GetKey(vkSharedInstance)].EnumeratePhysicalDevices(vkSharedInstance, &physicalCount, physicalDevicesLocal.data());
		}
		// fixme: actually use the device returned and don't pick the first one!
		in_XR_GetPhysicalDevice = true;
		VkPhysicalDevice xrPhysicalDevice = XR_GetPhysicalDevice(vkSharedInstance);
		in_XR_GetPhysicalDevice = false;
		if (pPhysicalDevices == nullptr) {
			*pPhysicalDeviceCount = 1;
			return VK_SUCCESS;
		}
		else {
			*pPhysicalDeviceCount = 1;
			*pPhysicalDevices = physicalDevicesLocal.at(0);
			vkSharedPhysicalDevice = physicalDevicesLocal.at(0);
			return VK_SUCCESS;
		}
	}
}

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateDevice(VkPhysicalDevice gpu, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
	// Get link info from pNext
	VkLayerDeviceCreateInfo* const chain_info = find_layer_info<VkLayerDeviceCreateInfo>(pCreateInfo->pNext, VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO, VK_LAYER_LINK_INFO);
	if (chain_info == nullptr) {
		return VK_ERROR_INITIALIZATION_FAILED;;
	}

	// Get next function from current chain and then move chain to next layer
	PFN_vkGetInstanceProcAddr next_GetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
	PFN_vkGetDeviceProcAddr next_GetDeviceProcAddr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
	chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

	// Call vkCreateDevice
	saved_GetInstanceProcAddr = next_GetInstanceProcAddr;
	saved_GetDeviceProcAddr = next_GetDeviceProcAddr;
	VkResult result = XR_CreateCompatibleVulkanDevice(SteamVRHook_GetInstanceProcAddr, gpu, pCreateInfo, pAllocator, pDevice);
	if (result != VK_SUCCESS)
		return result;

	vkSharedDevice = *pDevice;
	logPrint("Created Vulkan device successfully!");

	VkLayerDispatchTable deviceTable = {};
	deviceTable.GetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)next_GetDeviceProcAddr(*pDevice, "vkGetDeviceProcAddr");
	deviceTable.DestroyDevice = (PFN_vkDestroyDevice)next_GetDeviceProcAddr(*pDevice, "vkDestroyDevice");

	deviceTable.CreateImageView = (PFN_vkCreateImageView)next_GetDeviceProcAddr(*pDevice, "vkCreateImageView");
	deviceTable.UpdateDescriptorSets = (PFN_vkUpdateDescriptorSets)next_GetDeviceProcAddr(*pDevice, "vkUpdateDescriptorSets");
	deviceTable.CmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets)next_GetDeviceProcAddr(*pDevice, "vkCmdBindDescriptorSets");
	deviceTable.CreateRenderPass = (PFN_vkCreateRenderPass)next_GetDeviceProcAddr(*pDevice, "vkCreateRenderPass");
	deviceTable.CmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)next_GetDeviceProcAddr(*pDevice, "vkCmdBeginRenderPass");
	deviceTable.CmdEndRenderPass = (PFN_vkCmdEndRenderPass)next_GetDeviceProcAddr(*pDevice, "vkCmdEndRenderPass");
	deviceTable.QueuePresentKHR = (PFN_vkQueuePresentKHR)next_GetDeviceProcAddr(*pDevice, "vkQueuePresentKHR");

	deviceTable.AllocateMemory = (PFN_vkAllocateMemory)next_GetDeviceProcAddr(*pDevice, "vkAllocateMemory");
	deviceTable.BindImageMemory = (PFN_vkBindImageMemory)next_GetDeviceProcAddr(*pDevice, "vkBindImageMemory");
	deviceTable.GetImageMemoryRequirements = (PFN_vkGetImageMemoryRequirements)next_GetDeviceProcAddr(*pDevice, "vkGetImageMemoryRequirements");
	deviceTable.CreateImage = (PFN_vkCreateImage)next_GetDeviceProcAddr(*pDevice, "vkCreateImage");
	deviceTable.CmdCopyImage = (PFN_vkCmdCopyImage)next_GetDeviceProcAddr(*pDevice, "vkCmdCopyImage");
	deviceTable.CmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)next_GetDeviceProcAddr(*pDevice, "vkCmdPipelineBarrier");

	{
		scoped_lock l(global_lock);
		device_dispatch[GetKey(*pDevice)] = deviceTable;
	}

	return VK_SUCCESS;
}

VK_LAYER_EXPORT void VKAPI_CALL Layer_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
	//scoped_lock l(global_lock);
	//device_dispatch.erase(GetKey(device));
}

// Can't be hooked since this is an explicit layer (prefered since implicit means it has to be installed and is non-portable)
VkResult Layer_EnumerateInstanceVersion(const VkEnumerateInstanceVersionChain* pChain, uint32_t* pApiVersion) {
	XrVersion minVersion = 0;
	XrVersion maxVersion = 0;
	XR_GetSupportedVulkanVersions(&minVersion, &maxVersion);
	*pApiVersion = VK_API_VERSION_1_2;
	return pChain->CallDown(pApiVersion);
}

// GetProcAddr hooks
// todo: implement layerGetPhysicalDeviceProcAddr if necessary
// https://github.dev/crosire/reshade/tree/main/source/vulkan
// https://github.dev/baldurk/renderdoc/tree/v1.x/renderdoc/driver/vulkan
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL Layer_GetInstanceProcAddr(VkInstance instance, const char* pName) {	
	
	PFN_vkVoidFunction voidFunc = nullptr;
	if (instance != nullptr) {
		scoped_lock l(global_lock);
		voidFunc = instance_dispatch[GetKey(instance)].GetInstanceProcAddr(instance, pName);
	}
	logPrint(std::format("[DEBUG] Layer GetInstanceProcAddr {} using {}, result = {}", pName, (void*)instance, (void*)voidFunc));

	HOOK_PROC_FUNC(CreateInstance);
	HOOK_PROC_FUNC(DestroyInstance);
	HOOK_PROC_FUNC(CreateDevice);
	HOOK_PROC_FUNC(DestroyDevice);

	// todo: sort these out later
	//HOOK_PROC_FUNC(EnumerateInstanceVersion);
	HOOK_PROC_FUNC(EnumeratePhysicalDevices);

	// Hook render functions for framebuffer tracking
	// todo: sort out which ones can't be called using a device
	HOOK_PROC_FUNC(CreateImage);
	HOOK_PROC_FUNC(CreateImageView);
	HOOK_PROC_FUNC(UpdateDescriptorSets);
	HOOK_PROC_FUNC(CmdBindDescriptorSets);
	HOOK_PROC_FUNC(CreateRenderPass);
	HOOK_PROC_FUNC(CmdBeginRenderPass);
	HOOK_PROC_FUNC(CmdEndRenderPass);

	// Self-intercept for compatibility
	HOOK_PROC_FUNC(GetInstanceProcAddr);

	{
		scoped_lock l(global_lock);
		return instance_dispatch[GetKey(instance)].GetInstanceProcAddr(instance, pName);
	}
}


VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) {
	logPrint("Allocated memory!");
	scoped_lock l(global_lock);
	return device_dispatch[GetKey(device)].AllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL Layer_GetDeviceProcAddr(VkDevice device, const char* pName) {
	PFN_vkVoidFunction voidFunc = nullptr;
	if (device != nullptr) {
		scoped_lock l(global_lock);
		voidFunc = device_dispatch[GetKey(device)].GetDeviceProcAddr(device, pName);
	}
	if (vkSharedDevice == device && false) {
		HOOK_PROC_FUNC(CreateDevice);
		HOOK_PROC_FUNC(DestroyDevice);

		// Required to self-intercept for compatibility
		HOOK_PROC_FUNC(GetDeviceProcAddr);


		return SteamVRHook_GetDeviceProcAddrExtra(device, pName);
	}
	else {
		logPrint(std::format("[DEBUG] Layer GetDeviceProcAddr {} using {}, result = {}", pName, (void*)device, (void*)voidFunc));

		HOOK_PROC_FUNC(CreateDevice);
		HOOK_PROC_FUNC(DestroyDevice);

		HOOK_PROC_FUNC(AllocateMemory);

		HOOK_PROC_FUNC(CreateImage);
		HOOK_PROC_FUNC(CreateImageView);
		HOOK_PROC_FUNC(UpdateDescriptorSets);
		HOOK_PROC_FUNC(CmdBindDescriptorSets);
		HOOK_PROC_FUNC(CreateRenderPass);
		HOOK_PROC_FUNC(CmdBeginRenderPass);
		HOOK_PROC_FUNC(CmdEndRenderPass);

		HOOK_PROC_FUNC(EnumeratePhysicalDevices);

		// Required to self-intercept for compatibility
		HOOK_PROC_FUNC(GetDeviceProcAddr);

		{
			scoped_lock l(global_lock);
			return device_dispatch[GetKey(device)].GetDeviceProcAddr(device, pName);
		}
	}
}

// Required for loading negotiations
VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {
    if (pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT)
		return VK_ERROR_INITIALIZATION_FAILED;

    if (pVersionStruct->loaderLayerInterfaceVersion >= 2) {
        pVersionStruct->pfnGetInstanceProcAddr = Layer_GetInstanceProcAddr;
        pVersionStruct->pfnGetDeviceProcAddr = Layer_GetDeviceProcAddr;
        pVersionStruct->pfnGetPhysicalDeviceProcAddr = NULL;
    }
	
    static_assert(CURRENT_LOADER_LAYER_INTERFACE_VERSION == 2);

    if (pVersionStruct->loaderLayerInterfaceVersion > 2)
        pVersionStruct->loaderLayerInterfaceVersion = 2;

    return VK_SUCCESS;
}