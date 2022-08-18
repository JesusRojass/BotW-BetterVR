#include "layer.h"

XrInstance xrSharedInstance = XR_NULL_HANDLE;
XrSystemId xrSharedSystemId = XR_NULL_SYSTEM_ID;
XrSession xrSharedSession = XR_NULL_HANDLE;

XrDebugUtilsMessengerEXT xrDebugMessengerHandle = XR_NULL_HANDLE;
//XrGraphicsBindingVulkanKHR xrVulkanBindings = {};
//VkSurfaceKHR xrSurfaceHandle = XR_NULL_HANDLE;
//uint32_t hmdViewCount = 0;

// Instance Properties/Requirements
uint32_t xrMaxSwapchainWidth = 0;
uint32_t xrMaxSwapchainHeight = 0;
bool xrSupportsOrientational = false;
bool xrSupportsPositional = false;
bool xrSupportsFovMutable = false;

void XR_initPointers(bool timeConvSupported, bool debugUtilsSupported) {
	xrGetInstanceProcAddr(xrSharedInstance, "xrGetVulkanGraphicsRequirements2KHR", (PFN_xrVoidFunction*)&func_xrGetVulkanGraphicsRequirements2KHR);
	xrGetInstanceProcAddr(xrSharedInstance, "xrCreateVulkanInstanceKHR", (PFN_xrVoidFunction*)&func_xrCreateVulkanInstanceKHR);
	xrGetInstanceProcAddr(xrSharedInstance, "xrCreateVulkanDeviceKHR", (PFN_xrVoidFunction*)&func_xrCreateVulkanDeviceKHR);
	xrGetInstanceProcAddr(xrSharedInstance, "xrGetVulkanGraphicsDevice2KHR", (PFN_xrVoidFunction*)&func_xrGetVulkanGraphicsDevice2KHR);

	if (timeConvSupported) {
		xrGetInstanceProcAddr(xrSharedInstance, "xrConvertTimeToWin32PerformanceCounterKHR", (PFN_xrVoidFunction*)&func_xrConvertTimeToWin32PerformanceCounterKHR);
		xrGetInstanceProcAddr(xrSharedInstance, "xrConvertWin32PerformanceCounterToTimeKHR", (PFN_xrVoidFunction*)&func_xrConvertWin32PerformanceCounterToTimeKHR);
	}

	if (debugUtilsSupported) {
		xrGetInstanceProcAddr(xrSharedInstance, "xrCreateDebugUtilsMessengerEXT", (PFN_xrVoidFunction*)&func_xrCreateDebugUtilsMessengerEXT);
		xrGetInstanceProcAddr(xrSharedInstance, "xrDestroyDebugUtilsMessengerEXT", (PFN_xrVoidFunction*)&func_xrDestroyDebugUtilsMessengerEXT);
	}
}

void XR_deinitPointers() {
	func_xrGetVulkanGraphicsRequirements2KHR = nullptr;
	func_xrCreateVulkanInstanceKHR = nullptr;
	func_xrCreateVulkanDeviceKHR = nullptr;
	func_xrGetVulkanGraphicsDevice2KHR = nullptr;
	func_xrConvertTimeToWin32PerformanceCounterKHR = nullptr;
	func_xrConvertWin32PerformanceCounterToTimeKHR = nullptr;
	func_xrCreateDebugUtilsMessengerEXT = nullptr;
	func_xrDestroyDebugUtilsMessengerEXT = nullptr;
}

void XR_DebugUtilsMessengerCallback(XrDebugUtilsMessageSeverityFlagsEXT messageSeverity, XrDebugUtilsMessageTypeFlagsEXT messageType, const XrDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData) {
	logPrint(std::string(callbackData->functionName) + std::string(": ") + std::string(callbackData->message));
}

void XR_initInstance() {
	if (xrSharedInstance != XR_NULL_HANDLE)
		throw std::runtime_error("Shouldn't initialize the instance twice!");

	uint32_t xrExtensionCount = 0;
	xrEnumerateInstanceExtensionProperties(NULL, 0, &xrExtensionCount, NULL);
	std::vector<XrExtensionProperties> instanceExtensions;
	instanceExtensions.resize(xrExtensionCount, { XR_TYPE_EXTENSION_PROPERTIES , NULL });
	checkXRResult(xrEnumerateInstanceExtensionProperties(NULL, xrExtensionCount, &xrExtensionCount, instanceExtensions.data()), "Couldn't enumerate OpenXR extensions!");

	bool vulkanSupported = false;
	bool timeConvSupported = false;
	bool debugUtilsSupported = false;
	for (XrExtensionProperties& extensionProperties : instanceExtensions) {
		if (strcmp(extensionProperties.extensionName, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME) == 0) {
			vulkanSupported = true;
		}
		else if (strcmp(extensionProperties.extensionName, XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME) == 0) {
			timeConvSupported = true;
		}
		else if (strcmp(extensionProperties.extensionName, XR_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
			debugUtilsSupported = true;
		}
	}

	if (!vulkanSupported) {
		logPrint("OpenXR runtime doesn't support Vulkan (XR_KHR_VULKAN_ENABLE2)!");
		throw std::runtime_error("Current OpenXR runtime doesn't support Vulkan (XR_KHR_VULKAN_ENABLE2). See the Github page's troubleshooting section for a solution!");
	}
	if (!timeConvSupported) {
		logPrint("OpenXR runtime doesn't support converting time from/to XrTime (XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME)!");
		throw std::runtime_error("Current OpenXR runtime doesn't support converting time from/to XrTime (XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME). See the Github page's troubleshooting section for a solution!");
	}
	if (!debugUtilsSupported) {
		logPrint("OpenXR runtime doesn't support debug utils (XR_EXT_DEBUG_UTILS)! Errors/debug information will no longer be able to be shown!");
	}

	// todo: disable debug utils when compiled in release mode
	std::vector<const char*> enabledExtensions = { XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME, XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME };
	if (debugUtilsSupported) enabledExtensions.emplace_back(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);

	XrInstanceCreateInfo xrInstanceCreateInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
	xrInstanceCreateInfo.createFlags = 0;
	xrInstanceCreateInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
	xrInstanceCreateInfo.enabledExtensionNames = enabledExtensions.data();
	xrInstanceCreateInfo.enabledApiLayerCount = 0;
	xrInstanceCreateInfo.enabledApiLayerNames = NULL;
	xrInstanceCreateInfo.applicationInfo = { "BetterVR OpenXR", 1, "Cemu", 1, XR_CURRENT_API_VERSION };
	checkXRResult(xrCreateInstance(&xrInstanceCreateInfo, &xrSharedInstance), "Failed to initialize the OpenXR instance!");

	XR_initPointers(timeConvSupported, debugUtilsSupported);

	if (debugUtilsSupported) {
		XrDebugUtilsMessengerCreateInfoEXT utilsMessengerCreateInfo = { XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		utilsMessengerCreateInfo.messageSeverities = XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		utilsMessengerCreateInfo.messageTypes = XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
		utilsMessengerCreateInfo.userCallback = (PFN_xrDebugUtilsMessengerCallbackEXT)&XR_DebugUtilsMessengerCallback;
		func_xrCreateDebugUtilsMessengerEXT(xrSharedInstance, &utilsMessengerCreateInfo, &xrDebugMessengerHandle);
	}

	// System
	XrSystemGetInfo xrSystemGetInfo = { XR_TYPE_SYSTEM_GET_INFO };
	xrSystemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	checkXRResult(xrGetSystem(xrSharedInstance, &xrSystemGetInfo, &xrSharedSystemId), "No (available) head mounted display found!");

	XrSystemProperties xrSystemProperties = { XR_TYPE_SYSTEM_PROPERTIES };
	checkXRResult(xrGetSystemProperties(xrSharedInstance, xrSharedSystemId, &xrSystemProperties), "Couldn't get system properties of the given VR headset!");
	xrMaxSwapchainWidth = xrSystemProperties.graphicsProperties.maxSwapchainImageWidth;
	xrMaxSwapchainHeight = xrSystemProperties.graphicsProperties.maxSwapchainImageHeight;
	xrSupportsOrientational = xrSystemProperties.trackingProperties.orientationTracking;
	xrSupportsPositional = xrSystemProperties.trackingProperties.positionTracking;
	
	
	auto xrViewConf = XR_CreateViewConfiguration();

	// Print configuration used, mostly for debugging purposes
	logPrint("Acquired system to be used:");
	logPrint(std::string(" - System Name: ") + xrSystemProperties.systemName);
	logPrint(std::string(" - Supports Orientation Tracking: ") + (xrSystemProperties.trackingProperties.orientationTracking ? "Yes" : "No"));
	logPrint(std::string(" - Supports Positional Tracking: ") + (xrSystemProperties.trackingProperties.positionTracking ? "Yes" : "No"));
	logPrint(std::string(" - Supports Max Swapchain Width: ") + std::to_string(xrSystemProperties.graphicsProperties.maxSwapchainImageWidth));
	logPrint(std::string(" - Supports Max Swapchain Height: ") + std::to_string(xrSystemProperties.graphicsProperties.maxSwapchainImageHeight));
	logPrint(std::format(" - [Left] Max View Resolution: w={}, h={} with {} samples", xrViewConf[0].maxImageRectWidth, xrViewConf[0].maxImageRectHeight, xrViewConf[0].maxSwapchainSampleCount));
	logPrint(std::format(" - [Right] Max View Resolution: w={}, h={}  with {} samples", xrViewConf[1].maxImageRectWidth, xrViewConf[1].maxImageRectHeight, xrViewConf[1].maxSwapchainSampleCount));
	logPrint(std::format(" - [Left] Recommended View Resolution: w={}, h={}  with {} samples", xrViewConf[0].recommendedImageRectWidth, xrViewConf[0].recommendedImageRectHeight, xrViewConf[0].recommendedSwapchainSampleCount));
	logPrint(std::format(" - [Right] Recommended View Resolution: w={}, h={}  with {} samples", xrViewConf[1].recommendedImageRectWidth, xrViewConf[1].recommendedImageRectHeight, xrViewConf[0].recommendedSwapchainSampleCount));
}

void XR_deinitInstance() {
	if (xrSharedInstance == XR_NULL_HANDLE)
		return;
	checkXRResult(xrDestroyInstance(xrSharedInstance), "Couldn't destroy xr instance!");
	if (xrDebugMessengerHandle != XR_NULL_HANDLE) {
		checkXRResult(func_xrDestroyDebugUtilsMessengerEXT(xrDebugMessengerHandle), "Couldn't destroy debug messenger!");
		xrDebugMessengerHandle = XR_NULL_HANDLE;
	}
	XR_deinitPointers();
	xrSharedInstance = XR_NULL_HANDLE;
	xrSharedSystemId = XR_NULL_SYSTEM_ID;
	xrMaxSwapchainWidth = 0;
	xrMaxSwapchainHeight = 0;
	xrSupportsOrientational = false;
	xrSupportsPositional = false;
}


void XR_GetSupportedVulkanVersions(XrVersion* minVulkanVersion, XrVersion* maxVulkanVersion) {
	logPrint("Acquiring supported Vulkan versions...");
	if (xrSharedInstance == XR_NULL_HANDLE)
		XR_initInstance();

	XrGraphicsRequirementsVulkan2KHR xrGraphicsRequirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR };
	checkXRResult(func_xrGetVulkanGraphicsRequirements2KHR(xrSharedInstance, xrSharedSystemId, &xrGraphicsRequirements), "Couldn't get Vulkan requirements for the given VR headset!");
	logPrint(std::format("OpenXR requires Vulkan versions v{:d}.{:d}.{:d} to v{:d}.{:d}.{:d}", XR_VERSION_MAJOR(xrGraphicsRequirements.minApiVersionSupported), XR_VERSION_MINOR(xrGraphicsRequirements.minApiVersionSupported), XR_VERSION_PATCH(xrGraphicsRequirements.minApiVersionSupported), XR_VERSION_MAJOR(xrGraphicsRequirements.maxApiVersionSupported), XR_VERSION_MINOR(xrGraphicsRequirements.maxApiVersionSupported), XR_VERSION_PATCH(xrGraphicsRequirements.maxApiVersionSupported)));

	*minVulkanVersion = xrGraphicsRequirements.minApiVersionSupported;
	*maxVulkanVersion = xrGraphicsRequirements.maxApiVersionSupported;
	
	// todo: if errors are thrown, could try deinitializing the instance here since we've got the necessary information out of it
	//if (xrInstanceHandle != XR_NULL_HANDLE)
	//	XR_deinitInstance();
}

VkResult XR_CreateCompatibleVulkanInstance(PFN_vkGetInstanceProcAddr getInstanceProcAddr, const VkInstanceCreateInfo* createInfo, const VkAllocationCallbacks* allocator, VkInstance* vkInstancePtr) {
	logPrint("Creating OpenXR-compatible Vulkan Instance...");
	if (xrSharedInstance == XR_NULL_HANDLE)
		XR_initInstance();

	XrVersion minVersion = 0;
	XrVersion maxVersion = 0;
	XR_GetSupportedVulkanVersions(&minVersion, &maxVersion);

	const_cast<VkApplicationInfo*>(const_cast<VkInstanceCreateInfo*>(createInfo)->pApplicationInfo)->apiVersion = VK_API_VERSION_1_1;

	VkResult result = VK_SUCCESS;

	XrVulkanInstanceCreateInfoKHR vkCreateInstanceInfo = { XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR };
	vkCreateInstanceInfo.systemId = xrSharedSystemId;
	vkCreateInstanceInfo.createFlags = 0;
	vkCreateInstanceInfo.pfnGetInstanceProcAddr = getInstanceProcAddr;
	vkCreateInstanceInfo.vulkanCreateInfo = createInfo;
	vkCreateInstanceInfo.vulkanAllocator = NULL;
	checkXRResult(func_xrCreateVulkanInstanceKHR(xrSharedInstance, &vkCreateInstanceInfo, vkInstancePtr, &result), "Couldn't create Vulkan instance using OpenXR wrapper!");

	return result;
}

VkPhysicalDevice XR_GetPhysicalDevice(VkInstance vkInstance) {
	logPrint("Acquiring physical device...");
	if (xrSharedInstance == XR_NULL_HANDLE)
		XR_initInstance();

	VkPhysicalDevice vkPhysicalDevice = VK_NULL_HANDLE;

	XrVulkanGraphicsDeviceGetInfoKHR xrVulkanDeviceInfo = { XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };
	xrVulkanDeviceInfo.systemId = xrSharedSystemId;
	xrVulkanDeviceInfo.vulkanInstance = vkInstance;
	checkXRResult(func_xrGetVulkanGraphicsDevice2KHR(xrSharedInstance, &xrVulkanDeviceInfo, &vkPhysicalDevice), "Couldn't get Vulkan Physical Device from OpenXR!");

	return vkPhysicalDevice;
}

VkResult XR_CreateCompatibleVulkanDevice(PFN_vkGetInstanceProcAddr getInstanceProcAddr, VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* createInfo, const VkAllocationCallbacks* allocator, VkDevice* vkDevicePtr) {
	logPrint("Creating OpenXR-compatible Vulkan device...");
	if (xrSharedInstance == XR_NULL_HANDLE)
		XR_initInstance();

	VkResult result = VK_SUCCESS;

	XrVulkanDeviceCreateInfoKHR vkCreateDeviceInfo = { XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR };
	vkCreateDeviceInfo.systemId = xrSharedSystemId;
	vkCreateDeviceInfo.createFlags = 0;
	vkCreateDeviceInfo.pfnGetInstanceProcAddr = getInstanceProcAddr;
	vkCreateDeviceInfo.vulkanPhysicalDevice = physicalDevice;
	vkCreateDeviceInfo.vulkanCreateInfo = createInfo;
	vkCreateDeviceInfo.vulkanAllocator = NULL;
	XrResult createDeviceResult = func_xrCreateVulkanDeviceKHR(xrSharedInstance, &vkCreateDeviceInfo, vkDevicePtr, &result);
	if (XR_FAILED(createDeviceResult)) {
		std::string errStr = "During Vulkan device creation, Vulkan threw error "+std::to_string(result)+"!";
		checkXRResult(createDeviceResult, errStr.c_str());
	}

	return result;
}

std::array<XrViewConfigurationView, 2> XR_CreateViewConfiguration() {
	logPrint("Creating the OpenXR view configuration...");

	XrViewConfigurationProperties stereoViewConfiguration = { XR_TYPE_VIEW_CONFIGURATION_PROPERTIES };
	checkXRResult(xrGetViewConfigurationProperties(xrSharedInstance, xrSharedSystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, &stereoViewConfiguration), "There's no VR headset available that allows stereo rendering!");
	xrSupportsFovMutable = stereoViewConfiguration.fovMutable;

	uint32_t eyeViewsConfigurationCount = 0;
	checkXRResult(xrEnumerateViewConfigurationViews(xrSharedInstance, xrSharedSystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &eyeViewsConfigurationCount, nullptr), "Can't get number of individual views for stereo view available");
	if (eyeViewsConfigurationCount != 2) {
		throw std::runtime_error(std::format("Unexpected amount of views for the stereo configuration? This is not supported! {}", eyeViewsConfigurationCount));
	}
	std::array<XrViewConfigurationView, 2> xrViewConf = { XrViewConfigurationView{ XR_TYPE_VIEW_CONFIGURATION_VIEW } , XrViewConfigurationView{ XR_TYPE_VIEW_CONFIGURATION_VIEW } };
	checkXRResult(xrEnumerateViewConfigurationViews(xrSharedInstance, xrSharedSystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eyeViewsConfigurationCount, &eyeViewsConfigurationCount, xrViewConf.data()), "Can't get individual views for stereo view available!");
	logPrint("Successfully created the OpenXR view configuration!");

	return xrViewConf;
}

XrSession XR_CreateSession(VkInstance vkInstance, VkDevice vkDevice, VkPhysicalDevice vkPhysicalDevice) {
	logPrint("Creating the OpenXR session...");
	
	XrGraphicsBindingVulkan2KHR xrVulkanBindings = { XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR };
	xrVulkanBindings.instance = topVkInstance;
	xrVulkanBindings.device = vkDevice;
	xrVulkanBindings.physicalDevice = vkPhysicalDevice;
	xrVulkanBindings.queueFamilyIndex = 0;
	xrVulkanBindings.queueIndex = 0;

	XrSessionCreateInfo sessionCreateInfo = { XR_TYPE_SESSION_CREATE_INFO };
	sessionCreateInfo.systemId = xrSharedSystemId;
	sessionCreateInfo.next = &xrVulkanBindings;
	sessionCreateInfo.createFlags = 0;

	checkXRResult(xrCreateSession(xrSharedInstance, &sessionCreateInfo, &xrSharedSession), "Failed to create Vulkan-based OpenXR session!");
	
	logPrint("Successfully created the OpenXR session!");
	return xrSharedSession;
}