#pragma once
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include "vk_mesh.h"

class Entity
{
public:
	glm::vec3 _model;

	Entity();
};

class RenderObject : Entity
{
public:
	RenderObject();

	Mesh* _mesh;
};

class Light : Entity
{
public:
	Light();

	float _intensity;
	float _maxDist;
};