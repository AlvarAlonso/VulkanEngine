//#include "vk_render_engine.h"
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include "vk_initializers.h"
#include "vk_utils.h"

#include "VkBootstrap.h"

#include <array>

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
	init_descriptor_set_pool();
	init_descriptor_set_layouts();
	init_depth_buffer();
	init_deferred_attachments();
	init_render_passes();
	init_framebuffers();
	init_pipelines();
	init_gbuffer_descriptors();

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

void RenderEngine::init_descriptor_set_pool()
{
	std::vector<VkDescriptorPoolSize> sizes = {
	{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
	{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10},
	{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
	{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10},
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

	VkDescriptorSetLayoutBinding textureBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);

	VkDescriptorSetLayoutCreateInfo set3Info = {};
	set3Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set3Info.pNext = nullptr;
	set3Info.bindingCount = 1;
	set3Info.flags = 0;
	set3Info.pBindings = &textureBind;

	vkCreateDescriptorSetLayout(_device, &set3Info, nullptr, &_singleTextureSetLayout);
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
	{
		//FORWARD PIPELINES
//default vertex shader for a mesh
		VkShaderModule meshVertShader;
		if (!vkutil::load_shader_module(_device, "../shaders/tri_mesh.vert.spv", &meshVertShader))
		{
			std::cout << "Error when building the triangle vertex shader module" << std::endl;
		}
		else {
			std::cout << "Red Triangle vertex shader succesfully loaded" << std::endl;
		}

		//shader for default material
		VkShaderModule colorMeshShader;
		if (!vkutil::load_shader_module(_device, "../shaders/default_lit.frag.spv", &colorMeshShader))
		{
			std::cout << "Error when building the triangle fragment shader module" << std::endl;
		}
		else
		{
			std::cout << "Triangle fragment shader succesfully loaded" << std::endl;
		}

		//shader for textured material
		VkShaderModule texturedMeshShader;
		if (!vkutil::load_shader_module(_device, "../shaders/textured_lit.frag.spv", &texturedMeshShader))
		{
			std::cout << "Error when building the textured mesh shader" << std::endl;
		}
		else
		{
			std::cout << "Textured mesh shader succesfully loaded" << endl;
		}

		// Layouts

		//mesh layout
		VkPipelineLayoutCreateInfo mesh_pipeline_layout_info = vkinit::pipeline_layout_create_info();

		VkPushConstantRange push_constant;
		push_constant.offset = 0;
		push_constant.size = sizeof(MeshPushConstants);
		push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		mesh_pipeline_layout_info.pPushConstantRanges = &push_constant;
		mesh_pipeline_layout_info.pushConstantRangeCount = 1;

		std::array<VkDescriptorSetLayout, 3> setLayouts = {
			_globalSetLayout,
			_objectSetLayout,
			_singleTextureSetLayout
		};

		mesh_pipeline_layout_info.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		mesh_pipeline_layout_info.pSetLayouts = setLayouts.data();

		VK_CHECK(vkCreatePipelineLayout(_device, &mesh_pipeline_layout_info, nullptr, &_forwardPipelineLayout));

		//PIPELINES
		PipelineBuilder pipelineBuilder;

		//color mesh pipeline

		pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();
		//connect the pipeline builder vertex input info to the one we get from Vertex
		VertexInputDescription vertexDescription = Vertex::get_vertex_description();
		pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
		pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();

		pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
		pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();


		pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

		pipelineBuilder._viewport.x = 0.0f;
		pipelineBuilder._viewport.y = 0.0f;
		pipelineBuilder._viewport.width = (float)_windowExtent.width;
		pipelineBuilder._viewport.height = (float)_windowExtent.height;
		pipelineBuilder._viewport.minDepth = 0.0f;
		pipelineBuilder._viewport.maxDepth = 1.0f;

		pipelineBuilder._scissor.offset = { 0, 0 };
		pipelineBuilder._scissor.extent = _windowExtent;

		pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

		pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);
		pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();
		pipelineBuilder._colorBlendAttachment.push_back(vkinit::color_blend_attachment_state());

		//add shaders
		pipelineBuilder._shaderStages.push_back(
			vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, meshVertShader));

		pipelineBuilder._shaderStages.push_back(
			vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, colorMeshShader));

		pipelineBuilder._pipelineLayout = _forwardPipelineLayout;

		//build the mesh pipeline
		_forwardPipeline = pipelineBuilder.build_pipeline(_device, _defaultRenderPass);

		VulkanEngine::cinstance->create_material("untexturedmesh");


		//texture pipeline

		pipelineBuilder._shaderStages.clear();
		pipelineBuilder._shaderStages.push_back(
			vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, meshVertShader));

		pipelineBuilder._shaderStages.push_back(
			vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, texturedMeshShader));

		_texPipeline = pipelineBuilder.build_pipeline(_device, _defaultRenderPass);
		VulkanEngine::cinstance->create_material("texturedmesh");

		//deleting all of the vulkan shaders
		vkDestroyShaderModule(_device, texturedMeshShader, nullptr);
		vkDestroyShaderModule(_device, colorMeshShader, nullptr);
		vkDestroyShaderModule(_device, meshVertShader, nullptr);

		//adding the pipelines to the deletion queue
		_mainDeletionQueue.push_function([=]() {
			vkDestroyPipeline(_device, _forwardPipeline, nullptr);
			vkDestroyPipeline(_device, _texPipeline, nullptr);

			vkDestroyPipelineLayout(_device, _forwardPipelineLayout, nullptr);
			});
	}

	{
		//DEFERRED PIPELINES
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

		std::array<VkDescriptorSetLayout, 3> deferredSetLayouts = { _camSetLayout, _objectSetLayout, _singleTextureSetLayout };

		layoutInfo.pushConstantRangeCount = 0;
		layoutInfo.pPushConstantRanges = nullptr;
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