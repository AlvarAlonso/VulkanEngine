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
		void register_texture(std::string name);
		/*
		bool operator< (const VKE::Texture& i) { return this->_id < i._id; }
		bool operator< (const VKE::Texture* i) { return this->_id < i->_id; }
		bool operator() (const Texture& i, const Texture& j) { return (i._id < j._id); }
		bool operator() (Texture* i, Texture* j) { return (i->_id < j->_id); }
		*/
	};
}

namespace vkutil {

	bool load_image_from_file(VulkanEngine& engine, const char* file, AllocatedImage& outImage);
}

