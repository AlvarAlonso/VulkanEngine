#include "vk_renderer.h"
#include "vk_initializers.h"
#include "vk_engine.h"
#include "vk_material.h"
#include "vk_textures.h"
#include "vk_prefab.h"
#include "vk_utils.h"
#include <iostream>

#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>

#include <array>
#include <map>
#include <algorithm>

RenderMode operator++(RenderMode& m, int) {

	if (m == RENDER_MODE_RAYTRACING) {

		m = RENDER_MODE_FORWARD;
		return m;
	}
	else {

		m = RenderMode(m + 1);
		return m;
	}
}

Renderer::Renderer()
{
	re = new RenderEngine();
	re->init();
	
	_physicalDevice = re->_physicalDevice;
	_device = RenderEngine::_device;
	_allocator = re->_allocator;
	_graphicsQueue = re->_graphicsQueue;
	_graphicsQueueFamily = re->_graphicsQueueFamily;
	_renderMode = RENDER_MODE_RAYTRACING;
	re->reset_imgui(_renderMode);

	init_renderer();
}

void Renderer::cleanup()
{
	re->cleanup();
}

SDL_Window* Renderer::get_sdl_window()
{
	return re->_window;
}

void Renderer::switch_render_mode()
{
	if (_renderMode == RENDER_MODE_RAYTRACING)
	{
		re->reset_imgui(_renderMode);
	}

	_renderMode++;

	if(_renderMode == RENDER_MODE_RAYTRACING)
	{
		re->reset_imgui(_renderMode);
	}

	// TODO: More things to add to optimize resources (only create structures related to the current render mode).
}

void Renderer::init_renderer()
{
	init_commands();
	init_sync_structures();
	create_descriptor_buffers();
	create_uniform_buffer();

	render_quad.create_quad();
}

void Renderer::draw_scene()
{
	if(currentScene == nullptr)
	{
		std::cout << "ERROR: The current scene in the renderer is null. Set the scene from the engine before attempting to draw!" << std::endl;
		std::abort();
	}

	ImGui::Render();

	if(!isDeferredCommandInit)
	{
		re->create_raster_scene_structures();
		init_descriptors();
		record_deferred_command_buffers(currentScene->_renderables.data(), currentScene->_renderables.size());
		isDeferredCommandInit = true;
	}

	if(_renderMode == RENDER_MODE_RAYTRACING && !areAccelerationStructuresInit)
	{
		re->create_raytracing_scene_structures(*currentScene);
		create_raytracing_descriptor_sets();
		record_raytracing_command_buffer();
		areAccelerationStructuresInit = true;
	}
	
	if(_renderMode == RENDER_MODE_FORWARD)
	{
		std::cout << "FORWARD RENDERING WAS REMOVED" << std::endl;
	}
	else if(_renderMode == RENDER_MODE_DEFERRED)
	{
		render_deferred();
	}
	else if(_renderMode == RENDER_MODE_RAYTRACING)
	{
		render_raytracing();
	}
}

void Renderer::create_uniform_buffer()
{
	_ubo = vkutil::create_buffer(_allocator, sizeof(uniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VMA_MEMORY_USAGE_CPU_TO_GPU);

	re->_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(_allocator, _ubo._buffer, _ubo._allocation);
		});

	//update_uniform_buffers();
}

void Renderer::update_uniform_buffers(RenderObject* first, size_t count)
{
	//Camera Update
	glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1700.0f / 900.0f, 0.1f, 512.0f);
	projection[1][1] *= -1;

	void* data;
	vmaMapMemory(_allocator, _ubo._allocation, &data);
	uniformData.projInverse = glm::inverse(projection);
	uniformData.viewInverse = glm::inverse(VulkanEngine::cinstance->camera->getView());
	uniformData.position = glm::vec4(VulkanEngine::cinstance->camera->_position, 1.0);
	memcpy(data, &uniformData, sizeof(uniformData));
	vmaUnmapMemory(_allocator, _ubo._allocation);

	//models update
	std::vector<RenderObject>& renderables = currentScene->_renderables;
	std::vector<glm::mat4> transforms;

	void* transformData;
	vmaMapMemory(_allocator, _transformBuffer._allocation, &transformData);

	for(const auto& renderable : renderables)
	{
		for(const auto& node : renderable._prefab->_roots)
		{
			get_nodes_transforms(nullptr, *node, renderable._model, transforms);
		}
	}

	memcpy(transformData, transforms.data(), transforms.size() * sizeof(glm::mat4));

	vmaUnmapMemory(_allocator, _transformBuffer._allocation);
	
	// TODO: Solve memory leak
	//re->create_top_level_acceleration_structure(*currentScene, true);
}

void Renderer::create_raytracing_descriptor_sets()
{
	std::vector<RenderObject>& renderables = currentScene->_renderables;

	// Allocation of raytracing DescriptorSet
	VkDescriptorSetAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc_info.pNext = nullptr;
	alloc_info.descriptorPool = re->_rayTracingDescriptorPool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &re->_rayTracingSetLayout;

	VK_CHECK(vkAllocateDescriptorSets(_device, &alloc_info, &_rayTracingDescriptorSet));

	// Binding 0 : Acceleration Structure Descriptor
	VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo{};
	descriptorAccelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
	descriptorAccelerationStructureInfo.pAccelerationStructures = &re->_topLevelAS._handle;

	VkWriteDescriptorSet accelerationStructureWrite{};
	accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	// The specialized acceleration structure descriptor has to be chained
	accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
	accelerationStructureWrite.dstSet = _rayTracingDescriptorSet;
	accelerationStructureWrite.dstBinding = 0;
	accelerationStructureWrite.descriptorCount = 1;
	accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

	// Binding 1: Result Image Descriptor
	VkDescriptorImageInfo storageImageDescriptor{};
	storageImageDescriptor.imageView = re->_storageImageView;
	storageImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	// Binding 2: Camera Descriptor
	VkDescriptorBufferInfo uboBufferDescriptor{};
	uboBufferDescriptor.offset = 0;
	uboBufferDescriptor.buffer = _ubo._buffer;
	uboBufferDescriptor.range = sizeof(uniformData);

	// ----------------------------------------------------
	std::vector<VkDescriptorBufferInfo> verticesBufferInfos;
	std::vector<VkDescriptorBufferInfo> indicesBufferInfos;

	verticesBufferInfos.reserve(renderables.size());
	indicesBufferInfos.reserve(renderables.size());
	std::vector<PrimitiveToShader> primitivesInfo;
	std::vector<glm::mat4> transforms;

	// Binding 5: Transforms Descriptor
	_transformBuffer = vkutil::create_buffer(_allocator, sizeof(glm::mat4) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	VkDescriptorBufferInfo transformBufferInfo{};
	transformBufferInfo.offset = 0;
	transformBufferInfo.buffer = _transformBuffer._buffer;
	transformBufferInfo.range = sizeof(glm::mat4) * MAX_OBJECTS;

	for (int i = 0; i < renderables.size(); i++)
	{
		// Binding 3: RTVertices Descriptor
		VkDescriptorBufferInfo rtVertexBufferInfo{};
		rtVertexBufferInfo.offset = 0;
		rtVertexBufferInfo.buffer = renderables[i]._prefab->_vertices.rtvBuffer._buffer;
		rtVertexBufferInfo.range = renderables[i]._prefab->_vertices.count * sizeof(rtVertex);

		verticesBufferInfos.push_back(rtVertexBufferInfo);
			
		// Binding 4: Vertex Indices Descriptor
		VkDescriptorBufferInfo indexBufferInfo{};
		indexBufferInfo.offset = 0;
		indexBufferInfo.buffer = renderables[i]._prefab->_indices.indexBuffer._buffer;
		indexBufferInfo.range = renderables[i]._prefab->_indices.count * sizeof(uint32_t);

		indicesBufferInfos.push_back(indexBufferInfo);

		// Warning: Take care with how primitives are added
		// Binding 6: Primitives Descriptor
		for(const auto& node : renderables[i]._prefab->_roots)
		{
			get_primitive_to_shader_info(nullptr, *node, renderables[i]._model, primitivesInfo, transforms, i);
		}
	}

	_primitiveInfoBuffer = vkutil::create_buffer(_allocator, primitivesInfo.size() * sizeof(PrimitiveToShader), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	VkDescriptorBufferInfo primitivesBufferDescriptor{};
	primitivesBufferDescriptor.offset = 0;
	primitivesBufferDescriptor.buffer = _primitiveInfoBuffer._buffer;
	primitivesBufferDescriptor.range = primitivesInfo.size() * sizeof(PrimitiveToShader);

	void* primitivesData;
	vmaMapMemory(_allocator, _primitiveInfoBuffer._allocation, &primitivesData);
	memcpy(primitivesData, primitivesInfo.data(), primitivesInfo.size() * sizeof(PrimitiveToShader));
	vmaUnmapMemory(_allocator, _primitiveInfoBuffer._allocation);

	// Binding 7: Scene Lights Descriptor
	_sceneBuffer = vkutil::create_buffer(_allocator, currentScene->_lights.size() * sizeof(Light), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	VkDescriptorBufferInfo sceneBufferDescriptor{};
	sceneBufferDescriptor.offset = 0;
	sceneBufferDescriptor.buffer = _sceneBuffer._buffer;
	sceneBufferDescriptor.range = currentScene->_lights.size() * sizeof(Light);

	void* sceneData;
	vmaMapMemory(_allocator, _sceneBuffer._allocation, &sceneData);
	memcpy(sceneData, currentScene->_lights.data(), currentScene->_lights.size() * sizeof(Light));
	vmaUnmapMemory(_allocator, _sceneBuffer._allocation);

	// Binding 8: Materials Descriptor

	// Create material infos vector
	_materialInfos.resize(VKE::Material::sMaterials.size());

	// Sort materials by their IDs
	std::vector<VKE::Material*> materialsAux;
	materialsAux.resize(VKE::Material::sMaterials.size());

	int i = 0;
	for (const auto& material : VKE::Material::sMaterials)
	{
		materialsAux[i] = material.second;
		i++;
	}

	std::sort(materialsAux.begin(), materialsAux.end(), VKE::Material::ComparePtrToMaterial);

	// Assign values to MaterialsToShader
	i = 0;
	for (const auto& material : materialsAux)
	{
		_materialInfos[i]._color = material->_color;
		_materialInfos[i]._emissive_factor = material->_emissive_factor;

		glm::vec4* factors = &glm::vec4{
			material->_roughness_factor, material->_metallic_factor,
			material->_tilling_factor, -1 };

		if (material->_color_texture == nullptr)
		{
			factors->w = VKE::Texture::sTexturesLoaded["default"]->_id;
		}
		else
		{
			factors->w = material->_color_texture->_id;
		}

		factors->z = 1;

		_materialInfos[i]._roughness_metallic_tilling_color_factors = glm::vec4{
			factors->x ? factors->x : 0, factors->y ? factors->y : 0,
			factors->z ? factors->z : 1, factors->w ? factors->w : 0
		};

		_materialInfos[i]._emissive_metRough_occlusion_normal_indices = glm::vec4{ -1, -1, -1, -1 };

		if (material->_emissive_texture)
		{
			_materialInfos[i]._emissive_metRough_occlusion_normal_indices.x = material->_emissive_texture->_id;
		}
		if (material->_metallic_roughness_texture)
		{
			_materialInfos[i]._emissive_metRough_occlusion_normal_indices.y = material->_metallic_roughness_texture->_id;
		}
		if (material->_occlusion_texture)
		{
			_materialInfos[i]._emissive_metRough_occlusion_normal_indices.z = material->_occlusion_texture->_id;
		}
		if (material->_normal_texture)
		{
			_materialInfos[i]._emissive_metRough_occlusion_normal_indices.w = material->_normal_texture->_id;
		}

		i++;
	}

	_materialBuffer = vkutil::create_buffer(_allocator, _materialInfos.size() * sizeof(VKE::MaterialToShader), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	
	VkDescriptorBufferInfo materialBufferDescriptor{};
	materialBufferDescriptor.offset = 0;
	materialBufferDescriptor.buffer = _materialBuffer._buffer;
	materialBufferDescriptor.range = _materialInfos.size() * sizeof(VKE::MaterialToShader);

	void* materialData;
	vmaMapMemory(_allocator, _materialBuffer._allocation, &materialData);
	memcpy(materialData, _materialInfos.data(), _materialInfos.size() * sizeof(VKE::MaterialToShader));
	vmaUnmapMemory(_allocator, _materialBuffer._allocation);

	// Binding 9: Textures Descriptor
	// Pass the texture data from a map to a vector, and order it by idx
	std::vector<VkDescriptorImageInfo> textureImageInfos;
	int texCount = VKE::Texture::textureCount;
	textureImageInfos.reserve(texCount);

	std::vector<VKE::Texture*> orderedTexVec;

	for (auto const& texture : VKE::Texture::sTexturesLoaded)
	{
		orderedTexVec.push_back(texture.second);
	}

	// Order tex idx vector
	std::sort(orderedTexVec.begin(), orderedTexVec.end(), VKE::Texture::ComparePtrToTexture);

	for (auto const& texture : orderedTexVec)
	{
		VkDescriptorImageInfo textureImageDescriptor{};
		textureImageDescriptor.sampler = re->_defaultSampler;
		textureImageDescriptor.imageView = texture->_imageView;
		textureImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		textureImageInfos.push_back(textureImageDescriptor);
	}

	//Descriptor Writes
	VkWriteDescriptorSet resultImageWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _rayTracingDescriptorSet, &storageImageDescriptor, 1);
	VkWriteDescriptorSet uniformBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _rayTracingDescriptorSet, &uboBufferDescriptor, 2);
	VkWriteDescriptorSet vertexBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rayTracingDescriptorSet, verticesBufferInfos.data(), 3, renderables.size());
	VkWriteDescriptorSet indexBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rayTracingDescriptorSet, indicesBufferInfos.data(), 4, renderables.size());
	VkWriteDescriptorSet transformBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rayTracingDescriptorSet, &transformBufferInfo, 5);
	VkWriteDescriptorSet primitivesInfoWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rayTracingDescriptorSet, &primitivesBufferDescriptor, 6);
	VkWriteDescriptorSet sceneBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _rayTracingDescriptorSet, &sceneBufferDescriptor, 7);
	VkWriteDescriptorSet materialBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rayTracingDescriptorSet, &materialBufferDescriptor, 8);
	VkWriteDescriptorSet textureImagesWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _rayTracingDescriptorSet, textureImageInfos.data(), 9, textureImageInfos.size());

	std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
		accelerationStructureWrite,
		resultImageWrite,
		uniformBufferWrite,
		vertexBufferWrite,
		indexBufferWrite,
		transformBufferWrite,
		primitivesInfoWrite,
		sceneBufferWrite,
		materialBufferWrite,
		textureImagesWrite,
	};

	vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);

	//pospo descriptor
	VkDescriptorSetAllocateInfo pospo_alloc_info = {};
	pospo_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	pospo_alloc_info.pNext = nullptr;
	pospo_alloc_info.descriptorPool = re->_rayTracingDescriptorPool;
	pospo_alloc_info.descriptorSetCount = 1;
	pospo_alloc_info.pSetLayouts = &re->_singleTextureSetLayout;

	VkResult result = vkAllocateDescriptorSets(_device, &pospo_alloc_info, &re->pospo._textureSet);

	VkDescriptorImageInfo pospoImageInfo = {};
	pospoImageInfo.sampler = re->_defaultSampler;
	pospoImageInfo.imageView = re->_storageImageView;
	pospoImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet pospoWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, re->pospo._textureSet, &pospoImageInfo, 0);

	vkUpdateDescriptorSets(_device, 1, &pospoWrite, 0, VK_NULL_HANDLE);

	re->_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(_allocator, _transformBuffer._buffer, _transformBuffer._allocation);
		vmaDestroyBuffer(_allocator, _primitiveInfoBuffer._buffer, _primitiveInfoBuffer._allocation);
		vmaDestroyBuffer(_allocator, _sceneBuffer._buffer, _sceneBuffer._allocation);
		vmaDestroyBuffer(_allocator, _materialBuffer._buffer, _materialBuffer._allocation);
		});
}

void Renderer::record_raytracing_command_buffer()
{
	VkCommandBuffer cmd = _raytracingCommandBuffer;

	VkCommandBufferBeginInfo cmdBufInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

	VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBufInfo));
	
	/*
		Setup the buffer regions pointing to the shaders in our shader binding table
	*/

	const uint32_t handleSizeAligned = vkutil::get_aligned_size(re->_rayTracingPipelineProperties.shaderGroupHandleSize, re->_rayTracingPipelineProperties.shaderGroupHandleAlignment);

	VkBufferDeviceAddressInfoKHR raygenDeviceAddressInfo{};
	raygenDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	raygenDeviceAddressInfo.buffer = re->_raygenShaderBindingTable._buffer;

	VkStridedDeviceAddressRegionKHR raygenShaderSbtEntry{};
	raygenShaderSbtEntry.deviceAddress = vkGetBufferDeviceAddress(_device, &raygenDeviceAddressInfo);
	raygenShaderSbtEntry.stride = handleSizeAligned;
	raygenShaderSbtEntry.size = handleSizeAligned;

	VkBufferDeviceAddressInfoKHR missDeviceAddressInfo{};
	missDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	missDeviceAddressInfo.buffer = re->_missShaderBindingTable._buffer;

	VkStridedDeviceAddressRegionKHR missShaderSbtEntry{};
	missShaderSbtEntry.deviceAddress = vkGetBufferDeviceAddress(_device, &missDeviceAddressInfo);
	missShaderSbtEntry.stride = handleSizeAligned;
	missShaderSbtEntry.size = handleSizeAligned;

	VkBufferDeviceAddressInfoKHR hitDeviceAddressInfo{};
	hitDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	hitDeviceAddressInfo.buffer = re->_hitShaderBindingTable._buffer;

	VkStridedDeviceAddressRegionKHR hitShaderSbtEntry{};
	hitShaderSbtEntry.deviceAddress = vkGetBufferDeviceAddress(_device, &hitDeviceAddressInfo);
	hitShaderSbtEntry.stride = handleSizeAligned;
	hitShaderSbtEntry.size = handleSizeAligned;

	VkStridedDeviceAddressRegionKHR callableShaderSbtEntry{};

	/*
		Dispatch the ray tracing commands
	*/
	
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, re->_rayTracingPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, re->_rayTracingPipelineLayout, 0, 1, &_rayTracingDescriptorSet, 0, 0);
	
	re->vkCmdTraceRaysKHR(
		cmd,
		&raygenShaderSbtEntry,
		&missShaderSbtEntry,
		&hitShaderSbtEntry,
		&callableShaderSbtEntry,
		re->_windowExtent.width,
		re->_windowExtent.height,
		1);
	
	/*
		Copy ray tracing output to swap chain image
	*/
	
	// Prepare current swap chain image as transfer destination
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = re->_storageImage;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);
	}
	
	VK_CHECK(vkEndCommandBuffer(cmd));
}

void Renderer::record_pospo_command_buffer(VkCommandBuffer cmd, uint32_t swapchainImageIndex)
{
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	VkClearValue clearValue = {0.1f, 0.1f, 0.1f, 1.0f};

	VkRenderPassBeginInfo pospo_begin_info = vkinit::renderpass_begin_info(re->pospo._renderPass, re->_windowExtent, re->pospo._framebuffers[swapchainImageIndex]);
	pospo_begin_info.clearValueCount = 1;
	pospo_begin_info.pClearValues = &clearValue;

	vkCmdBeginRenderPass(cmd, &pospo_begin_info, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, re->pospo._pipeline);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, re->pospo._pipelineLayout, 0, 1, &re->pospo._textureSet, 0, nullptr);

	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &render_quad._vertexBuffer._buffer, &offset);
	vkCmdBindIndexBuffer(cmd, render_quad._indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(cmd, static_cast<uint32_t>(render_quad._indices.size()), 1, 0, 0, 0);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRenderPass(cmd);

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = re->_storageImage;
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

	VK_CHECK(vkEndCommandBuffer(cmd));
}

//raster

void Renderer::init_commands()
{
	//create a command pool for commands submitted to the graphics queue.
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

		//allocat the default command buffer that we will use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));

		re->_mainDeletionQueue.push_function([=]() {
			vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
			});
	}

	VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_deferredCommandPool));

	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_deferredCommandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_deferredCommandBuffer));

	//raytracing command buffer
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_raytracingCommandBuffer));

	re->_mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(_device, _deferredCommandPool, nullptr);
		});
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

	VK_CHECK(vkCreateSemaphore(_device, &offscreenSemaphoreInfo, nullptr, &_offscreenSemaphore));

	re->_mainDeletionQueue.push_function([=]() {
		vkDestroySemaphore(_device, _offscreenSemaphore, nullptr);
		});

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

		re->_mainDeletionQueue.push_function([=]() {
			vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
			});

		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._presentSemaphore));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));

		re->_mainDeletionQueue.push_function([=]() {
			vkDestroySemaphore(_device, _frames[i]._presentSemaphore, nullptr);
			vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
			});
	}
}

void Renderer::create_descriptor_buffers()
{
	const size_t sceneParamBufferSize = FRAME_OVERLAP * vkutil::get_aligned_size(sizeof(GPUSceneData), re->_gpuProperties.limits.minUniformBufferOffsetAlignment);

	_sceneParameterBuffer = vkutil::create_buffer(_allocator, sceneParamBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		//_frames[i].objectBuffer = vkutil::create_buffer(_allocator, sizeof(GPUObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		_frames[i].cameraBuffer = vkutil::create_buffer(_allocator, sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		//cam buffer
		_camBuffer = vkutil::create_buffer(_allocator, sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	}

	//deferred
	_objectBuffer = vkutil::create_buffer(_allocator, sizeof(VKE::MaterialToShader) * MAX_MATERIALS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
}


void Renderer::record_deferred_command_buffers(RenderObject* first, int count)
{
	//FIRST PASS

	VkCommandBufferBeginInfo deferredCmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

	VK_CHECK(vkBeginCommandBuffer(_deferredCommandBuffer, &deferredCmdBeginInfo));

	VkClearValue first_clearValue;
	first_clearValue.color = { {0.2f, 0.2f, 0.2f, 1.0f} };

	VkClearValue first_depthClear;
	first_depthClear.depthStencil.depth = 1.0f;

	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(re->_deferredRenderPass, re->_windowExtent, re->_offscreen_framebuffer);

	std::array<VkClearValue, 4> first_clearValues = { first_clearValue, first_clearValue, first_clearValue, first_depthClear };

	rpInfo.clearValueCount = static_cast<uint32_t>(first_clearValues.size());
	rpInfo.pClearValues = first_clearValues.data();

	vkCmdBeginRenderPass(_deferredCommandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(_deferredCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_deferredPipeline);

	vkCmdBindDescriptorSets(_deferredCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_deferredPipelineLayout, 0, 1, &_camDescriptorSet, 0, nullptr);

	vkCmdBindDescriptorSets(_deferredCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_deferredPipelineLayout, 1, 1, &_materialsDescriptorSet, 0, nullptr);

	VKE::Prefab* lastPrefab = nullptr;
	for(int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];

		if(object._prefab != lastPrefab)
		{
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(_deferredCommandBuffer, 0, 1, &object._prefab->_vertices.vertexBuffer._buffer, &offset);
			if(object._prefab->_indices.count > 0)
			{
				vkCmdBindIndexBuffer(_deferredCommandBuffer, object._prefab->_indices.indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);
			}
		}

		object._prefab->draw(object._model, _deferredCommandBuffer, re->_deferredPipelineLayout);
	}

	vkCmdEndRenderPass(_deferredCommandBuffer);

	VK_CHECK(vkEndCommandBuffer(_deferredCommandBuffer));
}

void Renderer::init_descriptors()
{
	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.pNext = nullptr;
		allocInfo.descriptorPool = re->_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &re->_globalSetLayout;

		vkAllocateDescriptorSets(_device, &allocInfo, &_frames[i].globalDescriptor);

		VkDescriptorSetAllocateInfo objectSetAlloc = {};
		objectSetAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		objectSetAlloc.pNext = nullptr;
		objectSetAlloc.descriptorPool = re->_descriptorPool;
		objectSetAlloc.descriptorSetCount = 1;
		objectSetAlloc.pSetLayouts = &re->_objectSetLayout;

		vkAllocateDescriptorSets(_device, &objectSetAlloc, &_frames[i].objectDescriptor);

		VkDescriptorBufferInfo cameraInfo = {};
		cameraInfo.buffer = _frames[i].cameraBuffer._buffer;
		cameraInfo.offset = 0;
		cameraInfo.range = sizeof(GPUCameraData);

		VkDescriptorBufferInfo sceneInfo = {};
		sceneInfo.buffer = _sceneParameterBuffer._buffer;
		sceneInfo.offset = 0;
		sceneInfo.range = sizeof(GPUSceneData);

		VkWriteDescriptorSet cameraWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _frames[i].globalDescriptor, &cameraInfo, 0);

		VkWriteDescriptorSet sceneWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, _frames[i].globalDescriptor, &sceneInfo, 1);

		std::array<VkWriteDescriptorSet, 2> setWrites = { cameraWrite, sceneWrite };

		vkUpdateDescriptorSets(_device, static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0, nullptr);
	}

	//DEFERRED DESCRIPTORS
	// Camera
	VkDescriptorSetAllocateInfo cameraSetAllocInfo = {};
	cameraSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	cameraSetAllocInfo.pNext = nullptr;
	cameraSetAllocInfo.descriptorPool = re->_descriptorPool;
	cameraSetAllocInfo.descriptorSetCount = 1;
	cameraSetAllocInfo.pSetLayouts = &re->_camSetLayout;

	vkAllocateDescriptorSets(_device, &cameraSetAllocInfo, &_camDescriptorSet);

	VkDescriptorBufferInfo camBufferInfo = {};
	camBufferInfo.buffer = _camBuffer._buffer;
	camBufferInfo.offset = 0;
	camBufferInfo.range = sizeof(GPUCameraData);

	// Object materials
	VkDescriptorSetAllocateInfo objectSetAlloc = {};
	objectSetAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	objectSetAlloc.pNext = nullptr;
	objectSetAlloc.descriptorPool = re->_descriptorPool;
	objectSetAlloc.descriptorSetCount = 1;
	objectSetAlloc.pSetLayouts = &re->_materialsSetLayout;

	vkAllocateDescriptorSets(_device, &objectSetAlloc, &_materialsDescriptorSet);

	VkDescriptorBufferInfo materialBufferInfo;
	materialBufferInfo.buffer = _objectBuffer._buffer;
	materialBufferInfo.offset = 0;
	materialBufferInfo.range = sizeof(VKE::MaterialToShader) * MAX_MATERIALS;

	// Pass the texture data from a map to a vector, and order it by idx
	// Material Textures
	std::vector<VkDescriptorImageInfo> textureImageInfos;
	textureImageInfos.reserve(VKE::Texture::sTexturesLoaded.size());

	std::vector<VKE::Texture*> orderedTexVec;
	orderedTexVec.reserve(VKE::Texture::sTexturesLoaded.size());

	for (auto const& texture : VKE::Texture::sTexturesLoaded)
	{
		orderedTexVec.push_back(texture.second);
	}

	// Order tex idx vector
	std::sort(orderedTexVec.begin(), orderedTexVec.end(), VKE::Texture::ComparePtrToTexture);

	for(const auto& texture : orderedTexVec)
	{
		VkDescriptorImageInfo textureImageDescriptor;
		textureImageDescriptor.sampler = re->_defaultSampler;
		textureImageDescriptor.imageView = texture->_imageView;
		textureImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		textureImageInfos.push_back(textureImageDescriptor);
	}

	VkWriteDescriptorSet camWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _camDescriptorSet, &camBufferInfo, 0);
	VkWriteDescriptorSet materialsWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _materialsDescriptorSet, &materialBufferInfo, 0);
	VkWriteDescriptorSet texturesWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _materialsDescriptorSet, textureImageInfos.data(), 1, static_cast<uint32_t>(textureImageInfos.size()));

	std::vector<VkWriteDescriptorSet> deferredWrites = {
		camWrite,
		materialsWrite,
		texturesWrite
	};

	vkUpdateDescriptorSets(_device, static_cast<uint32_t>(deferredWrites.size()), deferredWrites.data(), 0, nullptr);

	// UPLOAD MATERIAL INFO TO GPU
	// Create material infos vector
	_materialInfos.resize(VKE::Material::sMaterials.size());

	// Sort materials by their IDs
	std::vector<VKE::Material*> materialsAux;
	materialsAux.resize(VKE::Material::sMaterials.size());

	int i = 0;
	for(const auto& material : VKE::Material::sMaterials)
	{
		materialsAux[i] = material.second;
		i++;
	}

	std::sort(materialsAux.begin(), materialsAux.end(), VKE::Material::ComparePtrToMaterial);

	// Assign values to MaterialsToShader
	i = 0;
	for(const auto& material : materialsAux)
	{
		_materialInfos[i]._color = material->_color;
		_materialInfos[i]._emissive_factor = material->_emissive_factor;

		glm::vec4* factors = &glm::vec4{
			material->_roughness_factor, material->_metallic_factor,
			material->_tilling_factor, -1 };

		if (material->_color_texture == nullptr)
		{
			factors->w = VKE::Texture::sTexturesLoaded["default"]->_id;
		}
		else
		{
			factors->w = material->_color_texture->_id;
		}

		factors->z = 1;

		_materialInfos[i]._roughness_metallic_tilling_color_factors = glm::vec4{
			factors->x ? factors->x : 0, factors->y ? factors->y : 0,
			factors->z ? factors->z : 1, factors->w ? factors->w : 0
		};

		_materialInfos[i]._emissive_metRough_occlusion_normal_indices = glm::vec4{ -1, -1, -1, -1 };

		if(material->_emissive_texture)
		{
			_materialInfos[i]._emissive_metRough_occlusion_normal_indices.x = material->_emissive_texture->_id;
		}
		if (material->_metallic_roughness_texture)
		{
			_materialInfos[i]._emissive_metRough_occlusion_normal_indices.x = material->_metallic_roughness_texture->_id;
		}
		if (material->_occlusion_texture)
		{
			_materialInfos[i]._emissive_metRough_occlusion_normal_indices.x = material->_occlusion_texture->_id;
		}
		if (material->_normal_texture)
		{
			_materialInfos[i]._emissive_metRough_occlusion_normal_indices.x = material->_normal_texture->_id;
		}

		i++;
	}
}

// Update descriptors for deferred
void Renderer::update_descriptors(RenderObject* first, size_t count)
{
	glm::mat4 projection = glm::perspective(glm::radians(70.0f), 1700.0f / 900.0f, 0.1f, 200.0f);
	projection[1][1] *= -1;

	GPUCameraData camData;
	camData.projection = projection;
	camData.view = VulkanEngine::cinstance->camera->getView();
	camData.viewproj = projection * camData.view;
	
	void* data2;
	vmaMapMemory(_allocator, _camBuffer._allocation, &data2);
	memcpy(data2, &camData, sizeof(GPUCameraData));
	vmaUnmapMemory(_allocator, _camBuffer._allocation);

	void* data;
	vmaMapMemory(_allocator, get_current_frame().cameraBuffer._allocation, &data);
	memcpy(data, &camData, sizeof(GPUCameraData));
	vmaUnmapMemory(_allocator, get_current_frame().cameraBuffer._allocation);

	float framed = { _frameNumber / 120.0f };

	_sceneParameters.ambientColor = { sin(framed), 0, cos(framed), 1 };

	char* sceneData;
	vmaMapMemory(_allocator, _sceneParameterBuffer._allocation, (void**)&sceneData);

	int frameIndex = _frameNumber % FRAME_OVERLAP;

	sceneData += vkutil::get_aligned_size(sizeof(GPUSceneData), re->_gpuProperties.limits.minUniformBufferOffsetAlignment) * frameIndex;

	memcpy(sceneData, &_sceneParameters, sizeof(GPUSceneData));

	vmaUnmapMemory(_allocator, _sceneParameterBuffer._allocation);

	void* objectData;
	vmaMapMemory(_allocator, _objectBuffer._allocation, &objectData);

	memcpy(objectData, _materialInfos.data(), sizeof(VKE::MaterialToShader) * _materialInfos.size());

	vmaUnmapMemory(_allocator, _objectBuffer._allocation);
}

int Renderer::get_current_frame_index()
{
	return _frameNumber % FRAME_OVERLAP;
}

void Renderer::render_deferred()
{
	VK_CHECK(vkWaitForFences(_device, 1, &_frames[get_current_frame_index()]._renderFence, true, UINT64_MAX));
	VK_CHECK(vkResetFences(_device, 1, &_frames[get_current_frame_index()]._renderFence));

	//request image from the swapchain
	uint32_t swapchainImageIndex;
	VkResult result = vkAcquireNextImageKHR(_device, re->_swapchain, 0, _frames[get_current_frame_index()]._presentSemaphore, nullptr, &swapchainImageIndex);

	int idx = get_current_frame_index();
	VkCommandBuffer cmd = _frames[get_current_frame_index()]._mainCommandBuffer;

	update_descriptors(currentScene->_renderables.data(), currentScene->_renderables.size());

	draw_deferred(cmd, swapchainImageIndex);

	VkSubmitInfo offscreenSubmit = vkinit::submit_info(&_deferredCommandBuffer);

	VkPipelineStageFlags offscreenWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	offscreenSubmit.pWaitDstStageMask = &offscreenWaitStage;
	offscreenSubmit.waitSemaphoreCount = 1;
	offscreenSubmit.pWaitSemaphores = &_frames[get_current_frame_index()]._presentSemaphore;
	offscreenSubmit.signalSemaphoreCount = 1;
	offscreenSubmit.pSignalSemaphores = &_offscreenSemaphore;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &offscreenSubmit, nullptr));

	VkSubmitInfo renderSubmit = vkinit::submit_info(&_frames[get_current_frame_index()]._mainCommandBuffer);

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	renderSubmit.pWaitDstStageMask = &waitStage;
	renderSubmit.waitSemaphoreCount = 1;
	renderSubmit.pWaitSemaphores = &_offscreenSemaphore;
	renderSubmit.signalSemaphoreCount = 1;
	renderSubmit.pSignalSemaphores = &_frames[get_current_frame_index()]._renderSemaphore;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &renderSubmit, _frames[get_current_frame_index()]._renderFence));

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.pSwapchains = &re->_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &_frames[get_current_frame_index()]._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

	_frameNumber++;
}

//raytracing
void Renderer::render_raytracing()
{
	VK_CHECK(vkWaitForFences(_device, 1, &_frames[get_current_frame_index()]._renderFence, true, UINT64_MAX));
	VK_CHECK(vkResetFences(_device, 1, &_frames[get_current_frame_index()]._renderFence));

	//request image from the swapchain
	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_device, re->_swapchain, 0, _frames[get_current_frame_index()]._presentSemaphore, nullptr, &swapchainImageIndex));

	update_uniform_buffers(currentScene->_renderables.data(), currentScene->_renderables.size());

	//VK_CHECK(vkResetCommandBuffer(_raytracingCommandBuffer, 0));

	//record_raytracing_command_buffer();

	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

	VkSubmitInfo submit_info = vkinit::submit_info(&_raytracingCommandBuffer);
	submit_info.pWaitDstStageMask = waitStages;
	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitSemaphores = &_frames[get_current_frame_index()]._presentSemaphore;
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores = &_offscreenSemaphore;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit_info, nullptr));

	VkCommandBuffer cmd = _frames[get_current_frame_index()]._mainCommandBuffer;

	record_pospo_command_buffer(cmd, swapchainImageIndex);

	VkSubmitInfo pospo_submit_info = vkinit::submit_info(&cmd);

	VkPipelineStageFlags pospoWaitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

	pospo_submit_info.pWaitDstStageMask = pospoWaitStages;
	pospo_submit_info.waitSemaphoreCount = 1;
	pospo_submit_info.pWaitSemaphores = &_offscreenSemaphore;
	pospo_submit_info.signalSemaphoreCount = 1;
	pospo_submit_info.pSignalSemaphores = &_frames[get_current_frame_index()]._renderSemaphore;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &pospo_submit_info, _frames[get_current_frame_index()]._renderFence));

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.pSwapchains = &re->_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &_frames[get_current_frame_index()]._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));


	_frameNumber++;
}

void Renderer::draw_deferred(VkCommandBuffer cmd, int imageIndex)
{
	//SECOND PASS

	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	VkClearValue clearValue;
	float flash = abs(sin(_frameNumber / 120.0f));
	clearValue.color = { {0.0f, 0.0f, flash, 1.0f} };

	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.0f;

	VkRenderPassBeginInfo light_rpInfo = vkinit::renderpass_begin_info(re->_defaultRenderPass, re->_windowExtent, re->_framebuffers[imageIndex]);

	std::array<VkClearValue, 2> clearValues = { clearValue, depthClear };

	light_rpInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	light_rpInfo.pClearValues = clearValues.data();

	vkCmdBeginRenderPass(cmd, &light_rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_lightPipeline);

	int frameIndex = _frameNumber % FRAME_OVERLAP;
	uint32_t uniform_offset = vkutil::get_aligned_size(sizeof(GPUSceneData) * frameIndex, re->_gpuProperties.limits.minUniformBufferOffsetAlignment);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_lightPipelineLayout, 0, 1, &_frames[get_current_frame_index()].globalDescriptor, 1, &uniform_offset);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_lightPipelineLayout, 1, 1, &re->_gbuffersDescriptorSet, 0, nullptr);

	//deferred quad
	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &render_quad._vertexBuffer._buffer, &offset);
	vkCmdBindIndexBuffer(cmd, render_quad._indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(cmd, static_cast<uint32_t>(render_quad._indices.size()), 1, 0, 0, 0);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRenderPass(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));
}

FrameData& Renderer::get_current_frame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}

void Renderer::get_primitive_to_shader_info(const VKE::Node* parent, VKE::Node& node, const glm::mat4& model, std::vector<PrimitiveToShader>& primitivesInfo, std::vector<glm::mat4>& transforms, const int renderableIndex)
{
	if(node._children.size() > 0)
	{
		for(const auto& child : node._children)
		{
			get_primitive_to_shader_info(&node, *child, model, primitivesInfo, transforms, renderableIndex);
		}
	}

	if(node._mesh != nullptr && node._mesh->_primitives.size() > 0)
	{
		glm::mat4 global_matrix = model * node.get_global_matrix();

		for(const auto& primitive : node._mesh->_primitives)
		{
			PrimitiveToShader primitiveInfo{};
			primitiveInfo.firstIdx_rndIdx_matIdx_transIdx.x = primitive->firstIndex;
			primitiveInfo.firstIdx_rndIdx_matIdx_transIdx.y = renderableIndex;
			primitiveInfo.firstIdx_rndIdx_matIdx_transIdx.z = primitive->material._id;
			primitiveInfo.firstIdx_rndIdx_matIdx_transIdx.w = transforms.size();

			primitivesInfo.push_back(primitiveInfo);
		}

		transforms.emplace_back(global_matrix);
	}
}

void Renderer::get_nodes_transforms(const VKE::Node* parent, VKE::Node& node, const glm::mat4& model, std::vector<glm::mat4>& transforms)
{
	if(node._children.size() > 0)
	{
		for(const auto& child : node._children)
		{
			get_nodes_transforms(&node, *child, model, transforms);
		}
	}

	if(node._mesh != nullptr)
	{
		if(node._mesh->_primitives.size() > 0)
		{
			glm::mat4 globalMatrix = model * node.get_global_matrix();
			transforms.emplace_back(globalMatrix);
		}
	}
}