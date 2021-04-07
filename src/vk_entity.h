#pragma once
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include "vk_prefab.h"
#include <string>

enum lightType {
	DIRECTIONAL = 0,
	SPHERE_LIGHT
};

struct GPUObjectData
{
	glm::mat4 modelMatrix;
	glm::vec4 matIndex; // currently using only x component to pass the primitive material index
};

class Entity
{
public:
	std::string _name;
	glm::mat4 _model;

	Entity();
};

class RenderObject : public Entity
{
public:
	RenderObject();
	RenderObject(const std::string& name);

	VKE::Prefab* _prefab;

	void renderInMenu();
};

class Light : public Entity
{
public:
	Light();
	Light(const std::string& name);

	glm::vec3 _targetPosition;
	glm::vec3 _color;
	float _maxDist;
	float _intensity;
	float _radius;
	lightType _type;

	void renderInMenu();
};

struct LightToShader
{
	glm::vec4 _position_dist;
	glm::vec4 _color_intensity;
	glm::vec4 _properties_type;
};