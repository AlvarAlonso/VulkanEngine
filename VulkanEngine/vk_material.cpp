#include "vk_material.h"

using namespace VKE;

std::map<std::string, Material*> Material::sMaterials;

Material::Material() : _color{1.0f, 1.0f, 1.0f, 1.0f}, _roughness_factor(1), _metallic_factor(1)
{
	_color_texture = _emissive_texture = _metallic_roughness_texture = _occlusion_texture = _normal_texture = nullptr;
}

Material::Material(Texture* texture) : Material()
{
	_color_texture = texture;
}

Material* Material::get(const char* name)
{
	assert(name);
	std::map<std::string, Material*>::iterator it = sMaterials.find(name);
	if (it != sMaterials.end())
		return it->second;
	return nullptr;
}

void Material::register_material(const char* name)
{
	_name = name;
	sMaterials[name] = this;
}
