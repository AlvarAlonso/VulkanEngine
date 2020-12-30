#pragma once
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include "vk_mesh.h"

struct Material {
	VkDescriptorSet albedoTexture;
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

	Mesh* _mesh;
	Material* _material;
};

class Light : public Entity
{
public:
	Light();

	float _intensity;
	float _maxDist;
};