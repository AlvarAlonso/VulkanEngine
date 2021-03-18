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
	/*
	Prefab* duckPrefab = Prefab::get("../assets/Duck.glb");
	duckPrefab->register_prefab("pato");

	RenderObject* duck = new RenderObject();
	duck->_model = glm::translate(glm::vec3{ 0, 0, 0 });
	duck->_prefab = duckPrefab;
	*/

	/*
	Prefab* cornellPrefab = Prefab::get("../assets/cornellBox.gltf");
	cornellPrefab->register_prefab("cornell");

	RenderObject* cornellBox = new RenderObject();
	cornellBox->_model = glm::translate(glm::vec3{ 0, 5, 0 });
	cornellBox->_model = glm::rotate(cornellBox->_model, glm::radians(-45.0f), glm::vec3{ 0.0, 1.0, 0.0 });
	cornellBox->_prefab = cornellPrefab;
	*/
	// TODO: Rework renderables created on code to work with raytracing
	
	VKE::Material* stemMaterial = new VKE::Material(Texture::get("bark"));
	stemMaterial->_type = VKE::DIFFUSE;
	stemMaterial->_id = VKE::Material::sMaterials.size();
	stemMaterial->register_material("stem");

	VKE::Mesh* treeStemMesh = VKE::Mesh::get("../assets/MapleTreeStem.obj");
	Prefab* stemPrefab = new VKE::Prefab(*treeStemMesh, "stem");

	RenderObject* treeStem = new RenderObject();
	treeStem->_model = glm::translate(glm::vec3{ 0, 0, 0 });
	treeStem->_prefab = stemPrefab;


	VKE::Material* leavesMaterial = new VKE::Material(Texture::get("leaf"));
	leavesMaterial->_type = VKE::REFRACTIVE;
	leavesMaterial->_occlusion_texture = Texture::get("leaf_occlusion");
	leavesMaterial->_id = VKE::Material::sMaterials.size();
	leavesMaterial->register_material("leaves");

	VKE::Mesh* treeLeavesMesh = VKE::Mesh::get("../assets/MapleTreeLeaves.obj");
	Prefab* leavesPrefab = new VKE::Prefab(*treeLeavesMesh, "leaves");

	RenderObject* treeLeaves = new RenderObject();
	treeLeaves->_model = glm::translate(glm::vec3{ 0, 0, 0 });
	treeLeaves->_prefab = leavesPrefab;

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
	grassMaterial->register_material("grass");
	
	VKE::Mesh* planeMesh = new VKE::Mesh();
	planeMesh->create_quad();
	Prefab* planePrefab = new VKE::Prefab(*planeMesh, "grass");

	RenderObject* plane = new RenderObject();
	plane->_model = glm::translate(glm::mat4(1), glm::vec3{ 0.0, 0.0, 0.0 });
	plane->_model = glm::rotate(glm::mat4(1), glm::radians(-90.0f), glm::vec3{ 1, 0, 0 });
	plane->_model *= glm::scale(glm::mat4(1), glm::vec3{ 100, 100, 1 });
	plane->_prefab = planePrefab;

	/*
	Prefab* boxPrefab = Prefab::get("../assets/Box.glb");
	boxPrefab->register_prefab("box");

	RenderObject* box = new RenderObject();
	box->_model = glm::translate(glm::vec3{ -5, 0, 0 });
	box->_prefab = boxPrefab;
	*/
	/*
	Prefab* carPrefab = Prefab::get("../assets/gmc/scene.gltf");
	carPrefab->register_prefab("cotxu");

	RenderObject* car = new RenderObject();
	car->_model = glm::scale(glm::vec3{ 0.02, 0.02, 0.02 });
	car->_model *= glm::translate(glm::vec3{ 0, 0, 0 });
	car->_prefab = carPrefab;
	*/
	Light point_light1;
	point_light1._model = glm::translate(glm::vec3{ 20, 100, 0 });
	point_light1._color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	point_light1._radius = 1.0f;
	point_light1._intensity = 1.0f;
	point_light1._maxDist = 300.0f;

	Light point_light2;
	point_light2._model = glm::translate(glm::vec3{ -50, 100, 0 });
	point_light2._color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	point_light2._radius = 1.0f;
	point_light2._intensity = 1.0f;
	point_light2._maxDist = 300.0f;
	
	//_renderables.push_back(*box);
	//_renderables.push_back(*duck);
	//_renderables.push_back(*fox);
	//_renderables.push_back(*helmet);
	//_renderables.push_back(*sponza);

	//_renderables.push_back(*cornellBox);
	//_renderables.push_back(*car);
	_renderables.push_back(*plane);

	_lights.push_back(point_light1);
	_lights.push_back(point_light2);
}                                
