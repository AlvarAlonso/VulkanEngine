#pragma once
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include "vk_prefab.h"

struct GPUObjectData {
	glm::mat4 modelMatrix;
	glm::ivec4 matIndex; // currently using only x component to pass the primitive material index
};

class Entity
{
public:
	glm::mat4 _model;

	Entity();
};

class RenderObject : public Entity
{
public:
	RenderObject();
	VKE::Prefab* _prefab;
};

class Light
{
public:
	Light();

	glm::vec4 _position;
	glm::vec4 _color;
};