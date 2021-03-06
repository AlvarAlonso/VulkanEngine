#include "vk_entity.h"

Entity::Entity()
{
	_model = glm::mat4(1);
}

RenderObject::RenderObject() : Entity()
{
	_prefab = nullptr;
}

Light::Light() : Entity()
{
	_color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	_maxDist = 10.0f;
	_intensity = 30.0f;
	_radius = 1.0f;
}
