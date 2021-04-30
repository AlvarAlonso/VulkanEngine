#include "vk_engine.h";
#include "vk_initializers.h"
#include <glm/gtx/transform.hpp>
#include <array>
#include "vk_material.h"
#include "vk_textures.h"

using namespace VKE;

Scene::Scene()
{
}

void Scene::generate_sample_scene()
{	
	// TODO raytracing must accept glTFs with no indices
	
	// TODO: Rework renderables created on code to work with raytracing
	
	VKE::Material* mapleStemMaterial = new VKE::Material(Texture::get("maple_bark"));
	mapleStemMaterial->_type = VKE::DIFFUSE;
	mapleStemMaterial->_id = VKE::Material::sMaterials.size();
	mapleStemMaterial->_color = glm::vec4{ 1.0, 1.0, 1.0, 1.0 };
	mapleStemMaterial->register_material("maple_stem");

	VKE::Mesh* mapleStemMesh = VKE::Mesh::get("../assets/vegetation/maple/MapleTreeStem.obj");
	Prefab* mapleStemPrefab = new VKE::Prefab(*mapleStemMesh, "maple_stem");

	VKE::Material* mapleLeavesMaterial = new VKE::Material(Texture::get("maple_leaf"));
	mapleLeavesMaterial->_type = VKE::REFRACTIVE;
	mapleLeavesMaterial->_occlusion_texture = Texture::get("maple_leaf_occlusion");
	mapleLeavesMaterial->_id = VKE::Material::sMaterials.size();
	mapleLeavesMaterial->_color = glm::vec4{ 1.0, 1.0, 1.0, 0.5 };
	mapleLeavesMaterial->register_material("maple_leaves");

	VKE::Mesh* mapleLeavesMesh = VKE::Mesh::get("../assets/vegetation/maple/MapleTreeLeaves.obj");
	Prefab* mapleLeavesPrefab = new VKE::Prefab(*mapleLeavesMesh, "maple_leaves");

	//VKE::Mesh* treesMesh = VKE::Mesh::get("../assets/vegetation/trees9.obj");
	//Prefab* treesMeshPrefab = new VKE::Prefab(*treesMesh, "maple_stem");

	// Trees render objects definitions

	RenderObject* treeStem = new RenderObject();
	treeStem->_model = glm::translate(glm::vec3{ 0, 0, 0 });
	treeStem->_prefab = mapleStemPrefab;

	RenderObject* treeLeaves = new RenderObject();
	treeLeaves->_model = glm::translate(glm::vec3{ 0, 0, 0 });
	treeLeaves->_prefab = mapleLeavesPrefab;

	_renderables.push_back(*treeStem);
	_renderables.push_back(*treeLeaves);
	
	treeStem->_model = glm::translate(glm::vec3{ 10, 0, 10 });
	treeLeaves->_model = glm::translate(glm::vec3{ 10, 0, 10 });

	_renderables.push_back(*treeStem);
	_renderables.push_back(*treeLeaves);

	treeStem->_model = glm::translate(glm::vec3{ -10, 0, 10 });
	treeLeaves->_model = glm::translate(glm::vec3{ -10, 0, 10 });

	_renderables.push_back(*treeStem);
	_renderables.push_back(*treeLeaves);

	treeStem->_model = glm::translate(glm::vec3{ 10, 0, -10 });
	treeLeaves->_model = glm::translate(glm::vec3{ 10, 0, -10 });

	_renderables.push_back(*treeStem);
	_renderables.push_back(*treeLeaves);

	treeStem->_model = glm::translate(glm::vec3{ 20, 0, 10 });
	treeLeaves->_model = glm::translate(glm::vec3{ 20, 0, 10 });

	_renderables.push_back(*treeStem);
	_renderables.push_back(*treeLeaves);
	

	VKE::Material* grassMaterial = new VKE::Material(Texture::get("grass"));
	grassMaterial->_tilling_factor = 10.0f;
	grassMaterial->_id = VKE::Material::sMaterials.size();
	grassMaterial->_color = glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
	grassMaterial->register_material("grass");
	
	VKE::Mesh* planeMesh = new VKE::Mesh();
	planeMesh->create_quad();

	Prefab* planePrefab = new VKE::Prefab(*planeMesh, "grass");

	RenderObject* plane = new RenderObject();
	plane->_model = glm::translate(glm::vec3{ 0.0, 0.0, 0.0 });
	plane->_model = glm::rotate(plane->_model, glm::radians(-90.0f), glm::vec3{ 1, 0, 0 });
	plane->_model *= glm::scale(glm::mat4(1), glm::vec3{ 500, 500, 1 });
	plane->_prefab = planePrefab;

	_renderables.push_back(*plane);

	plane->_model = glm::translate(glm::vec3{ 0.0, -50.0, 0.0 });
	plane->_model = glm::rotate(plane->_model, glm::radians(-90.0f), glm::vec3{ 1, 0, 0 });
	plane->_model *= glm::scale(glm::mat4(1), glm::vec3{ 500, 500, 1 });
	_renderables.push_back(*plane);

	plane->_model = glm::translate(glm::vec3{ 0.0, -100.0, 0.0 });
	plane->_model = glm::rotate(plane->_model, glm::radians(-90.0f), glm::vec3{ 1, 0, 0 });
	plane->_model *= glm::scale(glm::mat4(1), glm::vec3{ 500, 500, 1 });
	_renderables.push_back(*plane);

	plane->_model = glm::translate(glm::vec3{ 0.0, -150.0, 0.0 });
	plane->_model = glm::rotate(plane->_model, glm::radians(-90.0f), glm::vec3{ 1, 0, 0 });
	plane->_model *= glm::scale(glm::mat4(1), glm::vec3{ 500, 500, 1 });
	_renderables.push_back(*plane);

	VKE::Texture* cubeMap = new VKE::Texture();

	vkutil::load_cubemap(&std::string("../assets/cube_maps/bluecloud"), VK_FORMAT_R8G8B8A8_UNORM, cubeMap->_image, cubeMap->_imageView);

	Prefab* boxPrefab = Prefab::get("../assets/Box.glb");
	boxPrefab->register_prefab("box");

	RenderObject* box = new RenderObject();
	box->_name = "hardcoded";
	box->_model = glm::translate(glm::vec3{ 0, 0, 0 });
	box->_prefab = boxPrefab;

	_skybox._renderable = box;
	_skybox._cubeMap = cubeMap;

	Light point_light1;
	point_light1._model = glm::translate(glm::vec3{ 0, 150, 100 });
	point_light1._color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	point_light1._radius = 1.0f;
	point_light1._intensity = 1.0f;
	point_light1._maxDist = 300.0f;
	point_light1._type = DIRECTIONAL;
	point_light1._targetPosition = glm::vec3(0.0, 0.0, 0.0);

	_lights.push_back(point_light1);
}                                
