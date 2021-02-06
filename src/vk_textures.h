#pragma once

#include "vk_types.h"
#include "vk_engine.h"
#include <map>

namespace VKE
{
	class Texture
	{
	public:
		std::string _name;
		int _id;
		AllocatedImage _image;
		VkImageView _imageView;
		VkDescriptorSet _descriptorSet;

		//Manager to cache loaded textures
		static int textureCount;
		static std::map<std::string, Texture*> sTexturesLoaded;
		static VKE::Texture* get(const char* filename);
		void register_texture(const char* name);

		static bool ComparePtrToTexture(const VKE::Texture* l, const VKE::Texture* r) {	
			return l->_id < r->_id;
		}
	};
}

namespace vkutil {

	bool load_image_from_file(VulkanEngine& engine, const char* file, AllocatedImage& outImage);
}

