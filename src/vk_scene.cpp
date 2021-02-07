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
	
	Prefab* foxPrefab = Prefab::get("../assets/Fox.glb");
	foxPrefab->register_prefab("fox");

	RenderObject* fox = new RenderObject();
	fox->_model = glm::translate(glm::vec3{ -2, 0, -5 });
	fox->_model = glm::scale(fox->_model, glm::vec3{ 0.02, 0.02, 0.02 });
	fox->_prefab = foxPrefab;
	
	Prefab* duckPrefab = Prefab::get("../assets/Duck.glb");
	duckPrefab->register_prefab("pato");

	RenderObject* duck = new RenderObject();
	duck->_model = glm::translate(glm::vec3{ 4, 0, 0 });
	duck->_prefab = duckPrefab;
	
	Prefab* helmetPrefab = Prefab::get("../assets/DamagedHelmet.glb");
	helmetPrefab->register_prefab("helmet");

	RenderObject* helmet = new RenderObject();
	helmet->_model = glm::translate(glm::vec3{0, 0, 0}); 
	//helmet->_model = glm::rotate(glm::mat4(1), glm::radians(90.0f), glm::vec3{1, 0, 0});
	helmet->_prefab = helmetPrefab;
	
	Prefab* cornellPrefab = Prefab::get("../assets/cornellBox.gltf");
	cornellPrefab->register_prefab("cornell");

	RenderObject* cornellBox = new RenderObject();
	cornellBox->_model = glm::translate(glm::vec3{ 10, 0 , 2 });
	cornellBox->_prefab = cornellPrefab;
	
	VKE::Mesh* treeMesh = new VKE::Mesh("../assets/MapleTree.obj");
	Prefab* treePrefab = new VKE::Prefab(*treeMesh);

	RenderObject* tree = new RenderObject();
	tree->_model = glm::translate(glm::vec3{ 0, 0, 0 });
	tree->_prefab = treePrefab = treePrefab;
	
	Light point_light1;
	point_light1._position = glm::vec4(5.0f, 30.0f, 20.0f, 300.0f);
	point_light1._color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

	_renderables.push_back(*duck);
	_renderables.push_back(*fox);
	_renderables.push_back(*helmet);
	_renderables.push_back(*tree);
	_renderables.push_back(*cornellBox);

	//_lights.push_back(point_light1);
}                                
