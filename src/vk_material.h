#pragma once

#include "vk_types.h"
#include <cassert>
#include <map>
#include <string>

namespace VKE
{
	class Texture;

	enum MaterialType 
	{
		UNDEFINED = 0,
		DIFFUSE,
		REFLECTIVE,
		REFRACTIVE
	};

	class Material
	{
	public:
		std::string _name;
		int _id;

		//static manager to reuse materials
		static std::map<std::string, Material*> sMaterials;
		static Material* get(const char* name);

		//material properties
		MaterialType _type;
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
		Material(VKE::Texture* texture);
		void register_material(const char* name);

		static bool ComparePtrToMaterial(const VKE::Material* l, const VKE::Material* r) {
			return l->_id < r->_id;
		}
	};

	struct MaterialToShader
	{
		glm::vec4 _color_type;
		glm::vec4 _emissive_factor;
		glm::vec4 _roughness_metallic_tilling_color_factors; // Color is the index to the color texture
		glm::vec4 _emissive_metRough_occlusion_normal_indices; // Indices to material textures
	};
}


