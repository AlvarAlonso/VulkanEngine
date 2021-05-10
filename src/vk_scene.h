#pragma once

#include "vk_entity.h"

namespace VKE
{
	class Texture;
}

struct GPUSceneData {
	glm::vec4 fogColor;
	glm::vec4 fogDistances;
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection;
	glm::vec4 sunlightColor;
};

struct Skybox
{
	RenderObject* _renderable;
	VKE::Texture*	  _cubeMap;
};

// Represents all the entities of a scene and all the custom parameters used for rendering
class Scene
{
public:

	Scene();

	void load_resources();
	void generate_sample_scene();
	void generate_random_sample_scene();

	int primitiveCount;
	std::vector<RenderObject> _renderables;
	std::vector<Light> _lights;
	Skybox _skybox;

	GPUSceneData _sceneData;
};

