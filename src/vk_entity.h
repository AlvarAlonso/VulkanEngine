#pragma once
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include "vk_prefab.h"


struct GPUObjectData
{
	glm::mat4 modelMatrix;
	glm::vec4 matIndex; // currently using only x component to pass the primitive material index
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

class Light : public Entity
{
public:
	Light();

	glm::vec3 _color;
	float _maxDist;
	float _intensity;
	float _radius;
};

struct LightToShader
{
	glm::vec4 _position_dist;
	glm::vec4 _color_intensity;
	glm::vec4 _radius;
};