#pragma once

#include "vk_types.h"
#include <cassert>
#include <map>
#include <string>

class Mesh;

namespace VKE
{
	struct Texture;

	class Material
	{
	public:
		std::string _name;

		//static manager to reuse materials
		static int materialsCount;
		static std::map<std::string, Material*> sMaterials;
		static Material* get(const char* name);

		//material properties
		glm::vec4 _color;
		float _roughness_factor;
		float _metallic_factor;
		float _tilling_factor;
		glm::vec4 _emissive_factor;

		//textures
		VKE::Texture* _color_texture;
		VKE::Texture* _emissive_texture;
		VKE::Texture* _metallic_roughness_texture;

		VKE::Texture* _occlusion_texture;
		VKE::Texture* _normal_texture;

		Material();
		Material(Texture* texture);
		void register_material(const char* name);
	};
}

