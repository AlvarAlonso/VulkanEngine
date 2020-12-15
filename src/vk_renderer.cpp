#include "vk_renderer.h"
#include "vk_initializers.h"
#include "vk_engine.h"
#include <iostream>

using namespace GRAPHICS;

Renderer::Renderer()
{
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

void Renderer::render_forward()
{
}

void Renderer::render_deferred()
{
}

void Renderer::init_commands()
{
}

void Renderer::record_forward_command_buffers()
{
}

void Renderer::record_deferred_command_buffers()
{
}

void Renderer::init_sync_structures()
{
}
