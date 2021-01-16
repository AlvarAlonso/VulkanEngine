#pragma once
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include "vk_mesh.h"

struct Material {
	glm::vec4 color;
	glm::vec4 properties; //metalness = x, roughness = y, index of refraction = z, material type = w (0 diffuse, 1 reflector, 2 refraction)
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
	Texture* _albedoTexture;
	Material* _material;
};

class Light
{
public:
	Light();

	glm::vec4 _position;
	glm::vec4 _color;
};