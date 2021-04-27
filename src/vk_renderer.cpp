#include "vk_renderer.h"
#include "vk_initializers.h"
#include "vk_engine.h"
#include "vk_material.h"
#include "vk_textures.h"
#include "vk_prefab.h"
#include "vk_utils.h"
#include "Camera.h"
#include <iostream>

#include <extra/imgui/imgui.h>
#include <extra/imgui/imgui_impl_sdl.h>
#include <extra/imgui/imgui_impl_vulkan.h>

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
	re->reset_imgui();

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
		re->reset_imgui();
	}

	_renderMode++;

	if(_renderMode == RENDER_MODE_RAYTRACING)
	{
		re->reset_imgui();
	}

	// TODO: More things to add to optimize resources (only create structures related to the current render mode).
}

void Renderer::init_renderer()
{
	_lightCamera = new Camera();
	//_lightCamera->setPerspective(60.0f, 1920.0f / 1080.0f, 0.1f, 512.0f);
	_lightCamera->setOrthographic(-128, 128, -128, 128, -500, 500);

	init_commands();
	init_sync_structures();
	create_descriptor_buffers();
	create_uniform_buffer();

	render_quad.create_quad();

	_rtPushConstant.frame_bias.y = SHADOW_BIAS;
	_rtPushConstant.flags.y = 1;
}

void Renderer::draw_scene()
{
	ImGui::Render();

	if(currentScene == nullptr)
	{
		std::cout << "ERROR: The current scene in the renderer is null. Set the scene from the engine before attempting to draw!" << std::endl;
		std::abort();
	}

	if(!isDeferredCommandInit)
	{
		re->create_raster_scene_structures();
		init_descriptors();
		record_skybox_command_buffer();
		if(currentScene->_lights.size() > 0)
		{
			_lightCamera->_position = currentScene->_lights[0]._model[3];
			_lightCamera->_direction = glm::vec3(0) - _lightCamera->_position;
		}
		else
		{
			std::cout << "[Warning]: No light set in the scene." << std::endl;
		}

		isDeferredCommandInit = true;
	}

	if(!areAccelerationStructuresInit)
	{
		re->create_raytracing_scene_structures(*currentScene);
		create_raytracing_descriptor_sets();
		areAccelerationStructuresInit = true;
	}

	render_raytracing();

	re->create_top_level_acceleration_structure(*currentScene, true);
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

void Renderer::update_uniform_buffers()
{
	//Camera Update
	//glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1700.0f / 900.0f, 0.1f, 512.0f);
	glm::mat4 projection = VulkanEngine::cinstance->camera->getProjection();
	//glm::mat4 projection = glm::ortho(-850, 850, -450, 450, -100, 1000);

	void* data;
	vmaMapMemory(_allocator, _ubo._allocation, &data);
	uniformData.projInverse = glm::inverse(projection);
	uniformData.viewInverse = glm::inverse(VulkanEngine::cinstance->camera->getView());
	uniformData.position = glm::vec4(VulkanEngine::cinstance->camera->_position, 1.0);
	memcpy(data, &uniformData, sizeof(uniformData));
	vmaUnmapMemory(_allocator, _ubo._allocation);

	// renderable models update
	std::vector<RenderObject>& renderables = currentScene->_renderables;
	std::vector<glm::mat4> transforms;

	void* transformData;
	vmaMapMemory(_allocator, _transformBuffer._allocation, &transformData);

	for(const auto& renderable : renderables)
	{
		for(const auto& node : renderable._prefab->_roots)
		{
			node->get_nodes_transforms(renderable._model, transforms);
		}
	}

	memcpy(transformData, transforms.data(), transforms.size() * sizeof(glm::mat4));

	vmaUnmapMemory(_allocator, _transformBuffer._allocation);
	
	// light models update

	std::vector<LightToShader> lightInfos;
	lightInfos.reserve(currentScene->_lights.size());

	// Pass Lights to LightToShader
	for (const auto& light : currentScene->_lights)
	{
		LightToShader lightInfo;
		lightInfo._position_dist = glm::vec4(glm::vec3(light._model[3]), light._maxDist);
		lightInfo._color_intensity = glm::vec4(light._color, light._intensity);

		if(light._type == DIRECTIONAL)
		{
			lightInfo._properties_type = glm::vec4(glm::vec3(light._targetPosition), light._type);
		}
		else
		{
			lightInfo._properties_type = glm::vec4(light._radius, 0.0, 0.0, light._type);
		}

		lightInfos.emplace_back(lightInfo);
	}

	void* sceneData;
	vmaMapMemory(_allocator, _sceneBuffer._allocation, &sceneData);
	memcpy(sceneData, lightInfos.data(), lightInfos.size() * sizeof(LightToShader));
	vmaUnmapMemory(_allocator, _sceneBuffer._allocation);

	// TODO: Solve memory leak
	//re->create_top_level_acceleration_structure(*currentScene, true);
}

void Renderer::create_raytracing_descriptor_sets()
{
	std::vector<RenderObject>& renderables = currentScene->_renderables;

	// RT SHARED DESCRIPTORS
	// Binding 0 : Acceleration Structure Descriptor
	VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo{};
	descriptorAccelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
	descriptorAccelerationStructureInfo.pAccelerationStructures = &re->_topLevelAS._handle;

	// Binding 1: Camera Descriptor
	VkDescriptorBufferInfo uboBufferDescriptor{};
	uboBufferDescriptor.offset = 0;
	uboBufferDescriptor.buffer = _ubo._buffer;
	uboBufferDescriptor.range = sizeof(uniformData);

	// Binding 2: G-BUFFERS
	// Position
	VkDescriptorImageInfo positionImageDescriptor{};
	positionImageDescriptor.imageView = re->_positionImage._view;
	positionImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	positionImageDescriptor.sampler = re->_defaultSampler;

	// Normal
	VkDescriptorImageInfo normalImageDescriptor{};
	normalImageDescriptor.imageView = re->_normalImage._view;
	normalImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	normalImageDescriptor.sampler = re->_defaultSampler;

	// Albedo
	VkDescriptorImageInfo albedoImageDescriptor{};
	albedoImageDescriptor.imageView = re->_albedoImage._view;
	albedoImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	albedoImageDescriptor.sampler = re->_defaultSampler;

	// Depth Buffer
	VkDescriptorImageInfo depthImageDescriptor{};
	depthImageDescriptor.imageView = re->_depthImage._view;
	depthImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	depthImageDescriptor.sampler = re->_defaultSampler;

	// Motion Vector
	VkDescriptorImageInfo motionImageDescriptor{};
	motionImageDescriptor.imageView = re->_motionVectorImage._view;
	motionImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	motionImageDescriptor.sampler = re->_defaultSampler;

	std::array<VkDescriptorImageInfo, 5> gbuffersImageInfos = 
	{ 
		positionImageDescriptor, 
		normalImageDescriptor, 
		albedoImageDescriptor, 
		depthImageDescriptor,
		motionImageDescriptor
	};
	
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

		// Binding 6: Primitives Descriptor
		for (const auto& node : renderables[i]._prefab->_roots)
		{
			node->get_primitive_to_shader_info(renderables[i]._model, primitivesInfo, transforms, i);
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
	_sceneBuffer = vkutil::create_buffer(_allocator, currentScene->_lights.size() * sizeof(LightToShader), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	VkDescriptorBufferInfo sceneBufferDescriptor{};
	sceneBufferDescriptor.offset = 0;
	sceneBufferDescriptor.buffer = _sceneBuffer._buffer;
	sceneBufferDescriptor.range = currentScene->_lights.size() * sizeof(LightToShader);

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
		_materialInfos[i]._color_type = material->_color;
		_materialInfos[i]._color_type.w = material->_type;
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

	// RT SHADOWS PASS DESCRIPTORS
	{
		VkDescriptorSetAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc_info.pNext = nullptr;
		alloc_info.descriptorPool = re->_rayTracingDescriptorPool;
		alloc_info.descriptorSetCount = 1;
		alloc_info.pSetLayouts = &re->_rtShadowsPipeline._setLayout;

		VK_CHECK(vkAllocateDescriptorSets(_device, &alloc_info, &_rtShadowsDescriptorSet));

		// Binding 10: Shadow Images
		std::vector<VkDescriptorImageInfo> shadowImageInfos;
		shadowImageInfos.reserve(re->_shadowImages.size());

		for (const auto& shadowImage : re->_shadowImages)
		{
			VkDescriptorImageInfo shadowDescriptor{};
			shadowDescriptor.sampler = VK_NULL_HANDLE;
			shadowDescriptor.imageView = shadowImage._view;
			shadowDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			shadowImageInfos.push_back(shadowDescriptor);
		}

		// Binding 11: Deep Shadow Image
		VkDescriptorImageInfo deepShadowDescriptor{};
		deepShadowDescriptor.sampler = re->_defaultSampler;
		deepShadowDescriptor.imageView = re->_deepShadowImage._view;
		deepShadowDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		// Binding 12: Deep Shadow Map Camera
		VkDescriptorBufferInfo deepShadowMapCameraDescriptor{};
		deepShadowMapCameraDescriptor.buffer = _lightCamBuffer._buffer;
		deepShadowMapCameraDescriptor.offset = 0;
		deepShadowMapCameraDescriptor.range = sizeof(GPUCameraData);

		//Descriptor Writes		

		//	Acceleration Structure Write
		VkWriteDescriptorSet accelerationStructureWrite{};
		accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		// The specialized acceleration structure descriptor has to be chained
		accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
		accelerationStructureWrite.dstSet = _rtShadowsDescriptorSet;
		accelerationStructureWrite.dstBinding = 0;
		accelerationStructureWrite.descriptorCount = 1;
		accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

		VkWriteDescriptorSet uniformBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _rtShadowsDescriptorSet, &uboBufferDescriptor, 1);
		VkWriteDescriptorSet gbuffersWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _rtShadowsDescriptorSet, gbuffersImageInfos.data(), 2, static_cast<uint32_t>(gbuffersImageInfos.size()));
		VkWriteDescriptorSet vertexBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rtShadowsDescriptorSet, verticesBufferInfos.data(), 3, renderables.size());
		VkWriteDescriptorSet indexBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rtShadowsDescriptorSet, indicesBufferInfos.data(), 4, renderables.size());
		VkWriteDescriptorSet transformBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rtShadowsDescriptorSet, &transformBufferInfo, 5);
		VkWriteDescriptorSet primitivesInfoWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rtShadowsDescriptorSet, &primitivesBufferDescriptor, 6);
		VkWriteDescriptorSet sceneBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _rtShadowsDescriptorSet, &sceneBufferDescriptor, 7);
		VkWriteDescriptorSet materialBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rtShadowsDescriptorSet, &materialBufferDescriptor, 8);
		VkWriteDescriptorSet textureImagesWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _rtShadowsDescriptorSet, textureImageInfos.data(), 9, textureImageInfos.size());
		VkWriteDescriptorSet shadowImagesWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _rtShadowsDescriptorSet, shadowImageInfos.data(), 10, static_cast<uint32_t>(shadowImageInfos.size()));
		VkWriteDescriptorSet deepShadowImagesWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _rtShadowsDescriptorSet, &deepShadowDescriptor, 11);
		VkWriteDescriptorSet deepShadowMapCamWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _rtShadowsDescriptorSet, &deepShadowMapCameraDescriptor, 12);

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			accelerationStructureWrite,
			uniformBufferWrite,
			gbuffersWrite,
			vertexBufferWrite,
			indexBufferWrite,
			transformBufferWrite,
			primitivesInfoWrite,
			sceneBufferWrite,
			materialBufferWrite,
			textureImagesWrite,
			shadowImagesWrite,
			deepShadowImagesWrite,
			deepShadowMapCamWrite
		};

		vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
	}

	// DENOISER PASS DESCRIPTORS
	{
		VkDescriptorSetAllocateInfo denoiser_set_alloc_info = {};
		denoiser_set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		denoiser_set_alloc_info.pNext = nullptr;
		denoiser_set_alloc_info.descriptorPool = re->_rayTracingDescriptorPool;
		denoiser_set_alloc_info.descriptorSetCount = 1;
		denoiser_set_alloc_info.pSetLayouts = &re->_denoiserSetLayout;

		VK_CHECK(vkAllocateDescriptorSets(_device, &denoiser_set_alloc_info, &_denoiserDescriptorSet));

		// Binding 0: Shadow Images
		std::vector<VkDescriptorImageInfo> shadowImageInfos;
		shadowImageInfos.resize(re->_shadowImages.size());

		VkDescriptorImageInfo shadowImageInfo = {};
		shadowImageInfo.sampler = VK_NULL_HANDLE;
		shadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		for (size_t i = 0; i < re->_shadowImages.size(); i++)
		{
			shadowImageInfo.imageView = re->_shadowImages[i]._view;
			shadowImageInfos[i] = shadowImageInfo;
		}

		// Binding 1: Denoised Shadow Images
		std::vector<VkDescriptorImageInfo> denoisedShadowImageInfos;
		denoisedShadowImageInfos.resize(re->_denoisedShadowImages.size());

		VkDescriptorImageInfo denoisedShadowImageInfo = {};
		denoisedShadowImageInfo.sampler = VK_NULL_HANDLE;
		denoisedShadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		for(size_t i = 0; i < re->_denoisedShadowImages.size(); i++)
		{
			denoisedShadowImageInfo.imageView = re->_denoisedShadowImages[i]._view;
			denoisedShadowImageInfos[i] = denoisedShadowImageInfo;
		}

		VkWriteDescriptorSet shadowImagesWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _denoiserDescriptorSet, shadowImageInfos.data(), 0, static_cast<uint32_t>(shadowImageInfos.size()));
		VkWriteDescriptorSet denoisedShadowImagesWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _denoiserDescriptorSet, denoisedShadowImageInfos.data(), 1, static_cast<uint32_t>(denoisedShadowImageInfos.size()));

		std::array<VkWriteDescriptorSet, 2> denoiser_set_writes = { shadowImagesWrite, denoisedShadowImagesWrite };

		vkUpdateDescriptorSets(_device, static_cast<uint32_t>(denoiser_set_writes.size()), denoiser_set_writes.data(), 0, VK_NULL_HANDLE);
	}

	// FINAL RT PASS DESCRIPTORS
	{
		VkDescriptorSetAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc_info.pNext = nullptr;
		alloc_info.descriptorPool = re->_rayTracingDescriptorPool;
		alloc_info.descriptorSetCount = 1;
		alloc_info.pSetLayouts = &re->_rtFinalPipeline._setLayout;

		VK_CHECK(vkAllocateDescriptorSets(_device, &alloc_info, &_rtFinalDescriptorSet));

		// Binding 10: Result Image Descriptor
		VkDescriptorImageInfo storageImageDescriptor{};
		storageImageDescriptor.imageView = re->_storageImageView;
		storageImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		
		// Binding 11: Denoised Shadow Images
		std::vector<VkDescriptorImageInfo> denoisedShadowImageInfos;
		denoisedShadowImageInfos.reserve(re->_denoisedShadowImages.size());

		for(const auto& shadowImage : re->_denoisedShadowImages)
		{
			VkDescriptorImageInfo denoisedShadowDescriptor{};
			denoisedShadowDescriptor.sampler = VK_NULL_HANDLE;
			denoisedShadowDescriptor.imageView = shadowImage._view;
			denoisedShadowDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			denoisedShadowImageInfos.push_back(denoisedShadowDescriptor);
		}

		// Binding 12: Skybox Image
		VkDescriptorImageInfo cubeMapInfo{};
		cubeMapInfo.sampler = re->_defaultSampler;
		cubeMapInfo.imageView = currentScene->_skybox._cubeMap->_imageView;
		cubeMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		//Descriptor Writes		

		// Acceleration Structure Write
		VkWriteDescriptorSet accelerationStructureWrite{};
		accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		// The specialized acceleration structure descriptor has to be chained
		accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
		accelerationStructureWrite.dstSet = _rtFinalDescriptorSet;
		accelerationStructureWrite.dstBinding = 0;
		accelerationStructureWrite.descriptorCount = 1;
		accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

		VkWriteDescriptorSet uniformBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _rtFinalDescriptorSet, &uboBufferDescriptor, 1);
		VkWriteDescriptorSet gbuffersWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _rtFinalDescriptorSet, gbuffersImageInfos.data(), 2, static_cast<uint32_t>(gbuffersImageInfos.size()));
		VkWriteDescriptorSet vertexBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rtFinalDescriptorSet, verticesBufferInfos.data(), 3, renderables.size());
		VkWriteDescriptorSet indexBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rtFinalDescriptorSet, indicesBufferInfos.data(), 4, renderables.size());
		VkWriteDescriptorSet transformBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rtFinalDescriptorSet, &transformBufferInfo, 5);
		VkWriteDescriptorSet primitivesInfoWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rtFinalDescriptorSet, &primitivesBufferDescriptor, 6);
		VkWriteDescriptorSet sceneBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _rtFinalDescriptorSet, &sceneBufferDescriptor, 7);
		VkWriteDescriptorSet materialBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rtFinalDescriptorSet, &materialBufferDescriptor, 8);
		VkWriteDescriptorSet textureImagesWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _rtFinalDescriptorSet, textureImageInfos.data(), 9, textureImageInfos.size());
		VkWriteDescriptorSet resultImageWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _rtFinalDescriptorSet, &storageImageDescriptor, 10);
		VkWriteDescriptorSet denoisedShadowImagesWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _rtFinalDescriptorSet, denoisedShadowImageInfos.data(), 11, static_cast<uint32_t>(denoisedShadowImageInfos.size()));
		VkWriteDescriptorSet cubeMapWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _rtFinalDescriptorSet, &cubeMapInfo, 12);

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			accelerationStructureWrite,
			uniformBufferWrite,
			gbuffersWrite,
			vertexBufferWrite,
			indexBufferWrite,
			transformBufferWrite,
			primitivesInfoWrite,
			sceneBufferWrite,
			materialBufferWrite,
			textureImagesWrite,		
			resultImageWrite,
			denoisedShadowImagesWrite,
			cubeMapWrite
		};

		vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
	}

	// POSPO PASS DESCRIPTORS
	VkDescriptorSetAllocateInfo pospo_alloc_info = {};
	pospo_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	pospo_alloc_info.pNext = nullptr;
	pospo_alloc_info.descriptorPool = re->_rayTracingDescriptorPool;
	pospo_alloc_info.descriptorSetCount = 1;
	pospo_alloc_info.pSetLayouts = &re->_singleTextureSetLayout;

	VK_CHECK(vkAllocateDescriptorSets(_device, &pospo_alloc_info, &re->pospo._textureSet));

	pospo_alloc_info.pSetLayouts = &re->_storageTextureSetLayout;

	VK_CHECK(vkAllocateDescriptorSets(_device, &pospo_alloc_info, &re->pospo._additionalTextureSet));

	VkDescriptorImageInfo pospoImageInfo = {};
	pospoImageInfo.sampler = re->_defaultSampler;
	pospoImageInfo.imageView = re->_storageImageView;
	pospoImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorImageInfo dsmImageInfo = {};
	dsmImageInfo.sampler = re->_defaultSampler;
	dsmImageInfo.imageView = re->_deepShadowImage._view;
	dsmImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkWriteDescriptorSet pospoWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, re->pospo._textureSet, &pospoImageInfo, 0);
	VkWriteDescriptorSet dsmWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, re->pospo._additionalTextureSet, &dsmImageInfo, 0);

	vkUpdateDescriptorSets(_device, 1, &pospoWrite, 0, VK_NULL_HANDLE);
	vkUpdateDescriptorSets(_device, 1, &dsmWrite, 0, VK_NULL_HANDLE);

	re->_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(_allocator, _transformBuffer._buffer, _transformBuffer._allocation);
		vmaDestroyBuffer(_allocator, _primitiveInfoBuffer._buffer, _primitiveInfoBuffer._allocation);
		vmaDestroyBuffer(_allocator, _sceneBuffer._buffer, _sceneBuffer._allocation);
		vmaDestroyBuffer(_allocator, _materialBuffer._buffer, _materialBuffer._allocation);
	});
}

void Renderer::record_deep_shadow_map_command_buffer(RenderObject* first, int count)
{
	VkCommandBufferBeginInfo dsmCmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

	VK_CHECK(vkBeginCommandBuffer(_dsmCommandBuffer, &dsmCmdBeginInfo));

	VkClearValue first_depthClear;
	first_depthClear.depthStencil.depth = 1.0f;

	VkExtent2D extent =
	{
		static_cast<uint32_t>(SHADOW_MAP_WIDTH),
		static_cast<uint32_t>(SHADOW_MAP_HEIGHT)
	};

	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(re->_singleAttachmentRenderPass, extent, re->_dsm_framebuffer);

	rpInfo.clearValueCount = 1;
	rpInfo.pClearValues = &first_depthClear;

	vkCmdBeginRenderPass(_dsmCommandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(_dsmCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_dsmPipeline);

	vkCmdBindDescriptorSets(_dsmCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_dsmPipelineLayout, 0, 1, &_lightCamDescriptorSet, 0, nullptr);

	vkCmdBindDescriptorSets(_dsmCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_dsmPipelineLayout, 1, 1, &_materialsDescriptorSet, 0, nullptr);

	vkCmdBindDescriptorSets(_dsmCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_dsmPipelineLayout, 2, 1, &_deepShadowMapDescriptorSet, 0, nullptr);

	VKE::Prefab* lastPrefab = nullptr;
	for (int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];

		if (object._prefab != lastPrefab)
		{
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(_dsmCommandBuffer, 0, 1, &object._prefab->_vertices.vertexBuffer._buffer, &offset);
			if (object._prefab->_indices.count > 0)
			{
				vkCmdBindIndexBuffer(_dsmCommandBuffer, object._prefab->_indices.indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);
			}
		}

		object._prefab->draw(object._model, _dsmCommandBuffer, re->_dsmPipelineLayout);
	}

	vkCmdEndRenderPass(_dsmCommandBuffer);

	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = re->_deepShadowImage._image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

		vkCmdPipelineBarrier(
			_dsmCommandBuffer,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);
	}

	VK_CHECK(vkEndCommandBuffer(_dsmCommandBuffer));
}

void Renderer::record_skybox_command_buffer()
{
	VkCommandBufferBeginInfo skyboxCmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

	VK_CHECK(vkBeginCommandBuffer(_skyboxCommandBuffer, &skyboxCmdBeginInfo));
	
	VkClearValue clearValue;
	clearValue.color = { {0.2f, 0.4f, 0.9f, 1.0f} };

	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(re->_skyboxRenderPass, re->_windowExtent, re->_skybox_framebuffer);
	rpInfo.clearValueCount = 1;
	rpInfo.pClearValues = &clearValue;

	vkCmdBeginRenderPass(_skyboxCommandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(_skyboxCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_skyboxPipeline);

	vkCmdBindDescriptorSets(_skyboxCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_skyboxPipelineLayout, 0, 1, &_skyboxDescriptorSet, 0, nullptr);

	Skybox& skybox = currentScene->_skybox;

	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(_skyboxCommandBuffer, 0, 1, &skybox._renderable->_prefab->_vertices.vertexBuffer._buffer, &offset);

	vkCmdBindIndexBuffer(_skyboxCommandBuffer, skybox._renderable->_prefab->_indices.indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(_skyboxCommandBuffer, skybox._renderable->_prefab->_indices.count, 1, 0, 0, 0);

	vkCmdEndRenderPass(_skyboxCommandBuffer);
	
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = re->_albedoImage._image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

	vkCmdPipelineBarrier(
		_skyboxCommandBuffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier
	);

	VK_CHECK(vkEndCommandBuffer(_skyboxCommandBuffer));
}

void Renderer::record_gbuffers_command_buffers(RenderObject* first, int count)
{
	VkCommandBufferBeginInfo gbuffersCmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

	VK_CHECK(vkBeginCommandBuffer(_gbuffersCommandBuffer, &gbuffersCmdBeginInfo));

	VkClearValue first_clearValue;
	first_clearValue.color = { {0.2f, 0.2f, 0.2f, 1.0f} };

	VkClearValue first_depthClear;
	first_depthClear.depthStencil.depth = 1.0f;

	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(re->_gbuffersRenderPass, re->_windowExtent, re->_offscreen_framebuffer);

	std::array<VkClearValue, 5> first_clearValues = { first_clearValue, first_clearValue, first_clearValue, first_clearValue, first_depthClear };

	rpInfo.clearValueCount = static_cast<uint32_t>(first_clearValues.size());
	rpInfo.pClearValues = first_clearValues.data();

	vkCmdBeginRenderPass(_gbuffersCommandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(_gbuffersCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_gbuffersPipeline);

	vkCmdBindDescriptorSets(_gbuffersCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_gbuffersPipelineLayout, 0, 1, &_camDescriptorSet, 0, nullptr);

	vkCmdBindDescriptorSets(_gbuffersCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_gbuffersPipelineLayout, 1, 1, &_materialsDescriptorSet, 0, nullptr);

	VKE::Prefab* lastPrefab = nullptr;
	for (int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];

		if (object._prefab != lastPrefab)
		{
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(_gbuffersCommandBuffer, 0, 1, &object._prefab->_vertices.vertexBuffer._buffer, &offset);
			if (object._prefab->_indices.count > 0)
			{
				vkCmdBindIndexBuffer(_gbuffersCommandBuffer, object._prefab->_indices.indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);
			}
		}

		object._prefab->draw(object._model, _gbuffersCommandBuffer, re->_gbuffersPipelineLayout);
	}

	vkCmdEndRenderPass(_gbuffersCommandBuffer);

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = re->_depthImage._image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

	vkCmdPipelineBarrier(
		_gbuffersCommandBuffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier
	);

	VK_CHECK(vkEndCommandBuffer(_gbuffersCommandBuffer));
}

void Renderer::record_rtShadows_command_buffer()
{
	VkCommandBuffer cmd = _rtShadowsCommandBuffer;

	VkCommandBufferBeginInfo cmdBufInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

	VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBufInfo));

	/*
		Setup the buffer regions pointing to the shaders in our shader binding table
	*/

	const uint32_t handleSizeAligned = vkutil::get_aligned_size(re->_rayTracingPipelineProperties.shaderGroupHandleSize, re->_rayTracingPipelineProperties.shaderGroupHandleAlignment);

	VkBufferDeviceAddressInfoKHR raygenDeviceAddressInfo{};
	raygenDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	raygenDeviceAddressInfo.buffer = re->_rtShadowsPipeline._raygenShaderBindingTable._buffer;

	VkStridedDeviceAddressRegionKHR raygenShaderSbtEntry{};
	raygenShaderSbtEntry.deviceAddress = vkGetBufferDeviceAddress(_device, &raygenDeviceAddressInfo);
	raygenShaderSbtEntry.stride = handleSizeAligned;
	raygenShaderSbtEntry.size = handleSizeAligned;

	VkBufferDeviceAddressInfoKHR missDeviceAddressInfo{};
	missDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	missDeviceAddressInfo.buffer = re->_rtShadowsPipeline._missShaderBindingTable._buffer;

	VkStridedDeviceAddressRegionKHR missShaderSbtEntry{};
	missShaderSbtEntry.deviceAddress = vkGetBufferDeviceAddress(_device, &missDeviceAddressInfo);
	missShaderSbtEntry.stride = handleSizeAligned;
	missShaderSbtEntry.size = handleSizeAligned;

	VkBufferDeviceAddressInfoKHR hitDeviceAddressInfo{};
	hitDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	hitDeviceAddressInfo.buffer = re->_rtShadowsPipeline._hitShaderBindingTable._buffer;

	VkStridedDeviceAddressRegionKHR hitShaderSbtEntry{};
	hitShaderSbtEntry.deviceAddress = vkGetBufferDeviceAddress(_device, &hitDeviceAddressInfo);
	hitShaderSbtEntry.stride = handleSizeAligned;
	hitShaderSbtEntry.size = handleSizeAligned;

	VkStridedDeviceAddressRegionKHR callableShaderSbtEntry{};

	/*
		Dispatch the ray tracing commands
	*/

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, re->_rtShadowsPipeline._pipeline);
	vkCmdPushConstants(cmd, re->_rtFinalPipeline._layout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(RtPushConstant), &_rtPushConstant);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, re->_rtShadowsPipeline._layout, 0, 1, &_rtShadowsDescriptorSet, 0, 0);

	re->vkCmdTraceRaysKHR(
		cmd,
		&raygenShaderSbtEntry,
		&missShaderSbtEntry,
		&hitShaderSbtEntry,
		&callableShaderSbtEntry,
		re->_windowExtent.width,
		re->_windowExtent.height,
		1);

	VkMemoryBarrier memoryBarrier = {};
	memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0,
		1, &memoryBarrier,
		0, nullptr,
		0, nullptr);

	// Bind the compute shader pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, re->_denoiserPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, re->_denoiserPipelineLayout, 0, 1, &_denoiserDescriptorSet, 0, nullptr);
	// Run the compute shader with enough workgroups to cover the entire buffer:
	vkCmdDispatch(cmd, (uint32_t(re->_windowExtent.width) + re->workgroup_width - 1) / re->workgroup_width,
		(uint32_t(re->_windowExtent.height) + re->workgroup_height - 1) / re->workgroup_height, 1);

	VK_CHECK(vkEndCommandBuffer(cmd));
}

void Renderer::record_rtFinal_command_buffer()
{
	VkCommandBuffer cmd = _rtFinalCommandBuffer;

	VkCommandBufferBeginInfo cmdBufInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

	VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBufInfo));
	
	/*
		Setup the buffer regions pointing to the shaders in our shader binding table
	*/

	const uint32_t handleSizeAligned = vkutil::get_aligned_size(re->_rayTracingPipelineProperties.shaderGroupHandleSize, re->_rayTracingPipelineProperties.shaderGroupHandleAlignment);

	VkBufferDeviceAddressInfoKHR raygenDeviceAddressInfo{};
	raygenDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	raygenDeviceAddressInfo.buffer = re->_rtFinalPipeline._raygenShaderBindingTable._buffer;

	VkStridedDeviceAddressRegionKHR raygenShaderSbtEntry{};
	raygenShaderSbtEntry.deviceAddress = vkGetBufferDeviceAddress(_device, &raygenDeviceAddressInfo);
	raygenShaderSbtEntry.stride = handleSizeAligned;
	raygenShaderSbtEntry.size = handleSizeAligned;

	VkBufferDeviceAddressInfoKHR missDeviceAddressInfo{};
	missDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	missDeviceAddressInfo.buffer = re->_rtFinalPipeline._missShaderBindingTable._buffer;

	VkStridedDeviceAddressRegionKHR missShaderSbtEntry{};
	missShaderSbtEntry.deviceAddress = vkGetBufferDeviceAddress(_device, &missDeviceAddressInfo);
	missShaderSbtEntry.stride = handleSizeAligned;
	missShaderSbtEntry.size = handleSizeAligned;

	VkBufferDeviceAddressInfoKHR hitDeviceAddressInfo{};
	hitDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	hitDeviceAddressInfo.buffer = re->_rtFinalPipeline._hitShaderBindingTable._buffer;

	VkStridedDeviceAddressRegionKHR hitShaderSbtEntry{};
	hitShaderSbtEntry.deviceAddress = vkGetBufferDeviceAddress(_device, &hitDeviceAddressInfo);
	hitShaderSbtEntry.stride = handleSizeAligned;
	hitShaderSbtEntry.size = handleSizeAligned;

	VkStridedDeviceAddressRegionKHR callableShaderSbtEntry{};

	/*
		Dispatch the ray tracing commands
	*/
	
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, re->_rtFinalPipeline._pipeline);
	vkCmdPushConstants(cmd, re->_rtFinalPipeline._layout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(RtPushConstant), &_rtPushConstant);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, re->_rtFinalPipeline._layout, 0, 1, &_rtFinalDescriptorSet, 0, 0);
	
	re->vkCmdTraceRaysKHR(
		cmd,
		&raygenShaderSbtEntry,
		&missShaderSbtEntry,
		&hitShaderSbtEntry,
		&callableShaderSbtEntry,
		re->_windowExtent.width,
		re->_windowExtent.height,
		1);

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

	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = re->_deepShadowImage._image;
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
	}
	
	VK_CHECK(vkEndCommandBuffer(cmd));
}

void Renderer::record_pospo_command_buffer(VkCommandBuffer cmd, uint32_t swapchainImageIndex)
{
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	VkClearValue clearValue = {0.2f, 0.2f, 0.2f, 1.0f};

	VkRenderPassBeginInfo pospo_begin_info = vkinit::renderpass_begin_info(re->pospo._renderPass, re->_windowExtent, re->pospo._framebuffers[swapchainImageIndex]);
	pospo_begin_info.clearValueCount = 1;
	pospo_begin_info.pClearValues = &clearValue;

	vkCmdBeginRenderPass(cmd, &pospo_begin_info, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, re->pospo._pipeline);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, re->pospo._pipelineLayout, 0, 1, &re->pospo._textureSet, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, re->pospo._pipelineLayout, 1, 1, &re->pospo._additionalTextureSet, 0, nullptr);

	vkCmdPushConstants(cmd, re->pospo._pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(FlagsPushConstant), &_shaderFlags);

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
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_dsmCommandBuffer));
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_skyboxCommandBuffer));
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_gbuffersCommandBuffer));

	//raytracing command buffer
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_rtShadowsCommandBuffer));
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_rtFinalCommandBuffer));

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

	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_dsmSemaphore));
	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_skyboxSemaphore));
	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_gbufferSemaphore));
	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_rtShadowsSemaphore));
	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_rtFinalSemaphore));

	re->_mainDeletionQueue.push_function([=]() {
		vkDestroySemaphore(_device, _gbufferSemaphore, nullptr);
		vkDestroySemaphore(_device, _rtShadowsSemaphore, nullptr);
		vkDestroySemaphore(_device, _rtFinalSemaphore, nullptr);
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

	}

	//cam buffer
	_camBuffer = vkutil::create_buffer(_allocator, sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	_lightCamBuffer = vkutil::create_buffer(_allocator, sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	//deferred
	_objectBuffer = vkutil::create_buffer(_allocator, sizeof(VKE::MaterialToShader) * MAX_MATERIALS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
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

	// Skybox descriptors

	VkDescriptorSetAllocateInfo skyboxSetAllocInfo = {};
	skyboxSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	skyboxSetAllocInfo.pNext = nullptr;
	skyboxSetAllocInfo.descriptorPool = re->_descriptorPool;
	skyboxSetAllocInfo.descriptorSetCount = 1;
	skyboxSetAllocInfo.pSetLayouts = &re->_skyboxSetLayout;

	VK_CHECK(vkAllocateDescriptorSets(_device, &skyboxSetAllocInfo, &_skyboxDescriptorSet));

	VkDescriptorBufferInfo camBufferInfo = {};
	camBufferInfo.buffer = _camBuffer._buffer;
	camBufferInfo.offset = 0;
	camBufferInfo.range = sizeof(GPUCameraData);

	VkDescriptorImageInfo cubeMapInfo = {};
	cubeMapInfo.sampler = re->_defaultSampler;
	cubeMapInfo.imageView = currentScene->_skybox._cubeMap->_imageView;
	cubeMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet camSkyWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _skyboxDescriptorSet, &camBufferInfo, 0);
	VkWriteDescriptorSet cubeMapWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _skyboxDescriptorSet, &cubeMapInfo, 1);

	std::array<VkWriteDescriptorSet, 2> skyboxWrites = { camSkyWrite, cubeMapWrite };

	vkUpdateDescriptorSets(_device, static_cast<uint32_t>(skyboxWrites.size()), skyboxWrites.data(), 0, nullptr);

	//DEFERRED DESCRIPTORS
	// Camera
	VkDescriptorSetAllocateInfo cameraSetAllocInfo = {};
	cameraSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	cameraSetAllocInfo.pNext = nullptr;
	cameraSetAllocInfo.descriptorPool = re->_descriptorPool;
	cameraSetAllocInfo.descriptorSetCount = 1;
	cameraSetAllocInfo.pSetLayouts = &re->_camSetLayout;

	VK_CHECK(vkAllocateDescriptorSets(_device, &cameraSetAllocInfo, &_camDescriptorSet));

	// Object materials
	VkDescriptorSetAllocateInfo objectSetAlloc = {};
	objectSetAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	objectSetAlloc.pNext = nullptr;
	objectSetAlloc.descriptorPool = re->_descriptorPool;
	objectSetAlloc.descriptorSetCount = 1;
	objectSetAlloc.pSetLayouts = &re->_materialsSetLayout;

	VK_CHECK(vkAllocateDescriptorSets(_device, &objectSetAlloc, &_materialsDescriptorSet));

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
		_materialInfos[i]._color_type = material->_color;
		_materialInfos[i]._color_type.w = material->_type;
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

	// Deep shadow map descriptors

	VkDescriptorSetAllocateInfo dsmSetAllocInfo = {};
	dsmSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	dsmSetAllocInfo.pNext = nullptr;
	dsmSetAllocInfo.descriptorPool = re->_descriptorPool;
	dsmSetAllocInfo.descriptorSetCount = 1;
	dsmSetAllocInfo.pSetLayouts = &re->_camSetLayout;

	VK_CHECK(vkAllocateDescriptorSets(_device, &dsmSetAllocInfo, &_lightCamDescriptorSet));

	VkDescriptorBufferInfo lightCamBufferInfo = {};
	lightCamBufferInfo.buffer = _lightCamBuffer._buffer;
	lightCamBufferInfo.offset = 0;
	lightCamBufferInfo.range = sizeof(GPUCameraData);

	dsmSetAllocInfo.pSetLayouts = &re->_storageTextureSetLayout;

	VK_CHECK(vkAllocateDescriptorSets(_device, &dsmSetAllocInfo, &_deepShadowMapDescriptorSet));

	VkDescriptorImageInfo dsmImageInfo{};
	dsmImageInfo.imageView = re->_deepShadowImage._view;
	dsmImageInfo.sampler = VK_NULL_HANDLE;
	dsmImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkWriteDescriptorSet lightCamWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _lightCamDescriptorSet, &lightCamBufferInfo, 0);
	VkWriteDescriptorSet dsmWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _deepShadowMapDescriptorSet, &dsmImageInfo, 0);

	vkUpdateDescriptorSets(_device, 1, &lightCamWrite, 0, nullptr);
	vkUpdateDescriptorSets(_device, 1, &dsmWrite, 0, nullptr);
}

// Update descriptors for deferred
void Renderer::update_descriptors(RenderObject* first, size_t count)
{
	_lightCamera->_position = currentScene->_lights[0]._model[3];
	_lightCamera->_direction = glm::vec3(0) - _lightCamera->_position;

	glm::mat4 projection = VulkanEngine::cinstance->camera->getProjection();

	GPUCameraData camData;
	camData.projection = projection;
	camData.view = VulkanEngine::cinstance->camera->getView();
	camData.viewproj = projection * camData.view;
	camData.viewproj_lastFrame = _lastFrame_viewProj;

	_lastFrame_viewProj = camData.viewproj;

	void* data2;
	vmaMapMemory(_allocator, _camBuffer._allocation, &data2);
	memcpy(data2, &camData, sizeof(GPUCameraData));
	vmaUnmapMemory(_allocator, _camBuffer._allocation);

	camData.projection = _lightCamera->getProjection();
	camData.view = _lightCamera->getView();
	camData.viewproj = camData.projection * camData.view;

	void* data3;
	vmaMapMemory(_allocator, _lightCamBuffer._allocation, &data3);
	memcpy(data3, &camData, sizeof(GPUCameraData));
	vmaUnmapMemory(_allocator, _lightCamBuffer._allocation);

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

//raytracing
void Renderer::render_raytracing()
{
	VK_CHECK(vkWaitForFences(_device, 1, &_frames[get_current_frame_index()]._renderFence, true, UINT64_MAX));
	VK_CHECK(vkResetFences(_device, 1, &_frames[get_current_frame_index()]._renderFence));

	//request image from the swapchain
	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_device, re->_swapchain, 0, _frames[get_current_frame_index()]._presentSemaphore, nullptr, &swapchainImageIndex));

	update_frame();
	update_uniform_buffers();
	update_descriptors(currentScene->_renderables.data(), currentScene->_renderables.size());

	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

	// DEEP SHADOW MAPS PASS
	record_deep_shadow_map_command_buffer(currentScene->_renderables.data(), currentScene->_renderables.size());

	VkSubmitInfo submit_info_dsm_pass = vkinit::submit_info(&_dsmCommandBuffer);
	submit_info_dsm_pass.pWaitDstStageMask = waitStages;
	submit_info_dsm_pass.waitSemaphoreCount = 1;
	submit_info_dsm_pass.pWaitSemaphores = &_frames[get_current_frame_index()]._presentSemaphore;
	submit_info_dsm_pass.signalSemaphoreCount = 1;
	submit_info_dsm_pass.pSignalSemaphores = &_dsmSemaphore;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit_info_dsm_pass, nullptr));

	// SKYBOX PASS

	VkSubmitInfo submit_info_skybox_pass = vkinit::submit_info(&_skyboxCommandBuffer);
	submit_info_skybox_pass.pWaitDstStageMask = waitStages;
	submit_info_skybox_pass.waitSemaphoreCount = 1;
	submit_info_skybox_pass.pWaitSemaphores = &_dsmSemaphore;
	submit_info_skybox_pass.signalSemaphoreCount = 1;
	submit_info_skybox_pass.pSignalSemaphores = &_skyboxSemaphore;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit_info_skybox_pass, nullptr));

	// G-BUFFER PASS
	record_gbuffers_command_buffers(currentScene->_renderables.data(), currentScene->_renderables.size());

	VkSubmitInfo submit_info_gbuffer_pass = vkinit::submit_info(&_gbuffersCommandBuffer);
	submit_info_gbuffer_pass.pWaitDstStageMask = waitStages;
	submit_info_gbuffer_pass.waitSemaphoreCount = 1;
	submit_info_gbuffer_pass.pWaitSemaphores = &_skyboxSemaphore;
	submit_info_gbuffer_pass.signalSemaphoreCount = 1;
	submit_info_gbuffer_pass.pSignalSemaphores = &_gbufferSemaphore;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit_info_gbuffer_pass, nullptr));

	// RT SHADOWS & DENOISING PASS
	record_rtShadows_command_buffer();

	VkSubmitInfo submit_info_rtShadows_pass = vkinit::submit_info(&_rtShadowsCommandBuffer);
	submit_info_rtShadows_pass.pWaitDstStageMask = waitStages;
	submit_info_rtShadows_pass.waitSemaphoreCount = 1;
	submit_info_rtShadows_pass.pWaitSemaphores = &_gbufferSemaphore;
	submit_info_rtShadows_pass.signalSemaphoreCount = 1;
	submit_info_rtShadows_pass.pSignalSemaphores = &_rtShadowsSemaphore;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit_info_rtShadows_pass, nullptr));

	// RT PASS
	record_rtFinal_command_buffer();

	VkSubmitInfo submit_info_rtFinal_pass = vkinit::submit_info(&_rtFinalCommandBuffer);
	submit_info_rtFinal_pass.pWaitDstStageMask = waitStages;
	submit_info_rtFinal_pass.waitSemaphoreCount = 1;
	submit_info_rtFinal_pass.pWaitSemaphores = &_rtShadowsSemaphore;
	submit_info_rtFinal_pass.signalSemaphoreCount = 1;
	submit_info_rtFinal_pass.pSignalSemaphores = &_rtFinalSemaphore;

	//submit_info_rtFinal_pass.pWaitSemaphores = &_gbufferSemaphore;
	//submit_info_rtFinal_pass.pSignalSemaphores = &_rtFinalSemaphore;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit_info_rtFinal_pass, nullptr));

	// POSTPROCESSING PASS
	VkCommandBuffer cmd = _frames[get_current_frame_index()]._mainCommandBuffer;

	record_pospo_command_buffer(cmd, swapchainImageIndex);

	VkSubmitInfo pospo_submit_info = vkinit::submit_info(&cmd);

	VkPipelineStageFlags pospoWaitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

	pospo_submit_info.pWaitDstStageMask = pospoWaitStages;
	pospo_submit_info.waitSemaphoreCount = 1;
	pospo_submit_info.pWaitSemaphores = &_rtFinalSemaphore;
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

FrameData& Renderer::get_current_frame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}

void Renderer::reset_frame()
{
	_rtPushConstant.frame_bias.x = -1.0f;
}

void Renderer::update_frame()
{
	static glm::mat4 refCamMatrix;

	const auto& m = VulkanEngine::cinstance->camera->getView();

	if (memcmp(&refCamMatrix, &m, sizeof(glm::mat4)) != 0)
	{
		reset_frame();
		refCamMatrix = m;
	}
	_rtPushConstant.frame_bias.x++;
}