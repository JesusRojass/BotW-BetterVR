#include "layer.h"

// #define ENABLE_FUNC_LOGGING

std::list<VkInstance> steamInstances;
std::list<VkDevice> steamDevices;
HMODULE vulkanModule = NULL;

// Original functions
PFN_vkCreateInstance orig_CreateInstance = nullptr;
PFN_vkCreateDevice orig_CreateDevice = nullptr;
PFN_vkDestroyInstance orig_DestroyInstance = nullptr;
PFN_vkDestroyDevice orig_DestroyDevice = nullptr;
PFN_vkGetInstanceProcAddr orig_GetInstanceProcAddr = nullptr;
PFN_vkGetDeviceProcAddr orig_GetDeviceProcAddr = nullptr;

PFN_vkEnumeratePhysicalDevices orig_EnumeratePhysicalDevices = nullptr;

// External variables
extern PFN_vkGetInstanceProcAddr saved_GetInstanceProcAddr;
extern PFN_vkGetDeviceProcAddr saved_GetDeviceProcAddr;

// Resolve macros
#define FUNC_LOGGING_LEVEL 2

#if FUNC_LOGGING_LEVEL == 2
//#define LOG_FUNC_RESOLVE(resolveFuncType, resolveFunc, object, name) \
//logPrint(std::format("[GetProcAddr] Using {}: vkGet*ProcAddr(object={}, name={}) returned {}", resolveFuncType, (void*)object, name, (void*)resolveFunc(object, name));\
//resolveFunc(object, name);
//
//#define LOG_ERROR_FUNC_RESOLVE(resolveFuncType, resolveFunc, object, name) \
//if (resolveFunc(object, name) == nullptr) logPrint(std::format("[GetProcAddr] Failed using {}: vkGet*ProcAddr(object={}, name={}) returned {}", resolveFuncType, (void*)object, name, (void*)resolveFunc(object, name));\
//resolveFunc(object, name);

#define logDebugPrintAddr(func) logPrint(func);
#else
#define LOG_FUNC_RESOLVE(func, funcName) resolveFunc(object, name);
#define LOG_ERROR_FUNC_RESOLVE(func, funcName) resolveFunc(object, name);
#define logDebugPrintAddr(func) {}
#endif

#define HOOK_STEAMVR_FUNC(func, origFuncPtr) if(!strcmp(pName, "vk" #func)) {\
	logPrint(std::format("[INFO] Hooked {}, original = {}", pName, (void*)origFuncPtr));\
	orig_##func = (PFN_vk##func)origFuncPtr;\
	return (PFN_vkVoidFunction)&SteamVRHook_##func;\
}

void SteamVRHook_initialize() {
	SetEnvironmentVariableA("VK_INSTANCE_LAYERS", NULL);
	//SetEnvironmentVariableA("DISABLE_RTSS_LAYER", "1");
	//SetEnvironmentVariableA("DISABLE_VULKAN_OBS_CAPTURE", "1");

	vulkanModule = LoadLibraryA("vulkan-1.dll");
}

VkResult VKAPI_CALL SteamVRHook_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
	VkResult result = orig_CreateInstance(pCreateInfo, pAllocator, pInstance);
	steamInstances.emplace_back(*pInstance);
	logPrint(std::format("Created new SteamVR instance {} with these extensions:", (void*)*pInstance));
	for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
		logPrint(std::format(" - {}", pCreateInfo->ppEnabledExtensionNames[i]));
	}
	return result;
}

VkResult VKAPI_CALL SteamVRHook_CreateDevice(VkPhysicalDevice gpu, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
	VkResult result = orig_CreateDevice(gpu, pCreateInfo, pAllocator, pDevice);
	steamDevices.emplace_back(*pDevice);
	logPrint(std::format("Created new SteamVR device {} with these extensions:", (void*)*pDevice));
	for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
		logPrint(std::format(" - {}", pCreateInfo->ppEnabledExtensionNames[i]));
	}
	return result;
}

void VKAPI_CALL SteamVRHook_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
	logPrint(std::format("Destroyed SteamVR instance {}", (void*)instance));
	orig_DestroyInstance(instance, pAllocator);
	steamInstances.remove(instance);
}

void VKAPI_CALL SteamVRHook_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
	orig_DestroyDevice(device, pAllocator);
	steamDevices.remove(device);
	logPrint(std::format("Destroyed SteamVR device {}", (void*)device));
}

VkResult VKAPI_CALL SteamVRHook_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) {
	VkResult result = orig_EnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
	logPrint(std::format("Enumerated {} physical devices in SteamVR with this instance {}", (uint32_t)*pPhysicalDeviceCount, (void*)instance));
	for (uint32_t i = 0; i < *pPhysicalDeviceCount; i++) {
		logPrint(std::format(" - {}", (void*)pPhysicalDevices[i]));
	}
	return result;
}

VkInstance topVkInstance = nullptr;
VkDevice topVkDevice = nullptr;

PFN_vkVoidFunction VKAPI_CALL SteamVRHook_GetInstanceProcAddr(VkInstance instance, const char* pName) {
	PFN_vkGetInstanceProcAddr top_InstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(reinterpret_cast<HMODULE>(vulkanModule), "vkGetInstanceProcAddr"));
	
	HOOK_STEAMVR_FUNC(CreateInstance, saved_GetInstanceProcAddr(instance, pName));
	HOOK_STEAMVR_FUNC(CreateDevice, saved_GetInstanceProcAddr(instance, pName));
	HOOK_STEAMVR_FUNC(DestroyInstance, top_InstanceProcAddr(instance, pName));
	HOOK_STEAMVR_FUNC(DestroyDevice, top_InstanceProcAddr(instance, pName));

	HOOK_STEAMVR_FUNC(EnumeratePhysicalDevices, top_InstanceProcAddr(instance, pName));

	HOOK_STEAMVR_FUNC(GetInstanceProcAddr, saved_GetInstanceProcAddr);
	HOOK_STEAMVR_FUNC(GetDeviceProcAddr, SteamVRHook_GetDeviceProcAddr);


	// Capture TopInstance by waiting till the hooked CreateInstance gets used, which it'll try to use next time!
	if (instance != nullptr && !steamInstances.empty() && topVkInstance == nullptr) {
		topVkInstance = instance;
		logDebugPrintAddr(std::format("[DEBUG] Found top level instance {} from created SteamVR instance {}!", (void*)topVkInstance, (void*)steamInstances.front()));
	}

	// ERROR IN CODE COULD BE RELATED TO US PREVIOUSLY ONLY STORING ONE STEAMVR INSTANCE, SO NEWLY CREATED ONES COULD HAVE DIFFERENT SPECS

	PFN_vkVoidFunction funcRet = nullptr;
	std::string resolvedUsing = "UNKNOWN";
	if (instance == nullptr) {
		funcRet = top_InstanceProcAddr(nullptr, pName);
		resolvedUsing = "NULL instance, so top_InstanceProcAddr";
	}
	else if (instance == vkSharedInstance) {
		funcRet = saved_GetInstanceProcAddr(instance, pName);
		resolvedUsing = std::format("vkInstance={} == sharedInstance={}, saved_GetInstanceProcAddr", (void*)instance, (void*)vkSharedInstance);
	}
	else if (instance == topVkInstance) {
		{
			scoped_lock l(global_lock);
			funcRet = instance_dispatch[GetKey(vkSharedInstance)].GetInstanceProcAddr(vkSharedInstance, pName);
		}
		resolvedUsing = std::format("use instance dispatch table", (void*)topVkInstance);
		if (funcRet == nullptr) {
			scoped_lock l(global_lock);
			funcRet = device_dispatch[GetKey(vkSharedDevice)].GetDeviceProcAddr(vkSharedDevice, pName);
			resolvedUsing = std::format("use device dispatch table", (void*)topVkInstance);
		}
	}
	/*else if (topVkInstance != nullptr) {
		funcRet = top_InstanceProcAddr(topVkInstance, pName);
		resolvedUsing = std::format("topVkInstance={}, saved_GetInstanceProcAddr", (void*)topVkInstance, (void*)vkSharedInstance);
	}
	else if (vkSharedDevice == nullptr) {
		topVkInstance = instance;
		funcRet = top_InstanceProcAddr(topVkInstance, pName);
		resolvedUsing = std::format("didn't know vkInstance={}, saved_GetInstanceProcAddr", (void*)topVkInstance);
	}
	else if (vkSharedDevice != nullptr) {
		topVkInstance = instance;
		funcRet = top_InstanceProcAddr(topVkInstance, pName);
		resolvedUsing = std::format("didn't know vkInstance={}, saved_GetInstanceProcAddr", (void*)topVkInstance);
	}*/
	else {
		//logDebugPrintAddr(std::format("[DEBUG] TopInstance={} SharedInstance={} SharedDevice={} ParameterInstance={}", (void*)topVkInstance, (void*)vkSharedInstance, (void*)vkSharedDevice, (void*)instance));
		//for (const auto& steamInstance : steamInstances) {
		//	logDebugPrintAddr(std::format(" - VkInstance = {}, address = {}", (void*)steamInstance, (void*)saved_GetInstanceProcAddr(steamInstance, pName)));
		//}
		//for (const auto& steamDevice : steamDevices) {
		//	logDebugPrintAddr(std::format(" - VkDevice = {}, address = {}", (void*)steamDevice, (void*)saved_GetDeviceProcAddr(steamDevice, pName)));
		//}
		//logDebugPrintAddr(std::format("[ERROR] Using {}: vkGet*ProcAddr(name=\"{}\") returned {}", resolvedUsing, pName, (void*)funcRet));
	}

#if FUNC_LOGGING_LEVEL == 2
	if (funcRet == nullptr) {
		logDebugPrintAddr(std::format("[DEBUG] TopInstance={} TopDevice={} SharedInstance={} SharedDevice={} ParameterInstance={}", (void*)topVkInstance, (void*)topVkDevice,(void*)vkSharedInstance, (void*)vkSharedDevice, (void*)instance));
		logDebugPrintAddr(std::format("[ERROR] Using {}: vkGet*ProcAddr(name=\"{}\") returned {}", resolvedUsing, pName, (void*)funcRet));
	}
	else {
		//logDebugPrintAddr(std::format("[DEBUG] Using {}: vkGet*ProcAddr(name=\"{}\") returned {}", resolvedUsing, pName, (void*)funcRet));
	}
#elif FUNC_LOGGING_LEVEL == 1
	if (funcRet == nullptr) {
		logDebugPrintAddr(std::format("[ERROR] Using {}: vkGet*ProcAddr(name=\"{}\") returned {}", resolvedUsing, pName, (void*)funcRet));
	}
#endif
	return funcRet;
}

PFN_vkVoidFunction VKAPI_CALL SteamVRHook_GetDeviceProcAddr(VkDevice device, const char* pName) {
	PFN_vkGetDeviceProcAddr top_DeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(reinterpret_cast<HMODULE>(vulkanModule), "vkGetDeviceProcAddr"));

	HOOK_STEAMVR_FUNC(CreateInstance, saved_GetDeviceProcAddr(device, pName));
	HOOK_STEAMVR_FUNC(CreateDevice, saved_GetDeviceProcAddr(device, pName));
	HOOK_STEAMVR_FUNC(DestroyInstance, top_DeviceProcAddr(device, pName));
	HOOK_STEAMVR_FUNC(DestroyDevice, top_DeviceProcAddr(device, pName));

	HOOK_STEAMVR_FUNC(EnumeratePhysicalDevices, top_DeviceProcAddr(device, pName));

	HOOK_STEAMVR_FUNC(GetInstanceProcAddr, SteamVRHook_GetInstanceProcAddr);
	HOOK_STEAMVR_FUNC(GetDeviceProcAddr, saved_GetDeviceProcAddr);

	// Capture TopInstance by waiting till the hooked CreateInstance gets used, which it'll try to use next time!
	if (device != nullptr && !steamDevices.empty() && topVkDevice == nullptr) {
		topVkDevice = device;
		logDebugPrintAddr(std::format("[DEBUG] Found top level device {} from created SteamVR device {}!", (void*)topVkInstance, (void*)steamInstances.front()));
	}

	//logDebugPrintAddr(std::format("[DEBUG] TopInstance={} TopDevice={} SharedInstance={} SharedDevice={} ParameterInstance={}", (void*)topVkInstance, (void*)topVkDevice, (void*)vkSharedInstance, (void*)vkSharedDevice, (void*)device));

	PFN_vkVoidFunction funcRet = nullptr;
	std::string resolvedUsing = "UNKNOWN";

	if (device == vkSharedDevice) {
		funcRet = saved_GetDeviceProcAddr(device, pName);
		resolvedUsing = std::format("vkDevice={} == sharedDevice={}, saved_GetDeviceProcAddr", (void*)device, (void*)vkSharedDevice);
	}
	else if (device == topVkDevice) {
		{
			scoped_lock l(global_lock);
			funcRet = instance_dispatch[GetKey(vkSharedInstance)].GetInstanceProcAddr(vkSharedInstance, pName);
		}
		resolvedUsing = std::format("use instance dispatch table");
		if (funcRet == nullptr) {
			scoped_lock l(global_lock);
			funcRet = device_dispatch[GetKey(vkSharedDevice)].GetDeviceProcAddr(vkSharedDevice, pName);
			resolvedUsing = std::format("use device dispatch table");
		}
	}

#if FUNC_LOGGING_LEVEL == 2
	if (funcRet == nullptr) {
		logDebugPrintAddr(std::format("[DEBUG] TopInstance={} TopDevice={} SharedInstance={} SharedDevice={} ParameterInstance={}", (void*)topVkInstance, (void*)topVkDevice, (void*)vkSharedInstance, (void*)vkSharedDevice, (void*)device));
		logDebugPrintAddr(std::format("[ERROR] Using {}: vkGetDeviceProcAddr(name=\"{}\") returned {}", resolvedUsing, pName, (void*)funcRet));
	}
	else {
		//logDebugPrintAddr(std::format("[DEBUG] Using {}: vkGetDeviceProcAddr(name=\"{}\") returned {}", resolvedUsing, pName, (void*)funcRet));
	}
#elif FUNC_LOGGING_LEVEL == 1
	if (funcRet == nullptr) {
		logDebugPrintAddr(std::format("[ERROR] Using {}: vkGetDeviceProcAddr(name=\"{}\") returned {}", resolvedUsing, pName, (void*)funcRet));
	}
#endif

	return funcRet;
}

//	PFN_vkVoidFunction funcRet = saved_GetDeviceProcAddr(device, pName);
//#ifdef ENABLE_FUNC_LOGGING
//	logPrint(std::format("Intercepted NESTED GetDeviceProcAddr load: {} {} {}", pName, (void*)device, (void*)funcRet));
//#endif