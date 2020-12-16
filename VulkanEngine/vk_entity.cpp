#include "vk_entity.h"

Entity::Entity()
{
	_model = glm::vec3(0.0f, 0.0f, 0.0f);
}

RenderObject::RenderObject() : Entity()
{
	_mesh = nullptr;
}

Light::Light() : Entity()
{
	_intensity = 1.0f;
	_maxDist = 5.0f;
}
