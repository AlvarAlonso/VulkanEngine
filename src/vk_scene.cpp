#include "vk_engine.h";
#include "vk_initializers.h"
#include <glm/gtx/transform.hpp>
#include <array>

Scene::Scene()
{
}

void Scene::generate_sample_scene()
{
	Texture* map_texture = &VulkanEngine::cinstance->_loadedTextures[0];
	Texture* default_texture = &VulkanEngine::cinstance->_loadedTextures[1];

	RenderObject map;
	map._mesh = VulkanEngine::cinstance->get_mesh("empire");
	map._albedoTexture = map_texture;
	map._material = &VulkanEngine::cinstance->_materials[0];
	map._model = glm::translate(glm::vec3{ 5, -10, 0 });
	
	Mesh* cube_mesh = new Mesh();
	cube_mesh->create_cube();

	RenderObject cube1;
	cube1._mesh = cube_mesh;
	cube1._albedoTexture = default_texture;
	cube1._material = &VulkanEngine::cinstance->_materials[1];
	cube1._model = glm::translate(glm::vec3{ 8, 30, 0 });

	RenderObject cube2;
	cube2._mesh = cube_mesh;
	cube2._albedoTexture = default_texture;
	cube2._material = &VulkanEngine::cinstance->_materials[0];
	cube2._model = glm::translate(glm::vec3{ 8, 30, 5 });

	RenderObject cube3;
	cube3._mesh = cube_mesh;
	cube3._albedoTexture = default_texture;
	cube3._material = &VulkanEngine::cinstance->_materials[0];
	cube3._model = glm::translate(glm::vec3{ 8, 30, 8 });

	RenderObject sphere1;
	sphere1._mesh = VulkanEngine::cinstance->get_mesh("sphere");
	sphere1._albedoTexture = default_texture;
	sphere1._material = &VulkanEngine::cinstance->_materials[1];
	sphere1._model = glm::translate(glm::vec3{ 5, 30, 0 });

	RenderObject sphere2;
	sphere2._mesh = VulkanEngine::cinstance->get_mesh("sphere");
	sphere2._albedoTexture = default_texture;
	sphere2._material = &VulkanEngine::cinstance->_materials[1];
	sphere2._model = glm::translate(glm::vec3{ 0, 30, 0 });

	RenderObject monkey;
	monkey._mesh = VulkanEngine::cinstance->get_mesh("monkey");
	monkey._albedoTexture = default_texture;
	monkey._material = &VulkanEngine::cinstance->_materials[2];
	monkey._model = glm::translate(glm::vec3{ 5, 30, 0 });

	Light point_light1;
	point_light1._position = glm::vec4(2.0f, 50.0f, 8.0f, 50.0f);
	point_light1._color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

	Light point_light2;
	point_light2._position = glm::vec4(-3.0f, 35.0f, 3.0f, 30.0f);
	point_light2._color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

	Light point_light3;
	point_light3._position = glm::vec4(-0.5f, 35.0f, -1.0f, 30.0f);
	point_light3._color = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

	//allocate the descriptor set for single-texture to use on the material
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.pNext = nullptr;
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = RenderEngine::_descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &RenderEngine::_singleTextureSetLayout;

	vkAllocateDescriptorSets(RenderEngine::_device, &allocInfo, &map_texture->descriptorSet);

	vkAllocateDescriptorSets(RenderEngine::_device, &allocInfo, &default_texture->descriptorSet);

	//write to the descriptor set so that it points to our empire_diffuse texture
	VkDescriptorImageInfo imageBufferInfo;
	imageBufferInfo.sampler = RenderEngine::_defaultSampler;
	imageBufferInfo.imageView = map_texture->imageView;;
	imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet texture1 = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, map_texture->descriptorSet, &imageBufferInfo, 0);

	vkUpdateDescriptorSets(RenderEngine::_device, 1, &texture1, 0, nullptr);

	imageBufferInfo.imageView = default_texture->imageView;

	VkWriteDescriptorSet texture2 = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, default_texture->descriptorSet, &imageBufferInfo, 0);

	vkUpdateDescriptorSets(RenderEngine::_device, 1, &texture2, 0, nullptr);

	
	_lights.push_back(point_light1);
	//_lights.push_back(point_light2);
	//_lights.push_back(point_light3);
	

	_renderables.push_back(map);
	_renderables.push_back(cube1);
	_renderables.push_back(cube2);
	_renderables.push_back(cube3);
	_renderables.push_back(sphere1);
	_renderables.push_back(sphere2);
	//_renderables.push_back(monkey);

	//renderables assigned to default material
	_matIndices.resize(_renderables.size());
	_matIndices[0] = 0;
	_matIndices[1] = 3;
	_matIndices[2] = 1;
	_matIndices[3] = 2;
	_matIndices[4] = 4;
	_matIndices[5] = 2;

	_texIndices.resize(_renderables.size());
	_texIndices[0] = 0;
	_texIndices[1] = 1;
	_texIndices[2] = 1;
	_texIndices[3] = 1;
	_texIndices[4] = 1;
	_texIndices[5] = 1;
}
