#include "vk_engine.h";
#include "vk_initializers.h"
#include <glm/gtx/transform.hpp>
#include <array>

using namespace VKE;

Scene::Scene()
{
}

void Scene::generate_sample_scene()
{	
	RenderObject tree;
	tree._prefab = Prefab::get("../assets/MapleTree.obj");
	tree._model = glm::translate(glm::vec3{ 0, 0, 0 });

	Mesh* meshGround = new Mesh();
	meshGround->create_quad();

	Prefab* groundPrefab = new Prefab(*meshGround);
	groundPrefab->register_prefab("ground");

	RenderObject ground;
	ground._prefab = Prefab::sPrefabsLoaded["ground"];
	ground._model = glm::translate(glm::vec3{ 0, 0, 0 });
	ground._model = glm::rotate(ground._model, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	ground._model = glm::scale(ground._model, glm::vec3(100.0f, 100.0f, 1.0f));

	Light point_light1;
	point_light1._position = glm::vec4(5.0f, 30.0f, 20.0f, 300.0f);
	point_light1._color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	
	// TODO: Create texture descriptor sets while loading the gltf or textures from disk
	/*
	//allocate the descriptor set for single-texture to use on the material
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.pNext = nullptr;
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = RenderEngine::_descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &RenderEngine::_singleTextureSetLayout;

	vkAllocateDescriptorSets(RenderEngine::_device, &allocInfo, &grass_texture->descriptorSet);

	vkAllocateDescriptorSets(RenderEngine::_device, &allocInfo, &default_texture->descriptorSet);

	//write to the descriptor set so that it points to our empire_diffuse texture
	VkDescriptorImageInfo imageBufferInfo;
	imageBufferInfo.sampler = RenderEngine::_defaultSampler;
	imageBufferInfo.imageView = grass_texture->imageView;;
	imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet texture1 = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, grass_texture->descriptorSet, &imageBufferInfo, 0);

	vkUpdateDescriptorSets(RenderEngine::_device, 1, &texture1, 0, nullptr);

	imageBufferInfo.imageView = default_texture->imageView;

	VkWriteDescriptorSet texture2 = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, default_texture->descriptorSet, &imageBufferInfo, 0);

	vkUpdateDescriptorSets(RenderEngine::_device, 1, &texture2, 0, nullptr);
	*/
	_lights.push_back(point_light1);

	//_renderables.push_back(tree);
	//_renderables.push_back(tree_leaves);
	//_renderables.push_back(tree_stem);
	_renderables.push_back(ground);
	
}                                
