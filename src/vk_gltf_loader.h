#pragma once

#include <string>

namespace VKE {
	class Prefab;
};

struct TextureSampler {
	VkFilter magFilter;
	VkFilter minFilter;
	VkSamplerAddressMode addressModeU;
	VkSamplerAddressMode addressModeV;
	VkSamplerAddressMode addressModeW;
};

VKE::Prefab* load_glTF(std::string filename, float scale);