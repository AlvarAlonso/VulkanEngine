#include "vk_entity.h"

Entity::Entity()
{
	_model = glm::mat4(1);
}

RenderObject::RenderObject() : Entity()
{
	_mesh = nullptr;
	_material = nullptr;
}

Light::Light() : Entity()
{
	_intensity = 1.0f;
	_maxDist = 5.0f;
}
