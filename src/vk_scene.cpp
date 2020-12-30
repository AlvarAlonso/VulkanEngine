#include "vk_engine.h";
#include "vk_initializers.h"
#include <glm/gtx/transform.hpp>
#include <array>

Scene::Scene()
{
}

void Scene::generate_sample_scene()
{
	RenderObject map;
	map._mesh = VulkanEngine::cinstance->get_mesh("empire");
	map._material = VulkanEngine::cinstance->get_material("texturedmesh");
	map._model = glm::translate(glm::vec3{ 5, -10, 0 });

	_renderables.push_back(map);

	RenderObject monkey;
	monkey._mesh = VulkanEngine::cinstance->get_mesh("monkey");
	monkey._material = VulkanEngine::cinstance->get_material("untexturedmesh");
	monkey._model = glm::mat4(1.0f);

	_renderables.push_back(monkey);


	Material* texMaterial = VulkanEngine::cinstance->get_material("texturedmesh");
	Material* untexMaterial = VulkanEngine::cinstance->get_material("untexturedmesh");

	//allocate the descriptor set for single-texture to use on the material
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.pNext = nullptr;
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = VulkanEngine::cinstance->_descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &VulkanEngine::cinstance->_singleTextureSetLayout;

	vkAllocateDescriptorSets(VulkanEngine::cinstance->_device, &allocInfo, &texMaterial->albedoTexture);

	VkDescriptorSetAllocateInfo allocInfo2 = {};
	allocInfo2.pNext = nullptr;
	allocInfo2.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo2.descriptorPool = VulkanEngine::cinstance->_descriptorPool;
	allocInfo2.descriptorSetCount = 1;
	allocInfo2.pSetLayouts = &VulkanEngine::cinstance->_singleTextureSetLayout;

	vkAllocateDescriptorSets(VulkanEngine::cinstance->_device, &allocInfo, &untexMaterial->albedoTexture);

	//write to the descriptor set so that it points to our empire_diffuse texture
	VkDescriptorImageInfo imageBufferInfo;
	imageBufferInfo.sampler = VulkanEngine::cinstance->_defaultSampler;
	imageBufferInfo.imageView = VulkanEngine::cinstance->_loadedTextures["empire_diffuse"].imageView;
	imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet texture1 = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, texMaterial->albedoTexture, &imageBufferInfo, 0);

	vkUpdateDescriptorSets(VulkanEngine::cinstance->_device, 1, &texture1, 0, nullptr);

	imageBufferInfo.imageView = VulkanEngine::cinstance->_loadedTextures["default"].imageView;

	VkWriteDescriptorSet texture2 = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, untexMaterial->albedoTexture, &imageBufferInfo, 0);

	vkUpdateDescriptorSets(VulkanEngine::cinstance->_device, 1, &texture2, 0, nullptr);
}
