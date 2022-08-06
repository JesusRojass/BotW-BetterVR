#include "layer.h"

constexpr const char* const_BetterVR_Layer_Name = "VK_LAYER_BetterVR_Layer";
constexpr const char* const_BetterVR_Layer_Description = "Vulkan layer used to breath some VR into BotW for Cemu, using OpenXR!";

std::mutex global_lock;

std::map<void*, VkLayerInstanceDispatchTable> instance_dispatch;
std::map<void*, VkLayerDispatchTable> device_dispatch;

HMODULE vulkanModule = NULL;

VkInstance sharedInstance = VK_NULL_HANDLE;
PFN_vkGetInstanceProcAddr saved_GetInstanceProcAddr = nullptr;
PFN_vkCreateInstance next_CreateInstance = nullptr;
bool useHookedFuncs = true;

// Setup dispatch table

VkInstance steamVrInstance = VK_NULL_HANDLE;
PFN_vkGetInstanceProcAddr top_origGetInstanceProcAddr = nullptr;
PFN_vkGetDeviceProcAddr top_origGetDeviceProcAddr = nullptr;
PFN_vkCreateInstance top_origCreateInstance = nullptr;
PFN_vkCreateDevice top_origCreateDevice = nullptr;

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_NESTED_TOP_CreateDevice(VkPhysicalDevice gpu, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
	VkResult result = top_origCreateDevice(gpu, pCreateInfo, pAllocator, pDevice);
	logPrint(std::format("Created new NESTED device: {}", (void*)*pDevice));
	return result;
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL Layer_NESTED_TOP_GetDeviceProcAddr(VkDevice device, const char* pName) {
	PFN_vkGetDeviceProcAddr top_DeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(reinterpret_cast<HMODULE>(vulkanModule), "vkGetDeviceProcAddr"));
	
	if (strcmp(pName, "vkCreateDevice") == 0) {
		top_origCreateDevice = (PFN_vkCreateDevice)top_DeviceProcAddr(device, pName);
		return (PFN_vkVoidFunction)Layer_NESTED_TOP_CreateDevice;
	}

	// Required to self-intercept for compatibility
	if (strcmp(pName, "vkGetDeviceProcAddr") == 0)
		return (PFN_vkVoidFunction)Layer_NESTED_TOP_GetDeviceProcAddr;
	
	PFN_vkVoidFunction funcRet = nullptr; // saved_GetInstanceProcAddr(device, pName);
	if (funcRet == nullptr) {
		funcRet = top_DeviceProcAddr(device, pName);
		logPrint(std::format("Couldn't resolve using GetDeviceProcAddr, used top-level hook: {} {} {}", pName, (void*)device, (void*)funcRet));
	}
	else {
		logPrint(std::format("Could resolve using GetDeviceProcAddr: {} {} {}", pName, (void*)device, (void*)funcRet));
	}
	return funcRet;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_NESTED_TOP_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
	VkResult result = top_origCreateInstance(pCreateInfo, pAllocator, pInstance);
	logPrint(std::format("Created new NESTED instance: {}", (void*)*pInstance));
	return result;
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL Layer_NESTED_GetInstanceProcAddr(VkInstance instance, const char* pName) {	
	PFN_vkGetInstanceProcAddr top_InstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(reinterpret_cast<HMODULE>(vulkanModule), "vkGetInstanceProcAddr"));
	
	if (strcmp(pName, "vkCreateInstance") == 0) {
		top_origCreateInstance = (PFN_vkCreateInstance)top_InstanceProcAddr(instance, pName);
		return (PFN_vkVoidFunction)Layer_NESTED_TOP_CreateInstance;
	}

	// Required to self-intercept for compatibility
	if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
		top_origGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)top_InstanceProcAddr(instance, pName);
		return (PFN_vkVoidFunction)Layer_NESTED_TOP_GetDeviceProcAddr;
	}
	if (strcmp(pName, "vkGetInstanceProcAddr") == 0)
		return (PFN_vkVoidFunction)Layer_NESTED_GetInstanceProcAddr;

	PFN_vkVoidFunction funcRet = saved_GetInstanceProcAddr(instance, pName);
	if (funcRet == nullptr) {
		funcRet = top_InstanceProcAddr(instance, pName);
		logPrint(std::format("Couldn't resolve using GetInstanceProcAddr, used top-level hook: {} {} {}", pName, (void*)instance, (void*)funcRet));
	}
	else {
		logPrint(std::format("Could resolve using GetInstanceProcAddr: {} {} {}", pName, (void*)instance, (void*)funcRet));
	}
	return funcRet;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
	logInitialize();

	// Get link info from pNext
	VkLayerInstanceCreateInfo* const chain_info = find_layer_info<VkLayerInstanceCreateInfo>(pCreateInfo->pNext, VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO, VK_LAYER_LINK_INFO);
	if (chain_info == nullptr) {
		return VK_ERROR_INITIALIZATION_FAILED;;
	}
	
	SetEnvironmentVariableA("VK_INSTANCE_LAYERS", NULL);

	vulkanModule = LoadLibraryA("vulkan-1.dll");

	// Get next function from current chain and then move chain to next layer
	PFN_vkGetInstanceProcAddr next_GetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
	chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

	// Call vkCreateInstance
	//next_CreateInstance = (PFN_vkCreateInstance)next_GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
	//VkResult result = next_CreateInstance(pCreateInfo, pAllocator, pInstance);
	saved_GetInstanceProcAddr = next_GetInstanceProcAddr;
	VkResult result = XR_CreateCompatibleVulkanInstance(Layer_NESTED_GetInstanceProcAddr, pCreateInfo, pAllocator, pInstance);
	if (result != VK_SUCCESS)
		return result;

	sharedInstance = *pInstance;

	logPrint("Created Vulkan instance successfully!");

	//SetEnvironmentVariableA("VK_INSTANCE_LAYERS", "VK_LAYER_BetterVR_Layer");
	
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
}


std::atomic_bool in_XR_GetPhysicalDevice = false;
VkPhysicalDevice xrPhysicalDevice = VK_NULL_HANDLE;
VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) {
	logPrint(std::format("Enumerating physical devices {}...", *pPhysicalDeviceCount));
	VkResult result = VK_SUCCESS;
	{
		scoped_lock l(global_lock);
		result = instance_dispatch[GetKey(instance)].EnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
	}
	logPrint(std::format("Enumerated devices {}...", *pPhysicalDeviceCount));
	return result;
	//if (in_XR_GetPhysicalDevice) {
	//	VkResult result = VK_SUCCESS;
	//	{
	//		scoped_lock l(global_lock);
	//		result = instance_dispatch[GetKey(instance)].EnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
	//	}
	//	return result;
	//}
	//else {
	//	uint32_t physicalCount = 0;
	//	{
	//		scoped_lock l(global_lock);
	//		instance_dispatch[GetKey(sharedInstance)].EnumeratePhysicalDevices(sharedInstance, &physicalCount, nullptr);
	//	}
	//	std::vector<VkPhysicalDevice> physicalDevicesLocal;
	//	physicalDevicesLocal.resize(physicalCount);
	//	{
	//		scoped_lock l(global_lock);
	//		instance_dispatch[GetKey(sharedInstance)].EnumeratePhysicalDevices(sharedInstance, &physicalCount, physicalDevicesLocal.data());
	//	}

	//	in_XR_GetPhysicalDevice = true;
	//	xrPhysicalDevice = XR_GetPhysicalDevice(sharedInstance);
	//	in_XR_GetPhysicalDevice = false;
	//	if (pPhysicalDevices == nullptr) {
	//		*pPhysicalDeviceCount = 1;
	//		return VK_SUCCESS;
	//	}
	//	else {
	//		*pPhysicalDeviceCount = 1;
	//		*pPhysicalDevices = physicalDevicesLocal.at(0);
	//		return VK_SUCCESS;
	//	}
	//}
}

VK_LAYER_EXPORT void VKAPI_CALL Layer_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) {
	scoped_lock l(global_lock);
	logPrint(std::to_string(*pQueueFamilyPropertyCount));
	instance_dispatch[GetKey(physicalDevice)].GetPhysicalDeviceQueueFamilyProperties(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
	logPrint(std::to_string(*pQueueFamilyPropertyCount));
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
	//PFN_vkCreateDevice next_CreateDevice = (PFN_vkCreateDevice)next_GetDeviceProcAddr(VK_NULL_HANDLE, "vkCreateDevice");
	//VkResult result = next_CreateDevice(gpu, pCreateInfo, pAllocator, pDevice);
	VkResult result = XR_CreateCompatibleVulkanDevice(Layer_NESTED_GetInstanceProcAddr, gpu, pCreateInfo, pAllocator, pDevice);
	if (result != VK_SUCCESS)
		return result;

	logPrint("Created Vulkan device successfully!");

	VkLayerDispatchTable dispatchTable = {};
	dispatchTable.GetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)next_GetDeviceProcAddr(*pDevice, "vkGetDeviceProcAddr");
	dispatchTable.DestroyDevice = (PFN_vkDestroyDevice)next_GetDeviceProcAddr(*pDevice, "vkDestroyDevice");

	{
		scoped_lock l(global_lock);
		device_dispatch[GetKey(gpu)] = dispatchTable;
	}

	return VK_SUCCESS;
}

VK_LAYER_EXPORT void VKAPI_CALL Layer_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
}

VkResult Layer_EnumerateInstanceVersion(const VkEnumerateInstanceVersionChain* pChain, uint32_t* pApiVersion) {
	XrVersion minVersion = 0;
	XrVersion maxVersion = 0;
	XR_GetSupportedVulkanVersions(&minVersion, &maxVersion);
	*pApiVersion = VK_API_VERSION_1_2;
	return pChain->CallDown(pApiVersion);
}

// GetProcAddr hooks
// todo: implement GetPhysicalDeviceProcAddr if necessary?
// https://github.dev/crosire/reshade/tree/main/source/vulkan
// https://github.dev/baldurk/renderdoc/tree/v1.x/renderdoc/driver/vulkan
PFN_vkCreateInstance origVulkanExtension = nullptr;
VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_WRAPPED_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
	VkResult result = origVulkanExtension(pCreateInfo, pAllocator, pInstance);
	logPrint(std::format("vkCreateInstance: {} {} {} {}", (void*)pCreateInfo, (void*)pAllocator, (void*)pAllocator, (void*)*pInstance));
	return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_WRAPPED_EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
	logPrint(std::format("EnumerateInstanceExtensionProperties: {} {}", pLayerName, *pPropertyCount));
	return VK_SUCCESS;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_WRAPPED_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
	logPrint(std::format("EnumerateInstanceLayerProperties: {}", *pPropertyCount));
	return VK_SUCCESS;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_WRAPPED_EnumerateInstanceVersion(uint32_t* pApiVersion) {
	logPrint(std::format("EnumerateInstanceVersion: {}", *pApiVersion));
	return VK_SUCCESS;
}


VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL Layer_GetInstanceProcAddr(VkInstance instance, const char* pName) {	
	if (!useHookedFuncs) {
		//HOOK_PROC_FUNC(EnumerateInstanceExtensionProperties);
		//PFN_vkVoidFunction address = nullptr;
		//address = saved_GetInstanceProcAddr(instance, pName);
		logPrint(std::format("SHOULDN'T BE CALLED!! GetInstanceProcAddr: {} {}", pName, (void*)instance));
		//
		//if (!strcmp(pName, "vkCreateInstance")) {
		//	origVulkanExtension = (PFN_vkCreateInstance)address;
		//	return (PFN_vkVoidFunction)Layer_WRAPPED_CreateInstance;
		//}
		//if (!strcmp(pName, "vkEnumerateInstanceExtensionProperties")) {
		//	return (PFN_vkVoidFunction)Layer_WRAPPED_EnumerateInstanceExtensionProperties;
		//}
		//if (!strcmp(pName, "vkEnumerateInstanceLayerProperties")) {
		//	return (PFN_vkVoidFunction)Layer_WRAPPED_EnumerateInstanceLayerProperties;
		//}
		//if (!strcmp(pName, "vkEnumerateInstanceVersion")) {
		//	return (PFN_vkVoidFunction)Layer_WRAPPED_EnumerateInstanceVersion;
		//}
		//else
		//	return address;
		//PFN_vkVoidFunction address = nullptr;
		//address = saved_GetInstanceProcAddr(instance, pName);
		//logPrint(std::format("GetInstanceProcAddr: {} {} {}", pName, (void*)instance, (void*)address));
		scoped_lock l(global_lock);
		return instance_dispatch[GetKey(instance)].GetInstanceProcAddr(instance, pName);
	}
	else {
		HOOK_PROC_FUNC(CreateInstance);
		HOOK_PROC_FUNC(DestroyInstance);
		HOOK_PROC_FUNC(CreateDevice);
		HOOK_PROC_FUNC(DestroyDevice);

		// todo: SteamVR seems to go into a loop, so am gonna try just faking the PhysicalDevice :3
		//HOOK_PROC_FUNC(EnumerateInstanceVersion);
		//HOOK_PROC_FUNC(EnumeratePhysicalDevices);
		//HOOK_PROC_FUNC(GetPhysicalDeviceQueueFamilyProperties);

		// Self-intercept for compatibility
		HOOK_PROC_FUNC(GetInstanceProcAddr);

		{
			scoped_lock l(global_lock);
			return instance_dispatch[GetKey(instance)].GetInstanceProcAddr(instance, pName);
		}
	}
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL Layer_GetDeviceProcAddr(VkDevice device, const char* pName) {
	HOOK_PROC_FUNC(CreateDevice);
	HOOK_PROC_FUNC(DestroyDevice);

	// Required to self-intercept for compatibility
	HOOK_PROC_FUNC(GetDeviceProcAddr);

	{
		scoped_lock l(global_lock);
		return device_dispatch[GetKey(device)].GetDeviceProcAddr(device, pName);
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

// Layer init and shutdown
//std::atomic_bool hookedCemu = false;

//VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
//	if (initializeLayer()) hookedCemu = true;
//	VkLayerInstanceCreateInfo* layerCreateInfo = (VkLayerInstanceCreateInfo*)pCreateInfo->pNext;
//
//	// step through the chain of pNext until we get to the link info
//	while (layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || layerCreateInfo->function != VK_LAYER_LINK_INFO)) {
//		layerCreateInfo = (VkLayerInstanceCreateInfo*)layerCreateInfo->pNext;
//	}
//
//	if (layerCreateInfo == NULL) {
//		// No loader instance create info
//		return VK_ERROR_INITIALIZATION_FAILED;
//	}
//
//	PFN_vkGetInstanceProcAddr gpa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
//	// move chain on for next layer
//	layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;
//
//	PFN_vkCreateInstance createFunc = (PFN_vkCreateInstance)gpa(VK_NULL_HANDLE, "vkCreateInstance");
//
//	if (hookedCemu) modifyInstanceExtensions(const_cast<VkInstanceCreateInfo*>(pCreateInfo));
//	createFunc(pCreateInfo, pAllocator, pInstance);
//	instanceHandle = *pInstance;
//
//	// fetch functions we need from the next layer and add it to the dispatch table so that we can call it
//	VkLayerInstanceDispatchTable dispatchTable;
//	dispatchTable.GetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)gpa(*pInstance, "vkGetInstanceProcAddr");
//	dispatchTable.DestroyInstance = (PFN_vkDestroyInstance)gpa(*pInstance, "vkDestroyInstance");
//	dispatchTable.EnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties)gpa(*pInstance, "vkEnumerateDeviceExtensionProperties");
//
//	dispatchTable.GetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)gpa(*pInstance, "vkGetPhysicalDeviceMemoryProperties");
//	dispatchTable.GetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2)gpa(*pInstance, "vkGetPhysicalDeviceProperties2");
//	dispatchTable.GetPhysicalDeviceProperties2KHR = (PFN_vkGetPhysicalDeviceProperties2KHR)gpa(*pInstance, "vkGetPhysicalDeviceProperties2KHR");
//	dispatchTable.CreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR)gpa(*pInstance, "vkCreateWin32SurfaceKHR");
//	dispatchTable.GetPhysicalDeviceSurfaceFormatsKHR = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)gpa(*pInstance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
//	dispatchTable.GetPhysicalDeviceImageFormatProperties2 = (PFN_vkGetPhysicalDeviceImageFormatProperties2)gpa(*pInstance, "vkGetPhysicalDeviceImageFormatProperties2");
//
//	// store the table by key
//	{
//		scoped_lock l(global_lock);
//		instance_dispatch[GetKey(*pInstance)] = dispatchTable;
//	}
//
//	return VK_SUCCESS;
//}

//VK_LAYER_EXPORT void VKAPI_CALL Layer_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
//	if (hookedCemu) shutdownLayer();
//	scoped_lock l(global_lock);
//	instance_dispatch.erase(GetKey(instance));
//}
//
//VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
//	VkLayerDeviceCreateInfo* layerCreateInfo = (VkLayerDeviceCreateInfo*)pCreateInfo->pNext;
//
//	// step through the chain of pNext until we get to the link info
//	while (layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || layerCreateInfo->function != VK_LAYER_LINK_INFO)) {
//		layerCreateInfo = (VkLayerDeviceCreateInfo*)layerCreateInfo->pNext;
//	}
//
//	if (layerCreateInfo == NULL) {
//		// No loader instance create info
//		return VK_ERROR_INITIALIZATION_FAILED;
//	}
//
//	PFN_vkGetInstanceProcAddr gipa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
//	PFN_vkGetDeviceProcAddr gdpa = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
//	// move chain on for next layer
//	layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;
//
//	PFN_vkCreateDevice createFunc = (PFN_vkCreateDevice)gipa(VK_NULL_HANDLE, "vkCreateDevice");
//
//	physicalDeviceHandle = physicalDevice;
//	if (hookedCemu) modifyDeviceExtensions(const_cast<VkDeviceCreateInfo*>(pCreateInfo));
//	VkResult ret = createFunc(physicalDevice, pCreateInfo, pAllocator, pDevice);
//	deviceHandle = *pDevice;
//
//	// fetch our own dispatch table for the functions we need, into the next layer
//	VkLayerDispatchTable dispatchTable;
//	dispatchTable.GetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)gdpa(*pDevice, "vkGetDeviceProcAddr");
//	dispatchTable.DestroyDevice = (PFN_vkDestroyDevice)gdpa(*pDevice, "vkDestroyDevice");
//
//	dispatchTable.CreateImage = (PFN_vkCreateImage)gdpa(*pDevice, "vkCreateImage");
//	dispatchTable.CreateImageView = (PFN_vkCreateImageView)gdpa(*pDevice, "vkCreateImageView");
//	dispatchTable.UpdateDescriptorSets = (PFN_vkUpdateDescriptorSets)gdpa(*pDevice, "vkUpdateDescriptorSets");
//	dispatchTable.CmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets)gdpa(*pDevice, "vkCmdBindDescriptorSets");
//	dispatchTable.CreateRenderPass = (PFN_vkCreateRenderPass)gdpa(*pDevice, "vkCreateRenderPass");
//	dispatchTable.CmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)gdpa(*pDevice, "vkCmdBeginRenderPass");
//	dispatchTable.CmdEndRenderPass = (PFN_vkCmdEndRenderPass)gdpa(*pDevice, "vkCmdEndRenderPass");
//	dispatchTable.QueuePresentKHR = (PFN_vkQueuePresentKHR)gdpa(*pDevice, "vkQueuePresentKHR");
//	dispatchTable.QueueSubmit = (PFN_vkQueueSubmit)gdpa(*pDevice, "vkQueueSubmit");
//
//	dispatchTable.AllocateMemory = (PFN_vkAllocateMemory)gdpa(*pDevice, "vkAllocateMemory");
//	dispatchTable.BindImageMemory = (PFN_vkBindImageMemory)gdpa(*pDevice, "vkBindImageMemory");
//	dispatchTable.GetImageMemoryRequirements = (PFN_vkGetImageMemoryRequirements)gdpa(*pDevice, "vkGetImageMemoryRequirements");
//	dispatchTable.CreateImage = (PFN_vkCreateImage)gdpa(*pDevice, "vkCreateImage");
//	dispatchTable.CmdCopyImage = (PFN_vkCmdCopyImage)gdpa(*pDevice, "vkCmdCopyImage");
//	dispatchTable.CmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)gdpa(*pDevice, "vkCmdPipelineBarrier");
//	dispatchTable.GetImageMemoryRequirements2 = (PFN_vkGetImageMemoryRequirements2)gdpa(*pDevice, "vkGetImageMemoryRequirements2");
//	dispatchTable.EndCommandBuffer = (PFN_vkEndCommandBuffer)gdpa(*pDevice, "vkEndCommandBuffer");
//
//	// store the table by key
//	{
//		scoped_lock l(global_lock);
//		device_dispatch[GetKey(*pDevice)] = dispatchTable;
//	}
//	return VK_SUCCESS;
//}

//VK_LAYER_EXPORT void VKAPI_CALL Layer_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
//	scoped_lock l(global_lock);
//	device_dispatch.erase(GetKey(device));
//}

// Enumeration functions

//VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
//	if (pPropertyCount) *pPropertyCount = 1;
//
//	if (pProperties) {
//		strcpy_s(pProperties->layerName, const_BetterVR_Layer_Name);
//		strcpy_s(pProperties->description, const_BetterVR_Layer_Description);
//		pProperties->implementationVersion = 1;
//		pProperties->specVersion = VK_MAKE_VERSION(1,3,0);
//	}
//
//	return VK_SUCCESS;
//}
//
//VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
//	return Layer_EnumerateInstanceLayerProperties(pPropertyCount, pProperties);
//}
//
//VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
//	if (pLayerName == NULL || strcmp(pLayerName, const_BetterVR_Layer_Name))
//		return VK_ERROR_LAYER_NOT_PRESENT;
//
//	// don't expose any extensions
//	if (pPropertyCount) *pPropertyCount = 0;
//	return VK_SUCCESS;
//}
//
//VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
//	// pass through any queries that aren't to us
//	if (pLayerName == NULL || strcmp(pLayerName, const_BetterVR_Layer_Name)) {
//		if (physicalDevice == VK_NULL_HANDLE)
//			return VK_SUCCESS;
//
//		scoped_lock l(global_lock);
//		return instance_dispatch[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
//	}
//
//	// don't expose any extensions
//	if (pPropertyCount) *pPropertyCount = 0;
//	return VK_SUCCESS;
//}


// Read GetDeviceProcAddr

//VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL Layer_GetDeviceProcAddr(VkDevice device, const char* pName) {
//	GETPROCADDR(GetDeviceProcAddr);
//	GETPROCADDR(CreateDevice);
//	GETPROCADDR(DestroyDevice);
//
//	GETPROCADDR(EnumerateDeviceLayerProperties);
//	GETPROCADDR(EnumerateDeviceExtensionProperties);
//
//	// device chain functions we intercept
//	if (hookedCemu) {
//		GETPROCADDR(CreateRenderPass);
//		GETPROCADDR(CmdBeginRenderPass);
//		GETPROCADDR(CmdEndRenderPass);
//		GETPROCADDR(QueuePresentKHR);
//
//		GETPROCADDR(QueueSubmit);
//	}
//
//	{
//		scoped_lock l(global_lock);
//		return device_dispatch[GetKey(device)].GetDeviceProcAddr(device, pName);
//	}
//}

//VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL Layer_GetInstanceProcAddr(VkInstance instance, const char* pName) {
//	GETPROCADDR(GetInstanceProcAddr);
//	GETPROCADDR(GetDeviceProcAddr);
//
//	GETPROCADDR(CreateInstance);
//	GETPROCADDR(DestroyInstance);
//	GETPROCADDR(EnumerateInstanceLayerProperties);
//	GETPROCADDR(EnumerateInstanceExtensionProperties);
//
//	GETPROCADDR(EnumerateDeviceLayerProperties);
//	GETPROCADDR(EnumerateDeviceExtensionProperties);
//	GETPROCADDR(CreateDevice);
//	GETPROCADDR(DestroyDevice);
//
//	if (hookedCemu) {
//		// instance chain functions we intercept
//		GETPROCADDR(CreateRenderPass);
//		GETPROCADDR(CmdBeginRenderPass);
//		GETPROCADDR(CmdEndRenderPass);
//		GETPROCADDR(QueuePresentKHR);
//		GETPROCADDR(QueueSubmit);
//
//		// device chain functions we intercept
//	}
//
//	{
//		scoped_lock l(global_lock);
//		return instance_dispatch[GetKey(instance)].GetInstanceProcAddr(instance, pName);
//	}
//}