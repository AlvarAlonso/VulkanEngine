// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#include <iostream>

#include <vector>
#include <functional>
#include <deque>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

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
	VmaAllocation _allocation;
};

struct AllocatedImage {
	VkImage _image;
	VmaAllocation _allocation;
};

struct RayTracingScratchBuffer
{
	uint64_t _deviceAddress = 0;
	VkBuffer _buffer = VK_NULL_HANDLE;
	VkDeviceMemory _memory = VK_NULL_HANDLE;
};

struct AccelerationStructure
{
	VkAccelerationStructureKHR _handle;
	uint64_t _deviceAddress = 0;
	VkDeviceMemory _memory;
	VkBuffer _buffer;
};

struct Image {
	VkImageView _view;
	VkImage _image;
	VmaAllocation _allocation;
	VkFormat _format;
};