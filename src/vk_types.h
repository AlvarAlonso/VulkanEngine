// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#include <vector>
#include <functional>
#include <deque>

using namespace std;
#define VK_CHECK(x)                                                 \
	do                                                              \
	{                                                               \
		VkResult err = x;                                           \
		if (err)                                                    \
		{                                                           \
			std::cout <<"Detected Vulkan error: " << err << std::endl; \
			abort();                                                \
		}                                                           \
	} while (0)

struct AllocatedBuffer {
	VkBuffer _buffer = VK_NULL_HANDLE;
	VmaAllocation _allocation = VMA_NULL;
};

struct AllocatedImage {
	VkImage _image;
	VmaAllocation _allocation;
};