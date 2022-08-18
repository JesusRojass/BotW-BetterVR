#include "layer.h"

#include <vulkan/vk_enum_string_helper.h>

// Variables
XrSession xrSessionHandle = XR_NULL_HANDLE;
std::array<XrSwapchain, 2> xrSwapchains = { VK_NULL_HANDLE, VK_NULL_HANDLE };
std::array<XrViewConfigurationView, 2> xrViewConfs;

// Functions

XrSwapchain RND_CreateSwapchain(XrSession xrSession, XrViewConfigurationView& viewConf) {
	logPrint("Creating OpenXR swapchain...");
	
	// Finds the first matching VkFormat (uint32) that matches the int64 from OpenXR
	auto getBestSwapchainFormat = [](const std::vector<int64_t>& runtimePreferredFormats, const std::vector<VkFormat>& applicationSupportedFormats) -> VkFormat {
		auto found = std::find_first_of(std::begin(runtimePreferredFormats), std::end(runtimePreferredFormats), std::begin(applicationSupportedFormats), std::end(applicationSupportedFormats));
		if (found == std::end(runtimePreferredFormats)) {
			throw std::runtime_error("OpenXR runtime doesn't support any of the presenting modes that the GPU drivers support.");
		}
		return (VkFormat)*found;
	};

	uint32_t swapchainCount = 0;
	xrEnumerateSwapchainFormats(xrSharedSession, 0, &swapchainCount, NULL);
	std::vector<int64_t> xrSwapchainFormats(swapchainCount);
	xrEnumerateSwapchainFormats(xrSharedSession, swapchainCount, &swapchainCount, (int64_t*)xrSwapchainFormats.data());

	logPrint("OpenXR supported swapchain formats:");
	for (uint32_t i=0; i<swapchainCount; i++) {
		logPrint(std::format(" - {:08x} = {}", (int64_t)xrSwapchainFormats[i], string_VkFormat((VkFormat)xrSwapchainFormats[i])));
	}

	std::vector<VkFormat> preferredColorFormats = {
		VK_FORMAT_B8G8R8A8_SRGB // Currently the framebuffer that gets caught is using VK_FORMAT_A2B10G10R10_UNORM_PACK32
	};

	VkFormat xrSwapchainFormat = getBestSwapchainFormat(xrSwapchainFormats, preferredColorFormats);
	logPrint(std::format("Picked {} as the texture format for swapchain", string_VkFormat(xrSwapchainFormat)));

	XrSwapchainCreateInfo swapchainCreateInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
	swapchainCreateInfo.width = viewConf.recommendedImageRectWidth;
	swapchainCreateInfo.height = viewConf.recommendedImageRectHeight;
	swapchainCreateInfo.arraySize = 1;
	swapchainCreateInfo.sampleCount = viewConf.recommendedSwapchainSampleCount;
	swapchainCreateInfo.format = xrSwapchainFormat;
	swapchainCreateInfo.mipCount = 1;
	swapchainCreateInfo.faceCount = 1;
	swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainCreateInfo.createFlags = 0;

	XrSwapchain retSwapchain = VK_NULL_HANDLE;
	checkXRResult(xrCreateSwapchain(xrSessionHandle, &swapchainCreateInfo, &retSwapchain), "Failed to create OpenXR swapchain images!");

	logPrint("Created OpenXR swapchain...");
	return retSwapchain;
}

void RND_InitRendering() {
	xrSessionHandle = XR_CreateSession(vkSharedInstance, vkSharedDevice, vkSharedPhysicalDevice);
	xrViewConfs = XR_CreateViewConfiguration();
	xrSwapchains[0] = RND_CreateSwapchain(xrSessionHandle, xrViewConfs[0]);
	xrSwapchains[1] = RND_CreateSwapchain(xrSessionHandle, xrViewConfs[1]);
}

void RND_SetupTexture() {
}

// Track frame rendering

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) {
	VkResult result;
	{
		scoped_lock l(global_lock);
		result = device_dispatch[GetKey(queue)].QueueSubmit(queue, submitCount, pSubmits, fence);
	}
	return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL Layer_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
	scoped_lock l(global_lock);
	return device_dispatch[GetKey(queue)].QueuePresentKHR(queue, pPresentInfo);
}