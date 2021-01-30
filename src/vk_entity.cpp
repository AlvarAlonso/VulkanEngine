#include "vk_entity.h"

Entity::Entity()
{
	_model = glm::mat4(1);
}

RenderObject::RenderObject() : Entity()
{
	_prefab = nullptr;
}

Light::Light()
{
	_position = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	_color = glm::vec4(1.0f, 1.0f, 1.0f, 10.0f);
}
