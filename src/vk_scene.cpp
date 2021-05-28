#include "vk_engine.h";
#include "vk_initializers.h"
#include <glm/gtx/transform.hpp>
#include <array>
#include "vk_material.h"
#include "vk_textures.h"
#include <cstdlib>
#include <ctime>

using namespace VKE;

Scene::Scene()
{
}

void Scene::load_resources()
{
	// Mesh load
	vkutil::load_meshes_from_obj(&std::string("../assets/vegetation/maple/MapleTree.obj"), &std::string("maple"));

	// Material generation
	VKE::Material* mapleStemMaterial = new VKE::Material(Texture::get("maple_bark"));
	mapleStemMaterial->_type = VKE::DIFFUSE;
	mapleStemMaterial->_id = VKE::Material::sMaterials.size();
	mapleStemMaterial->_color = glm::vec4{ 1.0, 1.0, 1.0, 1.0 };
	mapleStemMaterial->register_material("maple_stem");

	VKE::Material* mapleLeavesMaterial = new VKE::Material(Texture::get("maple_leaf"));
	mapleLeavesMaterial->_type = VKE::REFRACTIVE;
	mapleLeavesMaterial->_occlusion_texture = Texture::get("maple_leaf_occlusion");
	mapleLeavesMaterial->_id = VKE::Material::sMaterials.size();
	mapleLeavesMaterial->_color = glm::vec4{ 1.0, 1.0, 1.0, 0.5 };
	mapleLeavesMaterial->register_material("maple_leaves");

	VKE::Material* grassMaterial = new VKE::Material(Texture::get("grass"));
	grassMaterial->_tilling_factor = 10.0f;
	grassMaterial->_id = VKE::Material::sMaterials.size();
	grassMaterial->_color = glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
	grassMaterial->_tilling_factor = 150.0f;
	grassMaterial->register_material("grass");

	// Mesh generation
	VKE::Mesh* mapleStemMesh = VKE::Mesh::get("maple0");
	VKE::Mesh* mapleLeavesMesh = VKE::Mesh::get("maple1");
	VKE::Mesh* mapleLeaves2Mesh = VKE::Mesh::get("maple2");
	VKE::Mesh* planeMesh = new VKE::Mesh();
	planeMesh->create_quad();
	planeMesh->register_mesh("plane");

	// Prefab generation
	Prefab* mapleStemPrefab = new VKE::Prefab(*mapleStemMesh, "maple_stem");
	mapleStemPrefab->register_prefab("maple_stem");
	Prefab* mapleLeavesPrefab = new VKE::Prefab(*mapleLeavesMesh, "maple_leaves");
	mapleLeavesPrefab->register_prefab("maple_leaves");
	Prefab* mapleLeaves2Prefab = new VKE::Prefab(*mapleLeaves2Mesh, "maple_leaves");
	mapleLeaves2Prefab->register_prefab("maple_leaves2");
	Prefab* planePrefab = new VKE::Prefab(*planeMesh, "grass");
	planePrefab->register_prefab("plane");
}

void Scene::generate_sample_scene()
{	
	// TODO raytracing must accept glTFs with no indices
	
	// TODO: Rework renderables created on code to work with raytracing 
	RenderObject* treeStem = new RenderObject();
	treeStem->_model = glm::translate(glm::vec3{ 0, 0, 0 });
	treeStem->_prefab = VKE::Prefab::get("maple_stem");

	RenderObject* treeLeaves = new RenderObject();
	treeLeaves->_model = glm::translate(glm::vec3{ 0, 0, 0 });
	treeLeaves->_prefab = VKE::Prefab::get("maple_leaves");

	RenderObject* treeLeaves2 = new RenderObject();
	treeLeaves2->_model = glm::translate(glm::vec3{ 0, 0, 0 });
	treeLeaves2->_prefab = VKE::Prefab::get("maple_leaves2");

	_renderables.push_back(*treeStem);
	_renderables.push_back(*treeLeaves);
	_renderables.push_back(*treeLeaves2);

	treeStem->_model = glm::translate(glm::vec3{ 10, 0, 10 });
	treeStem->_model = glm::scale(treeStem->_model, glm::vec3{ 0.8f, 0.8f, 0.8f });
	treeLeaves->_model = glm::translate(glm::vec3{ 10, 0, 10 });
	treeLeaves->_model = glm::scale(treeLeaves->_model, glm::vec3{ 0.8f, 0.8f, 0.8f });
	treeLeaves2->_model = glm::translate(glm::vec3{ 10, 0, 10 });
	treeLeaves2->_model = glm::scale(treeLeaves2->_model, glm::vec3{ 0.8f, 0.8f, 0.8f });

	_renderables.push_back(*treeStem);
	_renderables.push_back(*treeLeaves);
	_renderables.push_back(*treeLeaves2);
	
	treeStem->_model = glm::translate(glm::vec3{ -5, 0, 12 });
	treeStem->_model = glm::scale(treeStem->_model, glm::vec3{ 0.5f, 0.5f, 0.5f });
	treeLeaves->_model = glm::translate(glm::vec3{ -5, 0, 12 });
	treeLeaves->_model = glm::scale(treeLeaves->_model, glm::vec3{ 0.5f, 0.5f, 0.5f });
	treeLeaves2->_model = glm::translate(glm::vec3{ -5, 0, 12 });
	treeLeaves2->_model = glm::scale(treeLeaves2->_model, glm::vec3{ 0.5f, 0.5f, 0.5f });
	
	_renderables.push_back(*treeStem);
	_renderables.push_back(*treeLeaves);
	_renderables.push_back(*treeLeaves2);
	
	
	treeStem->_model = glm::translate(glm::vec3{ 7, 0, -10 });
	treeStem->_model = glm::scale(treeStem->_model, glm::vec3{ 1.2f, 1.2f, 1.2f });
	treeLeaves->_model = glm::translate(glm::vec3{ 7, 0, -10 });
	treeLeaves->_model = glm::scale(treeLeaves->_model, glm::vec3{ 1.2f, 1.2f, 1.2f });
	treeLeaves2->_model = glm::translate(glm::vec3{ 7, 0, -10 });
	treeLeaves2->_model = glm::scale(treeLeaves2->_model, glm::vec3{ 1.2f, 1.2f, 1.2f });

	_renderables.push_back(*treeStem);
	_renderables.push_back(*treeLeaves);
	_renderables.push_back(*treeLeaves2);

	treeStem->_model = glm::translate(glm::vec3{ -2, 0, -15 });
	treeStem->_model = glm::scale(treeStem->_model, glm::vec3{ 0.8f, 0.8f, 0.8f });
	treeLeaves->_model = glm::translate(glm::vec3{ -2, 0, -15 });
	treeLeaves->_model = glm::scale(treeLeaves->_model, glm::vec3{ 0.8f, 0.8f, 0.8f });
	treeLeaves2->_model = glm::translate(glm::vec3{ -2, 0, -15 });
	treeLeaves2->_model = glm::scale(treeLeaves2->_model, glm::vec3{ 0.8f, 0.8f, 0.8f });

	_renderables.push_back(*treeStem);
	_renderables.push_back(*treeLeaves);
	_renderables.push_back(*treeLeaves2);
	
	/*
	treeStem->_model = glm::translate(glm::vec3{ 50, 0, 20 });
	treeStem->_model = glm::scale(treeStem->_model, glm::vec3{ 0.5, 0.5, 0.5 });
	treeLeaves->_model = glm::translate(glm::vec3{ 50, 0, 20 });
	treeLeaves->_model = glm::scale(treeLeaves->_model, glm::vec3{ 0.5, 0.5, 0.5 });
	treeLeaves2->_model = glm::translate(glm::vec3{ 50, 0, 20 });
	treeLeaves2->_model = glm::scale(treeLeaves2->_model, glm::vec3{ 0.5, 0.5, 0.5 });

	_renderables.push_back(*treeStem);
	_renderables.push_back(*treeLeaves);
	_renderables.push_back(*treeLeaves2);

	treeStem->_model = glm::translate(glm::vec3{ 55, 0, 25 });
	treeStem->_model = glm::scale(treeStem->_model, glm::vec3{ 1.5, 1.5, 1.5 });
	treeLeaves->_model = glm::translate(glm::vec3{ 55, 0, 25 });
	treeLeaves->_model = glm::scale(treeLeaves->_model, glm::vec3{ 1.5, 1.5, 1.5 });
	treeLeaves2->_model = glm::translate(glm::vec3{ 55, 0, 25 });
	treeLeaves2->_model = glm::scale(treeLeaves2->_model, glm::vec3{ 1.5, 1.5, 1.5 });

	_renderables.push_back(*treeStem);
	_renderables.push_back(*treeLeaves);
	_renderables.push_back(*treeLeaves2);

	treeStem->_model = glm::translate(glm::vec3{ 65, 0, 25 });
	treeStem->_model = glm::scale(treeStem->_model, glm::vec3{ 1.2, 1.2, 1.2 });
	treeLeaves->_model = glm::translate(glm::vec3{ 65, 0, 25 });
	treeLeaves->_model = glm::scale(treeLeaves->_model, glm::vec3{ 1.2, 1.2, 1.2 });
	treeLeaves2->_model = glm::translate(glm::vec3{ 65, 0, 25 });
	treeLeaves2->_model = glm::scale(treeLeaves2->_model, glm::vec3{ 1.2, 1.2, 1.2 });

	_renderables.push_back(*treeStem);
	_renderables.push_back(*treeLeaves);
	_renderables.push_back(*treeLeaves2);

	treeStem->_model = glm::translate(glm::vec3{ 66, 0, 31 });
	treeStem->_model = glm::scale(treeStem->_model, glm::vec3{ 1.5, 1.9, 1.5 });
	treeLeaves->_model = glm::translate(glm::vec3{ 66, 0, 31 });
	treeLeaves->_model = glm::scale(treeLeaves->_model, glm::vec3{ 1.5, 1.9, 1.5 });
	treeLeaves2->_model = glm::translate(glm::vec3{ 66, 0, 31 });
	treeLeaves2->_model = glm::scale(treeLeaves2->_model, glm::vec3{ 1.5, 1.9, 1.5 });

	_renderables.push_back(*treeStem);
	_renderables.push_back(*treeLeaves);
	_renderables.push_back(*treeLeaves2);
	*/

	RenderObject* plane = new RenderObject();
	plane->_model = glm::translate(glm::vec3{ 0.0, 0.0, 0.0 });
	plane->_model = glm::rotate(plane->_model, glm::radians(-90.0f), glm::vec3{ 1, 0, 0 });
	plane->_model *= glm::scale(glm::mat4(1), glm::vec3{ 1000, 1000, 1 });
	plane->_prefab = VKE::Prefab::get("plane");

	_renderables.push_back(*plane);

	plane->_model = glm::translate(glm::vec3{ 0.0, -50.0, 0.0 });
	plane->_model = glm::rotate(plane->_model, glm::radians(-90.0f), glm::vec3{ 1, 0, 0 });
	plane->_model *= glm::scale(glm::mat4(1), glm::vec3{ 1000, 1000, 1 });
	_renderables.push_back(*plane);

	plane->_model = glm::translate(glm::vec3{ 0.0, -100.0, 0.0 });
	plane->_model = glm::rotate(plane->_model, glm::radians(-90.0f), glm::vec3{ 1, 0, 0 });
	plane->_model *= glm::scale(glm::mat4(1), glm::vec3{ 1000, 1000, 1 });
	_renderables.push_back(*plane);

	plane->_model = glm::translate(glm::vec3{ 0.0, -150.0, 0.0 });
	plane->_model = glm::rotate(plane->_model, glm::radians(-90.0f), glm::vec3{ 1, 0, 0 });
	plane->_model *= glm::scale(glm::mat4(1), glm::vec3{ 1000, 1000, 1 });
	_renderables.push_back(*plane);

	// Skybox
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

	// Light sources
	Light* point_light1 = new Light();
	point_light1->_model = glm::translate(glm::vec3{ 100, 150, 0 });
	point_light1->_color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	point_light1->_radius = 1.0f;
	point_light1->_intensity = 1.0f;
	point_light1->_maxDist = 300.0f;
	point_light1->_type = DIRECTIONAL;
	point_light1->_targetPosition = glm::vec3(-15.0, 0.0, 0.0);

	_lights.push_back(*point_light1);
}

void Scene::generate_random_sample_scene()
{
	_renderables.reserve(100);

	RenderObject* treeStem = new RenderObject();
	treeStem->_model = glm::translate(glm::vec3{ 0, 0, 0 });
	treeStem->_prefab = VKE::Prefab::get("maple_stem");

	RenderObject* treeLeaves = new RenderObject();
	treeLeaves->_model = glm::translate(glm::vec3{ 0, 0, 0 });
	treeLeaves->_prefab = VKE::Prefab::get("maple_leaves");

	RenderObject* treeLeaves2 = new RenderObject();
	treeLeaves2->_model = glm::translate(glm::vec3{ 0, 0, 0 });
	treeLeaves2->_prefab = VKE::Prefab::get("maple_leaves2");

	const float default_distance = 20.0f;

	glm::vec3 position = glm::vec3(0);

	srand(static_cast<unsigned>(time(0)));
	
	for(int i = -2; i < 2; i++)
	{
		for(int j = -2; j < 2; j++)
		{
			float r1 = 0 + static_cast <float> (rand()) / (static_cast <float> (RAND_MAX / (5 - 0)));
			float r2 = 0 + static_cast <float> (rand()) / (static_cast <float> (RAND_MAX / (5 - 0)));

			position = glm::vec3(i * default_distance, 0.0, j * default_distance );

			float r3 = 0.3 + static_cast <float> (rand()) / (static_cast <float> (RAND_MAX / (2 - 0.3)));

			treeStem->_model = glm::translate(position);
			treeStem->_model = glm::scale(treeStem->_model, glm::vec3{ r3, r3, r3 });
			treeLeaves->_model = glm::translate(position);
			treeLeaves->_model = glm::scale(treeLeaves->_model, glm::vec3{ r3, r3, r3 });
			treeLeaves2->_model = glm::translate(position);
			treeLeaves2->_model = glm::scale(treeLeaves2->_model, glm::vec3{ r3, r3, r3 });

			_renderables.push_back(*treeStem);
			_renderables.push_back(*treeLeaves);
			_renderables.push_back(*treeLeaves2);
		}
	}
	
	RenderObject* plane = new RenderObject();
	plane->_model = glm::translate(glm::vec3{ 0.0, 0.0, 0.0 });
	plane->_model = glm::rotate(plane->_model, glm::radians(-90.0f), glm::vec3{ 1, 0, 0 });
	plane->_model *= glm::scale(glm::mat4(1), glm::vec3{ 500, 500, 1 });
	plane->_prefab = VKE::Prefab::get("plane");

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

	// Skybox
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

	// Light sources
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
