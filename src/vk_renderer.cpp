#include "vk_renderer.h"
#include "vk_initializers.h"
#include "vk_engine.h"
#include <iostream>

#include <array>

using namespace GRAPHICS;

Renderer::Renderer()
{
	_graphicsQueue = VulkanEngine::cinstance->_graphicsQueue;
	_graphicsQueueFamily = VulkanEngine::cinstance->_graphicsQueueFamily;
}

void Renderer::draw_scene()
{
}

void Renderer::init_renderer()
{
	create_depth_buffer();
	create_deferred_attachments();
	init_commands();
	init_sync_structures();
}

void Renderer::create_depth_buffer()
{
	VkExtent3D depthImageExtent = {
		VulkanEngine::cinstance->_windowExtent.width,
		VulkanEngine::cinstance->_windowExtent.height,
		1
	};

	_depthFormat = VK_FORMAT_D32_SFLOAT;

	VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);

	VmaAllocationCreateInfo dimg_allocinfo = {};
	dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	dimg_allocinfo.requiredFlags = VkMemoryPropertyFlagBits(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(VulkanEngine::cinstance->_allocator, &dimg_info, &dimg_allocinfo, &_depthImage._image, &_depthImage._allocation, nullptr);

	VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthFormat, _depthImage._image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK(vkCreateImageView(VulkanEngine::cinstance->_device, &dview_info, nullptr, &_depthImageView));

	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info(VK_FILTER_NEAREST);

	vkCreateSampler(VulkanEngine::cinstance->_device, &samplerInfo, nullptr, &_defaultSampler);
}

void Renderer::create_deferred_attachments()
{
	VkExtent3D attachmentExtent{
	VulkanEngine::cinstance->_windowExtent.width,
	VulkanEngine::cinstance->_windowExtent.height,
	1
	};

	_positionFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_normalFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_albedoFormat = VK_FORMAT_R8G8B8A8_UNORM;

	VkImageCreateInfo position_igm = vkinit::image_create_info(_positionFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, attachmentExtent);
	VkImageCreateInfo normal_igm = vkinit::image_create_info(_normalFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, attachmentExtent);
	VkImageCreateInfo albedo_igm = vkinit::image_create_info(_albedoFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, attachmentExtent);

	VmaAllocationCreateInfo img_alloc_info = {};
	img_alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	img_alloc_info.requiredFlags = VkMemoryPropertyFlagBits(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(VulkanEngine::cinstance->_allocator, &position_igm, &img_alloc_info, &_positionImage._image, &_positionImage._allocation, nullptr);
	vmaCreateImage(VulkanEngine::cinstance->_allocator, &normal_igm, &img_alloc_info, &_normalImage._image, &_normalImage._allocation, nullptr);
	vmaCreateImage(VulkanEngine::cinstance->_allocator, &albedo_igm, &img_alloc_info, &_albedoImage._image, &_albedoImage._allocation, nullptr);

	VkImageViewCreateInfo position_view_igm = vkinit::imageview_create_info(VK_FORMAT_R16G16B16A16_SFLOAT, _positionImage._image, VK_IMAGE_ASPECT_COLOR_BIT);
	VkImageViewCreateInfo normal_view_igm = vkinit::imageview_create_info(VK_FORMAT_R16G16B16A16_SFLOAT, _normalImage._image, VK_IMAGE_ASPECT_COLOR_BIT);
	VkImageViewCreateInfo albedo_view_igm = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, _albedoImage._image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(VulkanEngine::cinstance->_device, &position_view_igm, nullptr, &_positionImageView));
	VK_CHECK(vkCreateImageView(VulkanEngine::cinstance->_device, &normal_view_igm, nullptr, &_normalImageView));
	VK_CHECK(vkCreateImageView(VulkanEngine::cinstance->_device, &albedo_view_igm, nullptr, &_albedoImageView));

	VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(VulkanEngine::cinstance->_device, _positionImageView, nullptr);
		vmaDestroyImage(VulkanEngine::cinstance->_allocator, _positionImage._image, _positionImage._allocation);
		vkDestroyImageView(VulkanEngine::cinstance->_device, _normalImageView, nullptr);
		vmaDestroyImage(VulkanEngine::cinstance->_allocator, _normalImage._image, _normalImage._allocation);
		vkDestroyImageView(VulkanEngine::cinstance->_device, _albedoImageView, nullptr);
		vmaDestroyImage(VulkanEngine::cinstance->_allocator, _albedoImage._image, _albedoImage._allocation);
		});
}

void Renderer::init_commands()
{
	//create a command pool for commands submitted to the graphics queue.
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateCommandPool(VulkanEngine::cinstance->_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

		//allocat the default command buffer that we will use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(VulkanEngine::cinstance->_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));

		VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
			vkDestroyCommandPool(VulkanEngine::cinstance->_device, _frames[i]._commandPool, nullptr);
			});
	}

	VK_CHECK(vkCreateCommandPool(VulkanEngine::cinstance->_device, &commandPoolInfo, nullptr, &_deferredCommandPool));

	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_deferredCommandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(VulkanEngine::cinstance->_device, &cmdAllocInfo, &_deferredCommandBuffer));

	VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(VulkanEngine::cinstance->_device, _deferredCommandPool, nullptr);
		});
}

void Renderer::init_framebuffers()
{
	//OFFSCREEN FRAMEBUFFER

	std::array<VkImageView, 4> attachments;
	attachments[0] = _positionImageView;
	attachments[1] = _normalImageView;
	attachments[2] = _albedoImageView;
	attachments[3] = _depthImageView;

	VkFramebufferCreateInfo fb_info = {};
	fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb_info.pNext = nullptr;
	fb_info.renderPass = _deferredRenderPass;
	fb_info.attachmentCount = static_cast<uint32_t>(attachments.size());
	fb_info.pAttachments = attachments.data();
	fb_info.width = VulkanEngine::cinstance->_windowExtent.width;
	fb_info.height = VulkanEngine::cinstance->_windowExtent.height;
	fb_info.layers = 1;

	VK_CHECK(vkCreateFramebuffer(VulkanEngine::cinstance->_device, &fb_info, nullptr, &_offscreen_framebuffer));

	//SWAPCHAIN FRAMEBUFFERS

	VkFramebufferCreateInfo sc_fb_info = {};
	sc_fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	sc_fb_info.pNext = nullptr;

	sc_fb_info.renderPass = _defaultRenderPass;
	sc_fb_info.attachmentCount = 1;
	sc_fb_info.width = VulkanEngine::cinstance->_windowExtent.width;
	sc_fb_info.height = VulkanEngine::cinstance->_windowExtent.height;
	sc_fb_info.layers = 1;

	const uint32_t swapchain_imagecount = VulkanEngine::cinstance->_swapchainImages.size();
	_framebuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

	for (int i = 0; i < swapchain_imagecount; i++)
	{
		std::array<VkImageView, 2> attachments = { VulkanEngine::cinstance->_swapchainImageViews[i], _depthImageView };

		sc_fb_info.attachmentCount = static_cast<uint32_t>(attachments.size());
		sc_fb_info.pAttachments = attachments.data();

		VK_CHECK(vkCreateFramebuffer(VulkanEngine::cinstance->_device, &sc_fb_info, nullptr, &_framebuffers[i]));

		VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
			vkDestroyFramebuffer(VulkanEngine::cinstance->_device, _framebuffers[i], nullptr);
			vkDestroyImageView(VulkanEngine::cinstance->_device, VulkanEngine::cinstance->_swapchainImageViews[i], nullptr);
			});
	}
}

void Renderer::init_sync_structures()
{
	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = nullptr;
	semaphoreCreateInfo.flags = 0;

	VkSemaphoreCreateInfo offscreenSemaphoreInfo = {};
	offscreenSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	offscreenSemaphoreInfo.pNext = nullptr;
	offscreenSemaphoreInfo.flags = 0;

	VK_CHECK(vkCreateSemaphore(VulkanEngine::cinstance->_device, &offscreenSemaphoreInfo, nullptr, &_offscreenSemaphore));

	VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
		vkDestroySemaphore(VulkanEngine::cinstance->_device, _offscreenSemaphore, nullptr);
		});

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateFence(VulkanEngine::cinstance->_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

		VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
			vkDestroyFence(VulkanEngine::cinstance->_device, _frames[i]._renderFence, nullptr);
			});

		VK_CHECK(vkCreateSemaphore(VulkanEngine::cinstance->_device, &semaphoreCreateInfo, nullptr, &_frames[i]._presentSemaphore));
		VK_CHECK(vkCreateSemaphore(VulkanEngine::cinstance->_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));

		VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
			vkDestroySemaphore(VulkanEngine::cinstance->_device, _frames[i]._presentSemaphore, nullptr);
			vkDestroySemaphore(VulkanEngine::cinstance->_device, _frames[i]._renderSemaphore, nullptr);
			});
	}
}

void Renderer::init_default_render_pass()
{
	VkAttachmentDescription color_attachment = {};
	color_attachment.format = VulkanEngine::cinstance->_swapchainImageFormat;
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
	depth_attachment.format = _depthFormat;
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

	VK_CHECK(vkCreateRenderPass(VulkanEngine::cinstance->_device, &render_pass_info, nullptr, &_defaultRenderPass));

	VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
		vkDestroyRenderPass(VulkanEngine::cinstance->_device, _defaultRenderPass, nullptr);
		});
}

void Renderer::init_deferred_render_pass()
{
	//gBuffers Pass

	VkAttachmentDescription position_attachment = {};
	position_attachment.format = _positionFormat;
	position_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	position_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	position_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	position_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	position_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	position_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	position_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentDescription normal_attachment = {};
	normal_attachment.format = _normalFormat;
	normal_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	normal_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	normal_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	normal_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	normal_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	normal_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	normal_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentDescription albedo_attachment = {};
	albedo_attachment.format = _albedoFormat;
	albedo_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	albedo_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	albedo_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	albedo_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	albedo_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	albedo_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	albedo_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentDescription depth_attachment = {};
	depth_attachment.format = _depthFormat;
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

	VK_CHECK(vkCreateRenderPass(VulkanEngine::cinstance->_device, &deferred_pass, nullptr, &_deferredRenderPass));

	VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
		vkDestroyRenderPass(VulkanEngine::cinstance->_device, _deferredRenderPass, nullptr);
		});
}

void Renderer::record_forward_command_buffers()
{
}

void Renderer::record_deferred_command_buffers()
{
}

void Renderer::render_forward()
{
}

void Renderer::render_deferred()
{
}