#pragma once

#include "vk_mesh.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <string>

struct Timer
{
	std::string _name;
	std::chrono::time_point<std::chrono::steady_clock> start, end;
	std::chrono::duration<float> duration;

	Timer(const std::string& name = "");

	~Timer();
};

namespace vkutil {

	uint32_t find_memory_type_index(VkPhysicalDevice physicalDevice, uint32_t allowedTypes, VkMemoryPropertyFlags properties);

	uint64_t get_buffer_device_address(VkDevice device, VkBuffer buffer);

	size_t get_aligned_size(size_t originalSize, uint32_t alignment);

	bool load_shader_module(VkDevice device, const char* filePath, VkShaderModule* outShaderModule);

	AllocatedBuffer create_buffer(VmaAllocator allocator, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags flags = 0);
}

#include "vk_render_engine.h"

namespace vkupload {

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
}