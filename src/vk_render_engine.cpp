#include <SDL.h>
#include <SDL_vulkan.h>

#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>

#include "vk_initializers.h"
#include "vk_utils.h"
#include "vk_scene.h"
#include "vk_textures.h"

#include "VkBootstrap.h"

#include <array>

VkPhysicalDevice RenderEngine::_physicalDevice = VK_NULL_HANDLE;
VkDevice RenderEngine::_device = VK_NULL_HANDLE;
VkQueue RenderEngine::_graphicsQueue = VK_NULL_HANDLE;
VkSampler RenderEngine::_defaultSampler = VK_NULL_HANDLE;
DeletionQueue RenderEngine::_mainDeletionQueue{};
VmaAllocator RenderEngine::_allocator = nullptr;
UploadContext RenderEngine::_uploadContext;
VkDescriptorPool RenderEngine::_descriptorPool = VK_NULL_HANDLE;
VkDescriptorSetLayout RenderEngine::_materialsSetLayout = VK_NULL_HANDLE;

std::vector<const char*> required_device_extensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
		VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
		VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME,

		// VkRay
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
		//
		//// Required by VK_KHR_acceleration_structure
		VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,

		// Required by VK_KHR_raytracing_pipeline
		VK_KHR_SPIRV_1_4_EXTENSION_NAME,

		// Required by VK_KHR_spirv_1_4
		VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME
};

RenderEngine::RenderEngine()
{
}

void RenderEngine::init()
{
	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

	_window = SDL_CreateWindow(
		"Vulkan Engine",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width,
		_windowExtent.height,
		window_flags
	);

	init_vulkan();
	init_swapchain();
	init_command_pools();
	init_sync_structures();

	// Single Texture Set Layout
	VkDescriptorSetLayoutBinding textureBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);

	VkDescriptorSetLayoutCreateInfo set3Info = {};
	set3Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set3Info.pNext = nullptr;
	set3Info.bindingCount = 1;
	set3Info.flags = 0;
	set3Info.pBindings = &textureBind;

	vkCreateDescriptorSetLayout(_device, &set3Info, nullptr, &_singleTextureSetLayout);

	init_raster_structures();
	init_raytracing_structures();

	_isInitialized = true;
}

void RenderEngine::init_vulkan()
{
	vkb::InstanceBuilder builder;

	//make the Vulkan instance, with basic debug features
	auto inst_ret = builder.set_app_name("Vulkan Engine")
		.request_validation_layers(true)
		.require_api_version(1, 2, 0)
		.use_default_debug_messenger()
		.enable_extension("VK_KHR_get_physical_device_properties2")
		.build();

	vkb::Instance vkb_inst = inst_ret.value();

	//store the instance
	_instance = vkb_inst.instance;
	//store the debug messenger
	_debug_messenger = vkb_inst.debug_messenger;


	// get the surface of the window we opened with SDL
	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

	//use vkbootstrap to select a gpu
	//We want a gpu that can write to the SDL surface and supports Vulkan 1.1
	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 1)
		.set_surface(_surface)
		.add_required_extensions(required_device_extensions)
		.select()
		.value();

	get_enabled_features();

	//create the final Vulkan device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };

	vkb::Device vkbDevice = deviceBuilder.add_pNext(deviceCreatepNextChain).build().value();

	// Get the VkDevice handle used in the rest of a Vulkan application
	_device = vkbDevice.device;
	_physicalDevice = physicalDevice.physical_device;

	//get graphics Queue
	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _physicalDevice;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	vmaCreateAllocator(&allocatorInfo, &_allocator);

	vkGetPhysicalDeviceProperties(_physicalDevice, &_gpuProperties);

	std::cout << "The GPU has a minimum buffer alignment of" << _gpuProperties.limits.minUniformBufferOffsetAlignment << std::endl;
}

void RenderEngine::init_swapchain()
{
	vkb::SwapchainBuilder swapchainBuilder{ _physicalDevice, _device, _surface };

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.use_default_format_selection()
		//use vsync present mode
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(_windowExtent.width, _windowExtent.height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build()
		.value();

	//store swapchain and its related images
	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();

	_swapchainImageFormat = vkbSwapchain.image_format;

	//Create Sampler
	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info(VK_FILTER_NEAREST);

	vkCreateSampler(_device, &samplerInfo, nullptr, &_defaultSampler);

	_mainDeletionQueue.push_function([=]() {
		vkDestroySampler(_device, _defaultSampler, nullptr);
		vkDestroySwapchainKHR(_device, _swapchain, nullptr);
		});
}

// upload info command pool
void RenderEngine::init_command_pools()
{
	// Upload Context
	VkCommandPoolCreateInfo uploadCommandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily);

	VK_CHECK(vkCreateCommandPool(_device, &uploadCommandPoolInfo, nullptr, &_uploadContext._commandPool));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(_device, _uploadContext._commandPool, nullptr);
		});
}

void RenderEngine::init_sync_structures()
{
	VkFenceCreateInfo uploadFenceCreateInfo = {};
	uploadFenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	uploadFenceCreateInfo.pNext = nullptr;
	uploadFenceCreateInfo.flags = 0;

	VK_CHECK(vkCreateFence(_device, &uploadFenceCreateInfo, nullptr, &_uploadContext._uploadFence));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyFence(_device, _uploadContext._uploadFence, nullptr);
		});
}

#pragma region RASTER
void RenderEngine::init_descriptor_set_pool()
{
	std::vector<VkDescriptorPoolSize> sizes = {
	{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
	{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10},
	{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
	{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 50},
	{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
	{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10}
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = 0;
	pool_info.maxSets = 10;
	pool_info.poolSizeCount = static_cast<uint32_t>(sizes.size());
	pool_info.pPoolSizes = sizes.data();

	vkCreateDescriptorPool(_device, &pool_info, nullptr, &_descriptorPool);

	_mainDeletionQueue.push_function([=]() {
		vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
		});
}

void RenderEngine::init_descriptor_set_layouts()
{
	VkDescriptorSetLayoutBinding cameraBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);

	VkDescriptorSetLayoutBinding sceneBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1);

	std::array<VkDescriptorSetLayoutBinding, 2> bindings = { cameraBind, sceneBind };

	VkDescriptorSetLayoutCreateInfo setInfo = {};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.pNext = nullptr;

	setInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	setInfo.flags = 0;
	setInfo.pBindings = bindings.data();

	vkCreateDescriptorSetLayout(_device, &setInfo, nullptr, &_globalSetLayout);

	VkDescriptorSetLayoutCreateInfo camSetInfo = {};
	camSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	camSetInfo.pNext = nullptr;

	camSetInfo.bindingCount = 1;
	camSetInfo.flags = 0;
	camSetInfo.pBindings = &cameraBind;

	vkCreateDescriptorSetLayout(_device, &camSetInfo, nullptr, &_camSetLayout);

	VkDescriptorSetLayoutBinding objectBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);

	VkDescriptorSetLayoutCreateInfo set2Info = {};
	set2Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set2Info.pNext = nullptr;
	set2Info.bindingCount = 1;
	set2Info.flags = 0;
	set2Info.pBindings = &objectBind;

	vkCreateDescriptorSetLayout(_device, &set2Info, nullptr, &_objectSetLayout);

	VkDescriptorSetLayoutBinding materialsBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
	VkDescriptorSetLayoutBinding matTexturesBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1);
	matTexturesBind.descriptorCount = VKE::Texture::sTexturesLoaded.size();

	std::array<VkDescriptorSetLayoutBinding, 2> matBindings = { materialsBind, matTexturesBind };

	VkDescriptorSetLayoutCreateInfo materialsSetInfo = {};
	materialsSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	materialsSetInfo.pNext = nullptr;
	materialsSetInfo.bindingCount = static_cast<uint32_t>(matBindings.size());
	materialsSetInfo.flags = 0;
	materialsSetInfo.pBindings = matBindings.data();

	vkCreateDescriptorSetLayout(_device, &materialsSetInfo, nullptr, &_materialsSetLayout);
}

void RenderEngine::init_raster_structures()
{
	init_descriptor_set_pool();
	init_depth_buffer();
	init_deferred_attachments();
	init_render_passes();
	init_framebuffers();
	init_gbuffer_descriptors();
	
}

void RenderEngine::init_depth_buffer()
{
	VkExtent3D depthImageExtent = {
		_windowExtent.width,
		_windowExtent.height,
		1
	};

	_depthImage._format = VK_FORMAT_D32_SFLOAT;

	VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthImage._format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);

	VmaAllocationCreateInfo dimg_allocinfo = {};
	dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	dimg_allocinfo.requiredFlags = VkMemoryPropertyFlagBits(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(_allocator, &dimg_info, &dimg_allocinfo, &_depthImage._image, &_depthImage._allocation, nullptr);

	VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthImage._format, _depthImage._image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImage._view));
}

void RenderEngine::init_deferred_attachments()
{
	VkExtent3D attachmentExtent{
		_windowExtent.width,
		_windowExtent.height,
		1
	};

	_positionImage._format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_normalImage._format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_albedoImage._format = VK_FORMAT_R8G8B8A8_UNORM;

	VkImageCreateInfo position_igm = vkinit::image_create_info(_positionImage._format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, attachmentExtent);
	VkImageCreateInfo normal_igm = vkinit::image_create_info(_normalImage._format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, attachmentExtent);
	VkImageCreateInfo albedo_igm = vkinit::image_create_info(_albedoImage._format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, attachmentExtent);

	VmaAllocationCreateInfo img_alloc_info = {};
	img_alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	img_alloc_info.requiredFlags = VkMemoryPropertyFlagBits(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(_allocator, &position_igm, &img_alloc_info, &_positionImage._image, &_positionImage._allocation, nullptr);
	vmaCreateImage(_allocator, &normal_igm, &img_alloc_info, &_normalImage._image, &_normalImage._allocation, nullptr);
	vmaCreateImage(_allocator, &albedo_igm, &img_alloc_info, &_albedoImage._image, &_albedoImage._allocation, nullptr);

	VkImageViewCreateInfo position_view_igm = vkinit::imageview_create_info(VK_FORMAT_R16G16B16A16_SFLOAT, _positionImage._image, VK_IMAGE_ASPECT_COLOR_BIT);
	VkImageViewCreateInfo normal_view_igm = vkinit::imageview_create_info(VK_FORMAT_R16G16B16A16_SFLOAT, _normalImage._image, VK_IMAGE_ASPECT_COLOR_BIT);
	VkImageViewCreateInfo albedo_view_igm = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, _albedoImage._image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(_device, &position_view_igm, nullptr, &_positionImage._view));
	VK_CHECK(vkCreateImageView(_device, &normal_view_igm, nullptr, &_normalImage._view));
	VK_CHECK(vkCreateImageView(_device, &albedo_view_igm, nullptr, &_albedoImage._view));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(_device, _positionImage._view, nullptr);
		vmaDestroyImage(_allocator, _positionImage._image, _positionImage._allocation);
		vkDestroyImageView(_device, _normalImage._view, nullptr);
		vmaDestroyImage(_allocator, _normalImage._image, _normalImage._allocation);
		vkDestroyImageView(_device, _albedoImage._view, nullptr);
		vmaDestroyImage(_allocator, _albedoImage._image, _albedoImage._allocation);
		});
}

void RenderEngine::init_render_passes()
{
	{
		//DEFAULT RENDER PASS
		VkAttachmentDescription color_attachment = {};
		color_attachment.format = _swapchainImageFormat;
		color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference color_attachment_ref = {};
		color_attachment_ref.attachment = 0;
		color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentDescription depth_attachment = {};
		depth_attachment.flags = 0;
		depth_attachment.format = _depthImage._format;
		depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depth_attachment_ref = {};
		depth_attachment_ref.attachment = 1;
		depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &color_attachment_ref;
		subpass.pDepthStencilAttachment = &depth_attachment_ref;

		VkAttachmentDescription attachments[2] = { color_attachment, depth_attachment };

		VkRenderPassCreateInfo render_pass_info = {};
		render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		render_pass_info.attachmentCount = 2;
		render_pass_info.pAttachments = &attachments[0];
		render_pass_info.subpassCount = 1;
		render_pass_info.pSubpasses = &subpass;

		VK_CHECK(vkCreateRenderPass(_device, &render_pass_info, nullptr, &_defaultRenderPass));

		_mainDeletionQueue.push_function([=]() {
			vkDestroyRenderPass(_device, _defaultRenderPass, nullptr);
			});
	}

	{
		//DEFERRED RENDER PASS
		//gBuffers Pass
		VkAttachmentDescription position_attachment = {};
		position_attachment.format = _positionImage._format;
		position_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		position_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		position_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		position_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		position_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		position_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		position_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkAttachmentDescription normal_attachment = {};
		normal_attachment.format = _normalImage._format;
		normal_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		normal_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		normal_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		normal_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		normal_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		normal_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		normal_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkAttachmentDescription albedo_attachment = {};
		albedo_attachment.format = _albedoImage._format;
		albedo_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		albedo_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		albedo_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		albedo_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		albedo_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		albedo_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		albedo_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkAttachmentDescription depth_attachment = {};
		depth_attachment.format = _depthImage._format;
		depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		std::array<VkAttachmentDescription, 4> attachment_descriptions = { position_attachment, normal_attachment, albedo_attachment, depth_attachment };

		VkAttachmentReference position_ref;
		position_ref.attachment = 0;
		position_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference normal_ref;
		normal_ref.attachment = 1;
		normal_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference albedo_ref;
		albedo_ref.attachment = 2;
		albedo_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		std::array<VkAttachmentReference, 3> color_references = { position_ref, normal_ref, albedo_ref };

		VkAttachmentReference depth_ref;
		depth_ref.attachment = 3;
		depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = static_cast<uint32_t>(color_references.size());
		subpass.pColorAttachments = color_references.data();
		subpass.pDepthStencilAttachment = &depth_ref;

		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo deferred_pass = {};
		deferred_pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		deferred_pass.pNext = nullptr;
		deferred_pass.attachmentCount = static_cast<uint32_t>(attachment_descriptions.size());
		deferred_pass.pAttachments = attachment_descriptions.data();
		deferred_pass.subpassCount = 1;
		deferred_pass.pSubpasses = &subpass;
		deferred_pass.dependencyCount = static_cast<uint32_t>(dependencies.size());
		deferred_pass.pDependencies = dependencies.data();

		VK_CHECK(vkCreateRenderPass(_device, &deferred_pass, nullptr, &_deferredRenderPass));

		_mainDeletionQueue.push_function([=]() {
			vkDestroyRenderPass(_device, _deferredRenderPass, nullptr);
			});
	}
}

void RenderEngine::init_framebuffers()
{
	//OFFSCREEN FRAMEBUFFER
	std::array<VkImageView, 4> attachments;
	attachments[0] = _positionImage._view;
	attachments[1] = _normalImage._view;
	attachments[2] = _albedoImage._view;
	attachments[3] = _depthImage._view;

	VkFramebufferCreateInfo fb_info = {};
	fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb_info.pNext = nullptr;
	fb_info.renderPass = _deferredRenderPass;
	fb_info.attachmentCount = static_cast<uint32_t>(attachments.size());
	fb_info.pAttachments = attachments.data();
	fb_info.width = _windowExtent.width;
	fb_info.height = _windowExtent.height;
	fb_info.layers = 1;

	VK_CHECK(vkCreateFramebuffer(_device, &fb_info, nullptr, &_offscreen_framebuffer));

	//SWAPCHAIN FRAMEBUFFERS

	VkFramebufferCreateInfo sc_fb_info = {};
	sc_fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	sc_fb_info.pNext = nullptr;

	sc_fb_info.renderPass = _defaultRenderPass;
	sc_fb_info.attachmentCount = 1;
	sc_fb_info.width = _windowExtent.width;
	sc_fb_info.height = _windowExtent.height;
	sc_fb_info.layers = 1;

	const uint32_t swapchain_imagecount = _swapchainImages.size();
	_framebuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

	for (int i = 0; i < swapchain_imagecount; i++)
	{
		std::array<VkImageView, 2> attachments = { _swapchainImageViews[i], _depthImage._view };

		sc_fb_info.attachmentCount = static_cast<uint32_t>(attachments.size());
		sc_fb_info.pAttachments = attachments.data();

		VK_CHECK(vkCreateFramebuffer(_device, &sc_fb_info, nullptr, &_framebuffers[i]));

		_mainDeletionQueue.push_function([=]() {
			vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
			});
	}
}

void RenderEngine::init_pipelines()
{
	//DEFERRED PIPELINES
	{
		//SHADERS LOADING

		VkShaderModule deferredVertex;
		if (!vkutil::load_shader_module(_device, "../shaders/deferred.vert.spv", &deferredVertex))
		{
			std::cout << "Error when building the deferred vertex shader" << std::endl;
		}
		else
		{
			std::cout << "Deferred vertex shader succesfully loaded" << endl;
		}

		VkShaderModule deferredFrag;
		if (!vkutil::load_shader_module(_device, "../shaders/deferred.frag.spv", &deferredFrag))
		{
			std::cout << "Error when building the deferred frag shader" << std::endl;
		}
		else
		{
			std::cout << "Frag vertex shader succesfully loaded" << endl;
		}

		VkShaderModule lightVertex;
		if (!vkutil::load_shader_module(_device, "../shaders/light.vert.spv", &lightVertex))
		{
			std::cout << "Error when building the light vertex shader" << std::endl;
		}
		else
		{
			std::cout << "Light vertex shader succesfully loaded" << endl;
		}

		VkShaderModule lightFrag;
		if (!vkutil::load_shader_module(_device, "../shaders/light.frag.spv", &lightFrag))
		{
			std::cout << "Error when building the light frag shader" << std::endl;
		}
		else
		{
			std::cout << "Light frag shader succesfully loaded" << endl;
		}

		//LAYOUTS
		VkPipelineLayoutCreateInfo layoutInfo = vkinit::pipeline_layout_create_info();

		std::array<VkDescriptorSetLayout, 2> deferredSetLayouts = { _camSetLayout, _materialsSetLayout };

		VkPushConstantRange pushConstant = {};
		pushConstant.offset = 0;
		pushConstant.size = sizeof(GPUObjectData);
		pushConstant.stageFlags =  VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushConstant;
		layoutInfo.setLayoutCount = static_cast<uint32_t>(deferredSetLayouts.size());
		layoutInfo.pSetLayouts = deferredSetLayouts.data();

		VK_CHECK(vkCreatePipelineLayout(_device, &layoutInfo, nullptr, &_deferredPipelineLayout));

		std::array<VkDescriptorSetLayout, 2> lightSetLayouts = { _globalSetLayout, _gbuffersSetLayout };

		layoutInfo.setLayoutCount = static_cast<uint32_t>(lightSetLayouts.size());
		layoutInfo.pSetLayouts = lightSetLayouts.data();

		VK_CHECK(vkCreatePipelineLayout(_device, &layoutInfo, nullptr, &_lightPipelineLayout));


		//DEFERRED PIPELINE CREATION
		PipelineBuilder pipelineBuilder;

		//Deferred Vertex Info
		pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

		VertexInputDescription vertexDescription = Vertex::get_vertex_description();
		pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexDescription.attributes.size());
		pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
		pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexDescription.bindings.size());
		pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();

		//Deferred Assembly Info
		pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

		//Viewport and Scissor
		pipelineBuilder._viewport.x = 0.0f;
		pipelineBuilder._viewport.y = 0.0f;
		pipelineBuilder._viewport.width = (float)_windowExtent.width;
		pipelineBuilder._viewport.height = (float)_windowExtent.height;
		pipelineBuilder._viewport.minDepth = 0.0f;
		pipelineBuilder._viewport.maxDepth = 1.0f;

		pipelineBuilder._scissor.offset = { 0, 0 };
		pipelineBuilder._scissor.extent = _windowExtent;

		//Deferred Depth Stencil
		pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

		//Deferred Rasterizer
		pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

		//Deferred Multisampling
		pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

		//Deferred Color Blend Attachment
		pipelineBuilder._colorBlendAttachment.push_back(vkinit::color_blend_attachment_state());
		pipelineBuilder._colorBlendAttachment.push_back(vkinit::color_blend_attachment_state());
		pipelineBuilder._colorBlendAttachment.push_back(vkinit::color_blend_attachment_state());

		//Deferred Shaders
		pipelineBuilder._shaderStages.push_back(
			vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, deferredVertex));
		pipelineBuilder._shaderStages.push_back(
			vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, deferredFrag));

		//Deferred Layout
		pipelineBuilder._pipelineLayout = _deferredPipelineLayout;

		_deferredPipeline = pipelineBuilder.build_pipeline(_device, _deferredRenderPass);

		//LIGHT PIPELINE CREATION

		//Light Depth Stencil
		pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(false, false, VK_COMPARE_OP_ALWAYS);

		//Deferred Color Blend Attachment
		pipelineBuilder._colorBlendAttachment.clear();
		pipelineBuilder._colorBlendAttachment.push_back(vkinit::color_blend_attachment_state());

		//Light Shaders
		pipelineBuilder._shaderStages.clear();
		pipelineBuilder._shaderStages.push_back(
			vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, lightVertex));
		pipelineBuilder._shaderStages.push_back(
			vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, lightFrag));

		//Light Layout
		pipelineBuilder._pipelineLayout = _lightPipelineLayout;

		_lightPipeline = pipelineBuilder.build_pipeline(_device, _defaultRenderPass);


		//DELETIONS

		vkDestroyShaderModule(_device, lightFrag, nullptr);
		vkDestroyShaderModule(_device, lightVertex, nullptr);
		vkDestroyShaderModule(_device, deferredFrag, nullptr);
		vkDestroyShaderModule(_device, deferredVertex, nullptr);

		_mainDeletionQueue.push_function([=]() {
			vkDestroyPipeline(_device, _deferredPipeline, nullptr);
			vkDestroyPipeline(_device, _lightPipeline, nullptr);

			vkDestroyPipelineLayout(_device, _deferredPipelineLayout, nullptr);
			vkDestroyPipelineLayout(_device, _lightPipelineLayout, nullptr);
			});
	}
}

void RenderEngine::init_gbuffer_descriptors()
{
	VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 };

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = 0;
	pool_info.maxSets = 10;
	pool_info.poolSizeCount = 1;
	pool_info.pPoolSizes = &pool_size;

	vkCreateDescriptorPool(_device, &pool_info, nullptr, &_gbuffersPool);

	//gbuffers
	VkDescriptorSetLayoutBinding position_bind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
	VkDescriptorSetLayoutBinding normal_bind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1);
	VkDescriptorSetLayoutBinding albedo_bind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2);

	std::array<VkDescriptorSetLayoutBinding, 3> deferred_set_layouts = { position_bind, normal_bind, albedo_bind };

	VkDescriptorSetLayoutCreateInfo deferred_layout_info = {};
	deferred_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	deferred_layout_info.pNext = nullptr;
	deferred_layout_info.flags = 0;
	deferred_layout_info.bindingCount = static_cast<uint32_t>(deferred_set_layouts.size());
	deferred_layout_info.pBindings = deferred_set_layouts.data();

	VK_CHECK(vkCreateDescriptorSetLayout(_device, &deferred_layout_info, nullptr, &_gbuffersSetLayout));

	VkDescriptorSetAllocateInfo deferred_set_alloc = {};
	deferred_set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	deferred_set_alloc.pNext = nullptr;
	deferred_set_alloc.descriptorPool = _gbuffersPool;
	deferred_set_alloc.descriptorSetCount = 1;
	deferred_set_alloc.pSetLayouts = &_gbuffersSetLayout;

	vkAllocateDescriptorSets(_device, &deferred_set_alloc, &_gbuffersDescriptorSet);

	VkDescriptorImageInfo position_descriptor_image;
	position_descriptor_image.sampler = _defaultSampler;
	position_descriptor_image.imageView = _positionImage._view;
	position_descriptor_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorImageInfo normal_descriptor_image;
	normal_descriptor_image.sampler = _defaultSampler;
	normal_descriptor_image.imageView = _normalImage._view;
	normal_descriptor_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorImageInfo albedo_descriptor_image;
	albedo_descriptor_image.sampler = _defaultSampler;
	albedo_descriptor_image.imageView = _albedoImage._view;
	albedo_descriptor_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet position_texture = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _gbuffersDescriptorSet, &position_descriptor_image, 0);
	VkWriteDescriptorSet normal_texture = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _gbuffersDescriptorSet, &normal_descriptor_image, 1);
	VkWriteDescriptorSet albedo_texture = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _gbuffersDescriptorSet, &albedo_descriptor_image, 2);

	std::array<VkWriteDescriptorSet, 3> setWrites = { position_texture, normal_texture, albedo_texture };

	vkUpdateDescriptorSets(_device, static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0, nullptr);
}
#pragma endregion


#pragma region RAYTRACING
void RenderEngine::init_raytracing_structures()
{
	// Get the ray tracing and accelertion structure related function pointers required by this sample
	vkGetBufferDeviceAddressKHR = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(vkGetDeviceProcAddr(_device, "vkGetBufferDeviceAddressKHR"));
	vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(_device, "vkCmdBuildAccelerationStructuresKHR"));
	vkBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(_device, "vkBuildAccelerationStructuresKHR"));
	vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(_device, "vkCreateAccelerationStructureKHR"));
	vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(_device, "vkDestroyAccelerationStructureKHR"));
	vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(_device, "vkGetAccelerationStructureBuildSizesKHR"));
	vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(_device, "vkGetAccelerationStructureDeviceAddressKHR"));
	vkCmdTraceRaysKHR = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(vkGetDeviceProcAddr(_device, "vkCmdTraceRaysKHR"));
	vkGetRayTracingShaderGroupHandlesKHR = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(vkGetDeviceProcAddr(_device, "vkGetRayTracingShaderGroupHandlesKHR"));
	vkCreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(vkGetDeviceProcAddr(_device, "vkCreateRayTracingPipelinesKHR"));

	_rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
	VkPhysicalDeviceProperties2 deviceProperties2{};
	deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	deviceProperties2.pNext = &_rayTracingPipelineProperties;
	vkGetPhysicalDeviceProperties2(_physicalDevice, &deviceProperties2);

	// Requesting ray tracing features
	_accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	VkPhysicalDeviceFeatures2 deviceFeatures2{};
	deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	deviceFeatures2.pNext = &_accelerationStructureFeatures;
	vkGetPhysicalDeviceFeatures2(_physicalDevice, &deviceFeatures2);

	create_storage_image();

	create_raytracing_descriptor_pool();

	create_pospo_structures();
}

void RenderEngine::create_bottom_level_acceleration_structure(const Scene& scene)
{
	std::vector<BlasInput> allBlas;
	allBlas.reserve(scene._renderables.size());

	_bottomLevelAS.reserve(allBlas.size());
	_transformBuffers.reserve(allBlas.size());

	for (const auto& renderable : scene._renderables)
	{
		auto blas = renderable_to_vulkan_geometry(renderable);
		allBlas.push_back(blas);
	}

	build_blas(allBlas);
}

void RenderEngine::create_top_level_acceleration_structure(const Scene& scene, bool recreated)
{
	std::vector<VkAccelerationStructureInstanceKHR> instances;
	instances.resize(scene._renderables.size());

	for (int i = 0; i < scene._renderables.size(); i++)
	{
		glm::mat4 model = glm::transpose(scene._renderables[i]._model);

		VkTransformMatrixKHR transformMatrix = {
			model[0].x, model[0].y, model[0].z, model[0].w,
			model[1].x, model[1].y, model[1].z, model[1].w,
			model[2].x, model[2].y, model[2].z, model[2].w,
		};

		VkAccelerationStructureInstanceKHR instance{};
		instance.transform = transformMatrix;
		instance.instanceCustomIndex = i;
		instance.mask = 0xFF;
		instance.instanceShaderBindingTableRecordOffset = 0;
		instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		instance.accelerationStructureReference = _bottomLevelAS[i]._deviceAddress;

		instances[i] = instance;
	}

	// Buffer for instance data
	AllocatedBuffer instancesBuffer;
	instancesBuffer = vkutil::create_buffer(_allocator,
		sizeof(VkAccelerationStructureInstanceKHR) * instances.size(),
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VMA_MEMORY_USAGE_CPU_TO_GPU);

	void* data;
	vmaMapMemory(_allocator, instancesBuffer._allocation, &data);
	memcpy(data, instances.data(), sizeof(VkAccelerationStructureInstanceKHR) * instances.size());
	vmaUnmapMemory(_allocator, instancesBuffer._allocation);

	VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
	instanceDataDeviceAddress.deviceAddress = vkutil::get_buffer_device_address(_device, instancesBuffer._buffer);


	VkAccelerationStructureGeometryKHR accelerationStructureGeometry{};
	accelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	accelerationStructureGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	accelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
	accelerationStructureGeometry.geometry.instances.data = instanceDataDeviceAddress;

	// Get size info
	/*
	The pSrcAccelerationStructure, dstAccelerationStructure, and mode members of pBuildInfo are ignored. Any VkDeviceOrHostAddressKHR members of pBuildInfo are ignored by this command, except that the hostAddress member of VkAccelerationStructureGeometryTrianglesDataKHR::transformData will be examined to check if it is NULL.*
	*/
	VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
	accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	accelerationStructureBuildGeometryInfo.geometryCount = 1;
	accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

	uint32_t primitive_count = instances.size();

	VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
	accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	vkGetAccelerationStructureBuildSizesKHR(
		_device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&accelerationStructureBuildGeometryInfo,
		&primitive_count,
		&accelerationStructureBuildSizesInfo);

	if (!recreated)
	{
		create_acceleration_structure_buffer(_topLevelAS, accelerationStructureBuildSizesInfo);
	}

	VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
	accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	accelerationStructureCreateInfo.buffer = _topLevelAS._buffer;
	accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
	accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

	if (!recreated)
	{
		vkCreateAccelerationStructureKHR(_device, &accelerationStructureCreateInfo, nullptr, &_topLevelAS._handle);
	}

	// Create a small scratch buffer used during build of the top level acceleration structure
	RayTracingScratchBuffer scratchBuffer = create_scratch_buffer(accelerationStructureBuildSizesInfo.buildScratchSize);

	VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{};
	accelerationBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	accelerationBuildGeometryInfo.mode = recreated ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	accelerationBuildGeometryInfo.srcAccelerationStructure = recreated ? _topLevelAS._handle : VK_NULL_HANDLE;
	accelerationBuildGeometryInfo.dstAccelerationStructure = _topLevelAS._handle;
	accelerationBuildGeometryInfo.geometryCount = 1;
	accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
	accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer._deviceAddress;

	VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
	accelerationStructureBuildRangeInfo.primitiveCount = instances.size();
	accelerationStructureBuildRangeInfo.primitiveOffset = 0;
	accelerationStructureBuildRangeInfo.firstVertex = 0;
	accelerationStructureBuildRangeInfo.transformOffset = 0;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };

	if (_accelerationStructureFeatures.accelerationStructureHostCommands)
	{
		// Implementation supports building acceleration structure building on host
		vkBuildAccelerationStructuresKHR(
			_device,
			VK_NULL_HANDLE,
			1,
			&accelerationBuildGeometryInfo,
			accelerationBuildStructureRangeInfos.data());
	}
	else
	{
		// Acceleration structure needs to be build on the device
		vkupload::immediate_submit([&](VkCommandBuffer cmd)
			{
				vkCmdBuildAccelerationStructuresKHR(
					cmd,
					1,
					&accelerationBuildGeometryInfo,
					accelerationBuildStructureRangeInfos.data());
			});
	}

	VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
	accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	accelerationDeviceAddressInfo.accelerationStructure = _topLevelAS._handle;
	_topLevelAS._deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(_device, &accelerationDeviceAddressInfo);

	if (!recreated)
	{
		_mainDeletionQueue.push_function([=]() {
			vkDestroyAccelerationStructureKHR(_device, _topLevelAS._handle, nullptr);
			});
	}

	delete_scratch_buffer(scratchBuffer);

	vmaDestroyBuffer(_allocator, instancesBuffer._buffer, instancesBuffer._allocation);
}

void RenderEngine::create_storage_image()
{
	VkExtent3D extent =
	{
		_windowExtent.width,
		_windowExtent.height,
		1
	};

	VkImageCreateInfo image = vkinit::image_create_info(VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, extent);
	image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK(vkCreateImage(_device, &image, nullptr, &_storageImage));

	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements(_device, _storageImage, &memReqs);

	VkMemoryAllocateInfo memoryAllocateInfo = {};
	memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocateInfo.allocationSize = memReqs.size;
	memoryAllocateInfo.memoryTypeIndex = vkutil::find_memory_type_index(_physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VK_CHECK(vkAllocateMemory(_device, &memoryAllocateInfo, nullptr, &_storageImageMemory));
	VK_CHECK(vkBindImageMemory(_device, _storageImage, _storageImageMemory, 0));

	VkImageViewCreateInfo colorImageView = vkinit::imageview_create_info(VK_FORMAT_B8G8R8A8_UNORM, _storageImage, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(_device, &colorImageView, nullptr, &_storageImageView));

	vkupload::immediate_submit([&](VkCommandBuffer cmd) {
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = _storageImage;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		});
}

void RenderEngine::create_raytracing_pipeline(const int& renderablesCount)
{
	VkDescriptorSetLayoutBinding accelerationStructureLayoutBinding{};
	accelerationStructureLayoutBinding.binding = 0;
	accelerationStructureLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	accelerationStructureLayoutBinding.descriptorCount = 1;
	accelerationStructureLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

	VkDescriptorSetLayoutBinding resultImageLayoutBinding{};
	resultImageLayoutBinding.binding = 1;
	resultImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	resultImageLayoutBinding.descriptorCount = 1;
	resultImageLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

	VkDescriptorSetLayoutBinding uniformBufferBinding{};
	uniformBufferBinding.binding = 2;
	uniformBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uniformBufferBinding.descriptorCount = 1;
	uniformBufferBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

	VkDescriptorSetLayoutBinding vertexBufferBinding{};
	vertexBufferBinding.binding = 3;
	vertexBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	vertexBufferBinding.descriptorCount = renderablesCount;
	vertexBufferBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

	VkDescriptorSetLayoutBinding indexBufferBinding{};
	indexBufferBinding.binding = 4;
	indexBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	indexBufferBinding.descriptorCount = renderablesCount;
	indexBufferBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

	VkDescriptorSetLayoutBinding transformBufferBinding{};
	transformBufferBinding.binding = 5;
	transformBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	transformBufferBinding.descriptorCount = renderablesCount;
	transformBufferBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

	VkDescriptorSetLayoutBinding sceneBufferBinding{};
	sceneBufferBinding.binding = 6;
	sceneBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	sceneBufferBinding.descriptorCount = 1;
	sceneBufferBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

	VkDescriptorSetLayoutBinding materialBufferBinding{};
	materialBufferBinding.binding = 7;
	materialBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	materialBufferBinding.descriptorCount = 1;
	materialBufferBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

	VkDescriptorSetLayoutBinding materialIndexBufferBinding{};
	materialIndexBufferBinding.binding = 8;
	materialIndexBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	materialIndexBufferBinding.descriptorCount = 1;
	materialIndexBufferBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

	VkDescriptorSetLayoutBinding textureBufferBinding{};
	textureBufferBinding.binding = 9;
	textureBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	textureBufferBinding.descriptorCount = 2;
	textureBufferBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

	VkDescriptorSetLayoutBinding textureIndexBufferBinding{};
	textureIndexBufferBinding.binding = 10;
	textureIndexBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	textureIndexBufferBinding.descriptorCount = 1;
	textureIndexBufferBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

	std::vector<VkDescriptorSetLayoutBinding> bindings({
		accelerationStructureLayoutBinding,
		resultImageLayoutBinding,
		uniformBufferBinding,
		vertexBufferBinding,
		indexBufferBinding,
		transformBufferBinding,
		sceneBufferBinding,
		materialBufferBinding,
		materialIndexBufferBinding,
		textureBufferBinding,
		textureIndexBufferBinding
		});

	VkDescriptorSetLayoutCreateInfo desc_set_layout_info{};
	desc_set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	desc_set_layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
	desc_set_layout_info.pBindings = bindings.data();
	VK_CHECK(vkCreateDescriptorSetLayout(_device, &desc_set_layout_info, nullptr, &_rayTracingSetLayout));

	VkPipelineLayoutCreateInfo pipeline_layout_info{};
	pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &_rayTracingSetLayout;
	VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_rayTracingPipelineLayout));

	/*
		Setup ray tracing shader groups
	*/
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

	// Ray generation group
	{
		VkShaderModule raygenShader;
		if (!vkutil::load_shader_module(_device, "../shaders/raygen.rgen.spv", &raygenShader))
		{
			std::cout << "Error when building the ray generation shader module" << std::endl;
		}
		else {
			std::cout << "Ray generation shader succesfully loaded" << std::endl;
		}

		VkPipelineShaderStageCreateInfo shader_stage_info = vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_RAYGEN_BIT_KHR, raygenShader);

		shaderStages.push_back(shader_stage_info);
		VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
		shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		_shaderGroups.push_back(shaderGroup);
	}

	// Miss group
	{
		VkShaderModule missShader;
		if (!vkutil::load_shader_module(_device, "../shaders/miss.rmiss.spv", &missShader))
		{
			std::cout << "Error when building the miss shader module" << std::endl;
		}
		else {
			std::cout << "Miss shader succesfully loaded" << std::endl;
		}

		VkPipelineShaderStageCreateInfo shader_stage_info = vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_MISS_BIT_KHR, missShader);

		shaderStages.push_back(shader_stage_info);
		VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
		shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		_shaderGroups.push_back(shaderGroup);
	}

	//Shadow miss group
	{
		VkShaderModule shadowMissShader;
		if (!vkutil::load_shader_module(_device, "../shaders/raytraceShadow.rmiss.spv", &shadowMissShader))
		{
			std::cout << "Error when building the shadow miss shader module" << std::endl;
		}
		else {
			std::cout << "Shadw miss shader succesfully loaded" << std::endl;
		}
		VkPipelineShaderStageCreateInfo shader_stage_info = vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_MISS_BIT_KHR, shadowMissShader);

		shaderStages.push_back(shader_stage_info);
		VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
		shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		_shaderGroups.push_back(shaderGroup);
	}

	// Closest hit group
	{
		VkShaderModule closestHitShader;
		if (!vkutil::load_shader_module(_device, "../shaders/closestHit.rchit.spv", &closestHitShader))
		{
			std::cout << "Error when building the closest hit shader module" << std::endl;
		}
		else {
			std::cout << "Closest hit shader succesfully loaded" << std::endl;
		}

		VkPipelineShaderStageCreateInfo shader_stage_info = vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, closestHitShader);

		shaderStages.push_back(shader_stage_info);
		VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
		shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
		shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.closestHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		_shaderGroups.push_back(shaderGroup);
	}

	/*
		Create the ray tracing pipeline
	*/
	VkRayTracingPipelineCreateInfoKHR raytracing_pipeline_info{};
	raytracing_pipeline_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
	raytracing_pipeline_info.stageCount = static_cast<uint32_t>(shaderStages.size());
	raytracing_pipeline_info.pStages = shaderStages.data();
	raytracing_pipeline_info.groupCount = static_cast<uint32_t>(_shaderGroups.size());
	raytracing_pipeline_info.pGroups = _shaderGroups.data();
	raytracing_pipeline_info.maxPipelineRayRecursionDepth = 10;
	raytracing_pipeline_info.layout = _rayTracingPipelineLayout;

	VK_CHECK(vkCreateRayTracingPipelinesKHR(_device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &raytracing_pipeline_info, nullptr, &_rayTracingPipeline));
}

void RenderEngine::create_pospo_structures()
{
	//Render pass creation

	VkAttachmentDescription colorAttachment{};
	colorAttachment.format = _swapchainImageFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorReference{};
	colorReference.attachment = 0;
	colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorReference;

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &colorAttachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;

	VK_CHECK(vkCreateRenderPass(_device, &render_pass_info, nullptr, &pospo._renderPass));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyRenderPass(_device, pospo._renderPass, nullptr);
		});


	//Pipeline creation
	VkShaderModule pospoVertex;
	if (!vkutil::load_shader_module(_device, "../shaders/light.vert.spv", &pospoVertex))
	{
		std::cout << "Error when building the deferred vertex shader" << std::endl;
	}
	else
	{
		std::cout << "Deferred vertex shader succesfully loaded" << endl;
	}

	VkShaderModule pospoFrag;
	if (!vkutil::load_shader_module(_device, "../shaders/pospo.frag.spv", &pospoFrag))
	{
		std::cout << "Error when building the deferred frag shader" << std::endl;
	}
	else
	{
		std::cout << "Frag vertex shader succesfully loaded" << endl;
	}

	//Pospo Layout
	VkPipelineLayoutCreateInfo pospo_pipeline_layout_info = vkinit::pipeline_layout_create_info();

	pospo_pipeline_layout_info.setLayoutCount = 1;
	pospo_pipeline_layout_info.pSetLayouts = &_singleTextureSetLayout;

	VK_CHECK(vkCreatePipelineLayout(_device, &pospo_pipeline_layout_info, nullptr, &pospo._pipelineLayout));

	PipelineBuilder pipelineBuilder;

	//Pospo Vertex Info
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	VertexInputDescription vertexDescription = Vertex::get_vertex_description();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexDescription.attributes.size());
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexDescription.bindings.size());
	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();

	//Pospo Assembly Info
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	//Viewport and Scissor
	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float)_windowExtent.width;
	pipelineBuilder._viewport.height = (float)_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;

	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = _windowExtent;

	//Pospo Depth Stencil
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	//Pospo Rasterizer
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

	//Pospo Multisampling
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	//Pospo Color Blend Attachment
	pipelineBuilder._colorBlendAttachment.push_back(vkinit::color_blend_attachment_state());

	//Pospo Shaders
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, pospoVertex));
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, pospoFrag));

	//Pospo Layout
	pipelineBuilder._pipelineLayout = pospo._pipelineLayout;

	pospo._pipeline = pipelineBuilder.build_pipeline(_device, pospo._renderPass);


	//pospo framebuffers
	VkFramebufferCreateInfo sc_fb_info = {};
	sc_fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	sc_fb_info.pNext = nullptr;

	sc_fb_info.renderPass = pospo._renderPass;
	sc_fb_info.attachmentCount = 1;
	sc_fb_info.width = _windowExtent.width;
	sc_fb_info.height = _windowExtent.height;
	sc_fb_info.layers = 1;

	const uint32_t swapchain_imagecount = _swapchainImages.size();
	pospo._framebuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

	for (int i = 0; i < swapchain_imagecount; i++)
	{
		VkImageView attachment = _swapchainImageViews[i];

		sc_fb_info.attachmentCount = 1;
		sc_fb_info.pAttachments = &attachment;

		VK_CHECK(vkCreateFramebuffer(_device, &sc_fb_info, nullptr, &pospo._framebuffers[i]));

		_mainDeletionQueue.push_function([=]() {
			vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
			});
	}
}

void RenderEngine::create_shader_binding_table()
{
	const uint32_t handleSize = _rayTracingPipelineProperties.shaderGroupHandleSize;
	const uint32_t handleSizeAligned = vkutil::get_aligned_size(_rayTracingPipelineProperties.shaderGroupHandleSize, _rayTracingPipelineProperties.shaderGroupHandleAlignment);
	const uint32_t groupCount = static_cast<uint32_t>(_shaderGroups.size());
	const uint32_t sbtSize = groupCount * handleSizeAligned;

	std::vector<uint8_t> shaderHandleStorage(sbtSize);
	VK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(_device, _rayTracingPipeline, 0, groupCount, sbtSize, shaderHandleStorage.data()));

	const VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	const VmaMemoryUsage memoryUsageFlags = VMA_MEMORY_USAGE_CPU_TO_GPU;

	_raygenShaderBindingTable = vkutil::create_buffer(
		_allocator, handleSize, bufferUsageFlags, memoryUsageFlags);

	_missShaderBindingTable = vkutil::create_buffer(
		_allocator, handleSize * 2, bufferUsageFlags, memoryUsageFlags);

	_hitShaderBindingTable = vkutil::create_buffer(
		_allocator, handleSize, bufferUsageFlags, memoryUsageFlags);

	// Copy handles
	void* raygen_data;
	vmaMapMemory(_allocator, _raygenShaderBindingTable._allocation, &raygen_data);
	memcpy(raygen_data, shaderHandleStorage.data(), handleSize);
	vmaUnmapMemory(_allocator, _raygenShaderBindingTable._allocation);

	void* miss_data;
	vmaMapMemory(_allocator, _missShaderBindingTable._allocation, &miss_data);
	memcpy(miss_data, shaderHandleStorage.data() + handleSizeAligned, handleSize * 2);
	vmaUnmapMemory(_allocator, _missShaderBindingTable._allocation);

	void* hit_data;
	vmaMapMemory(_allocator, _hitShaderBindingTable._allocation, &hit_data);
	memcpy(hit_data, shaderHandleStorage.data() + handleSizeAligned * 3, handleSize);
	vmaUnmapMemory(_allocator, _hitShaderBindingTable._allocation);
}

void RenderEngine::create_raytracing_descriptor_pool()
{
	//used by raytracing and also pospo
	std::vector<VkDescriptorPoolSize> poolSizes = {
	{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
	{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
	{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
	{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
	{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
	{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
	{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
	{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
	{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
	{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
	{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
	{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
	};

	VkDescriptorPoolCreateInfo dp_info = {};
	dp_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dp_info.pNext = nullptr;
	dp_info.flags = 0;
	dp_info.maxSets = 2;
	dp_info.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	dp_info.pPoolSizes = poolSizes.data();

	VK_CHECK(vkCreateDescriptorPool(_device, &dp_info, nullptr, &_rayTracingDescriptorPool));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyDescriptorPool(_device, _rayTracingDescriptorPool, nullptr);
		});
}

RayTracingScratchBuffer RenderEngine::create_scratch_buffer(VkDeviceSize size)
{
	RayTracingScratchBuffer scratchBuffer{};

	VkBufferCreateInfo bufferCreateInfo{};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = size;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	VK_CHECK(vkCreateBuffer(_device, &bufferCreateInfo, nullptr, &scratchBuffer._buffer));

	VkMemoryRequirements memoryRequirements{};
	vkGetBufferMemoryRequirements(_device, scratchBuffer._buffer, &memoryRequirements);

	VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo{};
	memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

	VkMemoryAllocateInfo memoryAllocateInfo = {};
	memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
	memoryAllocateInfo.allocationSize = memoryRequirements.size;
	memoryAllocateInfo.memoryTypeIndex = vkutil::find_memory_type_index(_physicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK(vkAllocateMemory(_device, &memoryAllocateInfo, nullptr, &scratchBuffer._memory));
	VK_CHECK(vkBindBufferMemory(_device, scratchBuffer._buffer, scratchBuffer._memory, 0));

	VkBufferDeviceAddressInfoKHR bufferDeviceAddressInfo{};
	bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	bufferDeviceAddressInfo.buffer = scratchBuffer._buffer;
	scratchBuffer._deviceAddress = vkGetBufferDeviceAddressKHR(_device, &bufferDeviceAddressInfo);

	return scratchBuffer;
}

void RenderEngine::delete_scratch_buffer(RayTracingScratchBuffer& scratchBuffer)
{
	if (scratchBuffer._memory != VK_NULL_HANDLE) {
		vkFreeMemory(_device, scratchBuffer._memory, nullptr);
	}
	if (scratchBuffer._buffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(_device, scratchBuffer._buffer, nullptr);
	}
}

void RenderEngine::create_acceleration_structure_buffer(AccelerationStructure& accelerationStructure, VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo)
{
	VkBufferCreateInfo bufferCreateInfo{};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = buildSizeInfo.accelerationStructureSize;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

	VK_CHECK(vkCreateBuffer(_device, &bufferCreateInfo, nullptr, &accelerationStructure._buffer));

	VkMemoryRequirements memoryRequirements{};
	vkGetBufferMemoryRequirements(_device, accelerationStructure._buffer, &memoryRequirements);

	VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo{};
	memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

	VkMemoryAllocateInfo memoryAllocateInfo{};
	memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
	memoryAllocateInfo.allocationSize = memoryRequirements.size;
	memoryAllocateInfo.memoryTypeIndex = vkutil::find_memory_type_index(_physicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VK_CHECK(vkAllocateMemory(_device, &memoryAllocateInfo, nullptr, &accelerationStructure._memory));
	VK_CHECK(vkBindBufferMemory(_device, accelerationStructure._buffer, accelerationStructure._memory, 0));

	_mainDeletionQueue.push_function([=]() {
		vkFreeMemory(_device, accelerationStructure._memory, nullptr);
		vkDestroyBuffer(_device, accelerationStructure._buffer, nullptr);
		});
}

BlasInput RenderEngine::renderable_to_vulkan_geometry(RenderObject renderable)
{
	BlasInput input;

	// Setup identity transform matrix

	VkTransformMatrixKHR transformMatrix = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f
	};

	// Create buffers
	VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
	VmaMemoryUsage memoryUsageFlags = VMA_MEMORY_USAGE_CPU_TO_GPU;

	AllocatedBuffer transformBuffer = vkutil::create_buffer(_allocator, sizeof(VkTransformMatrixKHR), bufferUsageFlags, memoryUsageFlags);
	_transformBuffers.push_back(transformBuffer);

	void* transformData;
	vmaMapMemory(_allocator, transformBuffer._allocation, &transformData);
	memcpy(transformData, &transformMatrix, sizeof(VkTransformMatrixKHR));
	vmaUnmapMemory(_allocator, transformBuffer._allocation);

	VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
	VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
	VkDeviceOrHostAddressConstKHR transformBufferDeviceAddress{};

	vertexBufferDeviceAddress.deviceAddress = vkutil::get_buffer_device_address(_device, renderable._prefab->_vertices.vertexBuffer._buffer);
	indexBufferDeviceAddress.deviceAddress = vkutil::get_buffer_device_address(_device, renderable._prefab->_indices.indexBuffer._buffer);
	transformBufferDeviceAddress.deviceAddress = vkutil::get_buffer_device_address(_device, transformBuffer._buffer);

	// Build
	VkAccelerationStructureGeometryKHR accelerationStructureGeometry{};
	accelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	accelerationStructureGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	accelerationStructureGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	accelerationStructureGeometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
	accelerationStructureGeometry.geometry.triangles.maxVertex = renderable._prefab->_vertices.count;
	accelerationStructureGeometry.geometry.triangles.vertexStride = sizeof(Vertex);
	accelerationStructureGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	accelerationStructureGeometry.geometry.triangles.indexData = indexBufferDeviceAddress;
	accelerationStructureGeometry.geometry.triangles.transformData.deviceAddress = 0;
	accelerationStructureGeometry.geometry.triangles.transformData.hostAddress = nullptr;
	accelerationStructureGeometry.geometry.triangles.transformData = transformBufferDeviceAddress;

	// Get size info
	VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
	accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	accelerationStructureBuildGeometryInfo.geometryCount = 1;
	accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

	const uint32_t numTriangles = renderable._prefab->_indices.count / 3;
	VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
	accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	vkGetAccelerationStructureBuildSizesKHR(
		_device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&accelerationStructureBuildGeometryInfo,
		&numTriangles,
		&accelerationStructureBuildSizesInfo);

	VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
	accelerationStructureBuildRangeInfo.primitiveCount = numTriangles;
	accelerationStructureBuildRangeInfo.primitiveOffset = 0;
	accelerationStructureBuildRangeInfo.firstVertex = 0;
	accelerationStructureBuildRangeInfo.transformOffset = 0;

	input._accelerationStructureGeometry = accelerationStructureGeometry;
	input._accelerationStructureBuildGeometryInfo = accelerationStructureBuildGeometryInfo;
	input._accelerationStructureBuildSizesInfo = accelerationStructureBuildSizesInfo;
	input._accelerationStructureBuildRangeInfo = accelerationStructureBuildRangeInfo;

	return input;
}

void RenderEngine::build_blas(const std::vector<BlasInput>& input, VkBuildAccelerationStructureFlagsKHR flags)
{
	for (const auto& blasInput : input)
	{
		AccelerationStructure newAccelerationStructure{};

		create_acceleration_structure_buffer(newAccelerationStructure, blasInput._accelerationStructureBuildSizesInfo);

		VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
		accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		accelerationStructureCreateInfo.buffer = newAccelerationStructure._buffer;
		accelerationStructureCreateInfo.size = blasInput._accelerationStructureBuildSizesInfo.accelerationStructureSize;
		accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

		vkCreateAccelerationStructureKHR(_device, &accelerationStructureCreateInfo, nullptr, &newAccelerationStructure._handle);

		// Create a small scratch buffer used during build of the bottom level acceleration structure
		RayTracingScratchBuffer scratchBuffer = create_scratch_buffer(blasInput._accelerationStructureBuildSizesInfo.buildScratchSize);

		VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{};
		accelerationBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		accelerationBuildGeometryInfo.dstAccelerationStructure = newAccelerationStructure._handle;
		accelerationBuildGeometryInfo.geometryCount = 1;
		accelerationBuildGeometryInfo.pGeometries = &blasInput._accelerationStructureGeometry;
		accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer._deviceAddress;

		VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo = blasInput._accelerationStructureBuildRangeInfo;
		std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &buildRangeInfo };

		if (_accelerationStructureFeatures.accelerationStructureHostCommands)
		{
			// Implementation supports building acceleration structure building on host
			vkBuildAccelerationStructuresKHR(
				_device,
				VK_NULL_HANDLE,
				1,
				&accelerationBuildGeometryInfo,
				accelerationBuildStructureRangeInfos.data());
		}
		else
		{
			// Acceleration structure needs to be build on the device
			vkupload::immediate_submit([&](VkCommandBuffer cmd)
				{
					vkCmdBuildAccelerationStructuresKHR(
						cmd,
						1,
						&accelerationBuildGeometryInfo,
						accelerationBuildStructureRangeInfos.data());
				});

		}

		VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
		accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
		accelerationDeviceAddressInfo.accelerationStructure = newAccelerationStructure._handle;
		newAccelerationStructure._deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(_device, &accelerationDeviceAddressInfo);

		_bottomLevelAS.push_back(newAccelerationStructure);
		delete_scratch_buffer(scratchBuffer);
	}
}

void RenderEngine::get_enabled_features()
{
	_enabledIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
	//enabledIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	_enabledIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
	_enabledIndexingFeatures.pNext = nullptr;

	_enabledBufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
	_enabledBufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
	_enabledBufferDeviceAddressFeatures.pNext = nullptr;

	_enabledRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
	_enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
	_enabledRayTracingPipelineFeatures.rayTracingPipelineTraceRaysIndirect = VK_TRUE;
	_enabledRayTracingPipelineFeatures.pNext = &_enabledBufferDeviceAddressFeatures;

	_enabledAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	_enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
	_enabledAccelerationStructureFeatures.pNext = &_enabledRayTracingPipelineFeatures;

	deviceCreatepNextChain = &_enabledAccelerationStructureFeatures;
}

void RenderEngine::create_raytracing_scene_structures(const Scene& scene)
{
	create_raytracing_pipeline(scene._renderables.size());
	create_shader_binding_table();

	create_bottom_level_acceleration_structure(scene);
	create_top_level_acceleration_structure(scene, false);
}

void RenderEngine::create_raster_scene_structures()
{
	init_descriptor_set_layouts();
	init_pipelines();
}

#pragma endregion

void RenderEngine::init_imgui(VkRenderPass renderPass)
{
	//Create Descriptor Pool for IMGUI
	VkDescriptorPoolSize pool_sizes[] =
	{
		{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
		{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
		{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
		{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
		{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &imguiPool));

	//Initialize imgui library

	//core imgui structures
	ImGui::CreateContext();

	//initializes imgui for SDL
	ImGui_ImplSDL2_InitForVulkan(_window);

	//initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = _instance;
	init_info.PhysicalDevice = _physicalDevice;
	init_info.Device = _device;
	init_info.Queue = _graphicsQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;

	ImGui_ImplVulkan_Init(&init_info, renderPass);

	//execute a gpu command to upload imgui font textures
	vkupload::immediate_submit([&](VkCommandBuffer cmd) {
		ImGui_ImplVulkan_CreateFontsTexture(cmd);
		});

	//clear font textures from cpu data
	ImGui_ImplVulkan_DestroyFontUploadObjects();

	//add to destroy the imgui created structures
	_mainDeletionQueue.push_function([=]() {
		vkDestroyDescriptorPool(_device, imguiPool, nullptr);
		ImGui_ImplVulkan_Shutdown();
		});
}

void RenderEngine::cleanup()
{
	// TODO: Poner render fence al renderer i fer el cleanup del render engine despres

	if (_isInitialized) {

		_mainDeletionQueue.flush();

		vkDestroySurfaceKHR(_instance, _surface, nullptr);

		vkDestroyDevice(_device, nullptr);
		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyWindow(_window);
	}
}

void RenderEngine::reset_imgui(RenderMode renderMode)
{
	if(renderMode == RENDER_MODE_FORWARD || renderMode == RENDER_MODE_DEFERRED)
	{
		init_imgui(_defaultRenderPass);
	}
	else
	{
		init_imgui(pospo._renderPass);
	}
}

VkPipeline PipelineBuilder::build_pipeline(VkDevice device, VkRenderPass pass)
{
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.pNext = nullptr;

	viewportState.viewportCount = 1;
	viewportState.pViewports = &_viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &_scissor;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.pNext = nullptr;

	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = static_cast<uint32_t>(_colorBlendAttachment.size());
	colorBlending.pAttachments = _colorBlendAttachment.data();


	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = nullptr;

	pipelineInfo.stageCount = static_cast<uint32_t>(_shaderStages.size());
	pipelineInfo.pStages = _shaderStages.data();
	pipelineInfo.pVertexInputState = &_vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &_inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pDepthStencilState = &_depthStencil;
	pipelineInfo.pRasterizationState = &_rasterizer;
	pipelineInfo.pMultisampleState = &_multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.layout = _pipelineLayout;
	pipelineInfo.renderPass = pass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	VkPipeline newPipeline;
	if (vkCreateGraphicsPipelines(
		device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS)
	{
		std::cout << "failed to create pipeline\n";
		return VK_NULL_HANDLE;
	}
	else
	{
		return newPipeline;
	}
}