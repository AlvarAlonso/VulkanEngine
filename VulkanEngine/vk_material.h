#pragma once

#include "vk_types.h"
#include <cassert>
#include <map>
#include <string>

class Mesh;
struct Texture;

namespace VKE
{
	class Material
	{
	public:
		//static manager to reuse materials
		static std::map<std::string, Material*> sMaterials;
		static Material* get(const char* name);
		std::string _name;
		void register_material(const char* name);

		//material properties
		glm::vec4 _color;
		float _roughness_factor;
		float _metallic_factor;
		float _tilling_factor;
		glm::vec3 _emissive_factor;

		//textures
		Texture* _color_texture;
		Texture* _emissive_texture;
		Texture* _metallic_roughness_texture;

		Texture* _occlusion_texture;
		Texture* _normal_texture;

		Material();
		Material(Texture* texture);
	};
}


