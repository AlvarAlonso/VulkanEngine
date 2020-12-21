#include "vk_renderer.h"
#include "vk_initializers.h"
#include "vk_engine.h"
#include <iostream>

#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>

#include <array>

using namespace GRAPHICS;

Renderer::Renderer()
{
	_graphicsQueue = VulkanEngine::cinstance->_graphicsQueue;
	_graphicsQueueFamily = VulkanEngine::cinstance->_graphicsQueueFamily;
	_renderMode = RENDER_MODE_DEFERRED;
}

void Renderer::init_renderer()
{
	create_depth_buffer();
	create_deferred_attachments();
	init_commands();
	init_sync_structures();
	init_default_render_pass();
	init_deferred_render_pass();
	init_framebuffers();
	init_gbuffers_descriptors();

	deferred_quad.create_quad();
}

void Renderer::draw_scene()
{
	if(!isDeferredCommandInit)
	{
		record_deferred_command_buffers(VulkanEngine::cinstance->_renderables.data(), VulkanEngine::cinstance->_renderables.size());
		isDeferredCommandInit = true;
	}

	ImGui::Render();
	
	if(_renderMode == RENDER_MODE_FORWARD)
	{
		render_forward();
	}
	else if(_renderMode == RENDER_MODE_DEFERRED)
	{
		render_deferred();
	}
}

void GRAPHICS::Renderer::init_raytracing()
{
	// Get the ray tracing and accelertion structure related function pointers required by this sample
	vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(VulkanEngine::cinstance->_device, "vkCmdBuildAccelerationStructuresKHR"));
	vkBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(VulkanEngine::cinstance->_device, "vkBuildAccelerationStructuresKHR"));
	vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(VulkanEngine::cinstance->_device, "vkCreateAccelerationStructureKHR"));
	vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(VulkanEngine::cinstance->_device, "vkDestroyAccelerationStructureKHR"));
	vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(VulkanEngine::cinstance->_device, "vkGetAccelerationStructureBuildSizesKHR"));
	vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(VulkanEngine::cinstance->_device, "vkGetAccelerationStructureDeviceAddressKHR"));
	vkCmdTraceRaysKHR = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(vkGetDeviceProcAddr(VulkanEngine::cinstance->_device, "vkCmdTraceRaysKHR"));
	vkGetRayTracingShaderGroupHandlesKHR = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(vkGetDeviceProcAddr(VulkanEngine::cinstance->_device, "vkGetRayTracingShaderGroupHandlesKHR"));
	vkCreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(vkGetDeviceProcAddr(VulkanEngine::cinstance->_device, "vkCreateRayTracingPipelinesKHR"));

	void create_bottom_level_acceleration_structure();

	void create_top_level_acceleration_structure();

	void create_storage_image();

	void create_uniform_buffer();

	void create_raytracing_pipeline();

	void create_shader_binding_table();

	void create_raytracing_descriptor_sets();

	void allocate_raytracing_command_buffers();
}

void GRAPHICS::Renderer::create_bottom_level_acceleration_structure()
{
	// Setup vertices for a single triangle
	struct Vertex {
		float pos[3];
	};
	std::vector<Vertex> vertices = {
		{ {  1.0f,  1.0f, 0.0f } },
		{ { -1.0f,  1.0f, 0.0f } },
		{ {  0.0f, -1.0f, 0.0f } }
	};

	// Setup indices
	std::vector<uint32_t> indices = { 0, 1, 2 };
	_indexCount = static_cast<uint32_t>(indices.size());

	// Setup identity transform matrix
	VkTransformMatrixKHR transformMatrix = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f
	};

	// Create buffers
// For the sake of simplicity we won't stage the vertex data to the GPU memory
// Vertex buffer
	_vertexBuffer = VulkanEngine::cinstance->create_buffer(vertices.size() * sizeof(Vertex),
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VMA_MEMORY_USAGE_CPU_TO_GPU);
	// Index buffer
	_indexBuffer = VulkanEngine::cinstance->create_buffer(indices.size() * sizeof(uint32_t),
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VMA_MEMORY_USAGE_CPU_TO_GPU);

	// Transform buffer
	_transformBuffer = VulkanEngine::cinstance->create_buffer(sizeof(VkTransformMatrixKHR),
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VMA_MEMORY_USAGE_CPU_TO_GPU);

	VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
	VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
	VkDeviceOrHostAddressConstKHR transformBufferDeviceAddress{};

	vertexBufferDeviceAddress.deviceAddress = VulkanEngine::cinstance->get_buffer_device_address(_vertexBuffer._buffer);
	indexBufferDeviceAddress.deviceAddress = VulkanEngine::cinstance->get_buffer_device_address(_indexBuffer._buffer);
	transformBufferDeviceAddress.deviceAddress = VulkanEngine::cinstance->get_buffer_device_address(_transformBuffer._buffer);

	// Build
	VkAccelerationStructureGeometryKHR accelerationStructureGeometry{};
	accelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	accelerationStructureGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	accelerationStructureGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	accelerationStructureGeometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
	accelerationStructureGeometry.geometry.triangles.maxVertex = 3;
	accelerationStructureGeometry.geometry.triangles.vertexStride = sizeof(Vertex);
	accelerationStructureGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	accelerationStructureGeometry.geometry.triangles.indexData = indexBufferDeviceAddress;
	accelerationStructureGeometry.geometry.triangles.transformData.deviceAddress = 0;
	accelerationStructureGeometry.geometry.triangles.transformData.hostAddress = nullptr;
	accelerationStructureGeometry.geometry.triangles.transformData = transformBufferDeviceAddress;

	// Get size info
	VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
	accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	accelerationStructureBuildGeometryInfo.geometryCount = 1;
	accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

	const uint32_t numTriangles = 1;
	VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
	accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	vkGetAccelerationStructureBuildSizesKHR(
		VulkanEngine::cinstance->_device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&accelerationStructureBuildGeometryInfo,
		&numTriangles,
		&accelerationStructureBuildSizesInfo);

	create_acceleration_structure_buffer(_bottomLevelAS, accelerationStructureBuildSizesInfo);

	VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
	accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	accelerationStructureCreateInfo.buffer = _bottomLevelAS._buffer;
	accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
	accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	vkCreateAccelerationStructureKHR(VulkanEngine::cinstance->_device, &accelerationStructureCreateInfo, nullptr, &_bottomLevelAS._handle);

	// Create a small scratch buffer used during build of the bottom level acceleration structure
	RayTracingScratchBuffer scratchBuffer = create_scratch_buffer(accelerationStructureBuildSizesInfo.buildScratchSize);

	VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{};
	accelerationBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	accelerationBuildGeometryInfo.dstAccelerationStructure = _bottomLevelAS._handle;
	accelerationBuildGeometryInfo.geometryCount = 1;
	accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
	accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer._deviceAddress;

	VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
	accelerationStructureBuildRangeInfo.primitiveCount = numTriangles;
	accelerationStructureBuildRangeInfo.primitiveOffset = 0;
	accelerationStructureBuildRangeInfo.firstVertex = 0;
	accelerationStructureBuildRangeInfo.transformOffset = 0;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };

	if (VulkanEngine::cinstance->_accelerationStructureFeatures.accelerationStructureHostCommands)
	{
		// Implementation supports building acceleration structure building on host
		vkBuildAccelerationStructuresKHR(
			VulkanEngine::cinstance->_device,
			VK_NULL_HANDLE,
			1,
			&accelerationBuildGeometryInfo,
			accelerationBuildStructureRangeInfos.data());
	}
	else
	{
		// Acceleration structure needs to be build on the device
		VulkanEngine::cinstance->immediate_submit([&](VkCommandBuffer cmd)
			{
				vkCmdBuildAccelerationStructuresKHR(
					cmd,
					1,
					&accelerationBuildGeometryInfo,
					accelerationBuildStructureRangeInfos.data());
			});
	}

	VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
	accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	accelerationDeviceAddressInfo.accelerationStructure = _bottomLevelAS._handle;
	_bottomLevelAS._deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(VulkanEngine::cinstance->_device, &accelerationDeviceAddressInfo);
}

void GRAPHICS::Renderer::create_top_level_acceleration_structure()
{
	VkTransformMatrixKHR transformMatrix = {
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f };

	VkAccelerationStructureInstanceKHR instance{};
	instance.transform = transformMatrix;
	instance.instanceCustomIndex = 0;
	instance.mask = 0xFF;
	instance.instanceShaderBindingTableRecordOffset = 0;
	instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
	instance.accelerationStructureReference = _bottomLevelAS._deviceAddress;

	// Buffer for instance data
	AllocatedBuffer instancesBuffer;
	instancesBuffer = VulkanEngine::cinstance->create_buffer(
		sizeof(VkAccelerationStructureInstanceKHR),
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VMA_MEMORY_USAGE_CPU_TO_GPU);

	void* data;
	vmaMapMemory(VulkanEngine::cinstance->_allocator, instancesBuffer._allocation, &data);
	memcpy(data, &instance, sizeof(VkAccelerationStructureInstanceKHR));
	vmaUnmapMemory(VulkanEngine::cinstance->_allocator, instancesBuffer._allocation);

	VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
	instanceDataDeviceAddress.deviceAddress = VulkanEngine::cinstance->get_buffer_device_address(instancesBuffer._buffer);

	VkAccelerationStructureGeometryKHR accelerationStructureGeometry{};
	accelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	accelerationStructureGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	accelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
	accelerationStructureGeometry.geometry.instances.data = instanceDataDeviceAddress;

	// Get size info
	/*
	The pSrcAccelerationStructure, dstAccelerationStructure, and mode members of pBuildInfo are ignored. Any VkDeviceOrHostAddressKHR members of pBuildInfo are ignored by this command, except that the hostAddress member of VkAccelerationStructureGeometryTrianglesDataKHR::transformData will be examined to check if it is NULL.*
	*/
	VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
	accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	accelerationStructureBuildGeometryInfo.geometryCount = 1;
	accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

	uint32_t primitive_count = 1;

	VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
	accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	vkGetAccelerationStructureBuildSizesKHR(
		VulkanEngine::cinstance->_device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&accelerationStructureBuildGeometryInfo,
		&primitive_count,
		&accelerationStructureBuildSizesInfo);

	create_acceleration_structure_buffer(_topLevelAS, accelerationStructureBuildSizesInfo);

	VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
	accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	accelerationStructureCreateInfo.buffer = _topLevelAS._buffer;
	accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
	accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	vkCreateAccelerationStructureKHR(VulkanEngine::cinstance->_device, &accelerationStructureCreateInfo, nullptr, &_topLevelAS._handle);

	// Create a small scratch buffer used during build of the top level acceleration structure
	RayTracingScratchBuffer scratchBuffer = create_scratch_buffer(accelerationStructureBuildSizesInfo.buildScratchSize);

	VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{};
	accelerationBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	accelerationBuildGeometryInfo.dstAccelerationStructure = _topLevelAS._handle;
	accelerationBuildGeometryInfo.geometryCount = 1;
	accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
	accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer._deviceAddress;

	VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
	accelerationStructureBuildRangeInfo.primitiveCount = 1;
	accelerationStructureBuildRangeInfo.primitiveOffset = 0;
	accelerationStructureBuildRangeInfo.firstVertex = 0;
	accelerationStructureBuildRangeInfo.transformOffset = 0;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };

	if (VulkanEngine::cinstance->_accelerationStructureFeatures.accelerationStructureHostCommands)
	{
		// Implementation supports building acceleration structure building on host
		vkBuildAccelerationStructuresKHR(
			VulkanEngine::cinstance->_device,
			VK_NULL_HANDLE,
			1,
			&accelerationBuildGeometryInfo,
			accelerationBuildStructureRangeInfos.data());
	}
	else
	{
		// Acceleration structure needs to be build on the device
		VulkanEngine::cinstance->immediate_submit([&](VkCommandBuffer cmd)
			{
				vkCmdBuildAccelerationStructuresKHR(
					cmd,
					1,
					&accelerationBuildGeometryInfo,
					accelerationBuildStructureRangeInfos.data());
			});
	}

	VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
	accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	accelerationDeviceAddressInfo.accelerationStructure = _topLevelAS._handle;
	_topLevelAS._deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(VulkanEngine::cinstance->_device, &accelerationDeviceAddressInfo);
}

void GRAPHICS::Renderer::create_storage_image()
{
	VkExtent3D extent = 
	{
		VulkanEngine::cinstance->_windowExtent.width,
		VulkanEngine::cinstance->_windowExtent.height,
		1
	};

	VkImageCreateInfo image = vkinit::image_create_info(VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT, extent);
	image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK(vkCreateImage(VulkanEngine::cinstance->_device, &image, nullptr, &_storageImage));

	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements(VulkanEngine::cinstance->_device, _storageImage, &memReqs);

	VkMemoryAllocateInfo memoryAllocateInfo = {};
	memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocateInfo.allocationSize = memReqs.size;
	memoryAllocateInfo.memoryTypeIndex = VulkanEngine::cinstance->find_memory_type_index(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VK_CHECK(vkAllocateMemory(VulkanEngine::cinstance->_device, &memoryAllocateInfo, nullptr, &_storageImageMemory));
	VK_CHECK(vkBindImageMemory(VulkanEngine::cinstance->_device, _storageImage, _storageImageMemory, 0));

	VkImageViewCreateInfo colorImageView = vkinit::imageview_create_info(VK_FORMAT_B8G8R8A8_UNORM, _storageImage, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(VulkanEngine::cinstance->_device, &colorImageView, nullptr, &_storageImageView));

	VulkanEngine::cinstance->immediate_submit([&](VkCommandBuffer cmd) {
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = _storageImage;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		
		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		});
}

void GRAPHICS::Renderer::create_uniform_buffer()
{
}

void GRAPHICS::Renderer::create_raytracing_pipeline()
{
}

void GRAPHICS::Renderer::create_shader_binding_table()
{
}

void GRAPHICS::Renderer::create_raytracing_descriptor_sets()
{
}

void GRAPHICS::Renderer::allocate_raytracing_command_buffers()
{
}

RayTracingScratchBuffer GRAPHICS::Renderer::create_scratch_buffer(VkDeviceSize size)
{
	RayTracingScratchBuffer scratchBuffer{};

	VkBufferCreateInfo bufferCreateInfo{};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = size;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	VK_CHECK(vkCreateBuffer(VulkanEngine::cinstance->_device, &bufferCreateInfo, nullptr, &scratchBuffer._buffer));

	VkMemoryRequirements memoryRequirements{};
	vkGetBufferMemoryRequirements(VulkanEngine::cinstance->_device, scratchBuffer._buffer, &memoryRequirements);

	VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo{};
	memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

	VkMemoryAllocateInfo memoryAllocateInfo = {};
	memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
	memoryAllocateInfo.allocationSize = memoryRequirements.size;
	memoryAllocateInfo.memoryTypeIndex = VulkanEngine::cinstance->find_memory_type_index(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK(vkAllocateMemory(VulkanEngine::cinstance->_device, &memoryAllocateInfo, nullptr, &scratchBuffer._memory));
	VK_CHECK(vkBindBufferMemory(VulkanEngine::cinstance->_device, scratchBuffer._buffer, scratchBuffer._memory, 0));

	VkBufferDeviceAddressInfoKHR bufferDeviceAddressInfo{};
	bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	bufferDeviceAddressInfo.buffer = scratchBuffer._buffer;
	scratchBuffer._deviceAddress = vkGetBufferDeviceAddressKHR(VulkanEngine::cinstance->_device, &bufferDeviceAddressInfo);

	VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
		vkFreeMemory(VulkanEngine::cinstance->_device, scratchBuffer._memory, nullptr);
		vkDestroyBuffer(VulkanEngine::cinstance->_device, scratchBuffer._buffer, nullptr);
		});

	return scratchBuffer;
}

void GRAPHICS::Renderer::create_acceleration_structure_buffer(AccelerationStructure& accelerationStructure, VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo)
{
	VkBufferCreateInfo bufferCreateInfo{};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = buildSizeInfo.accelerationStructureSize;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

	VK_CHECK(vkCreateBuffer(VulkanEngine::cinstance->_device, &bufferCreateInfo, nullptr, &accelerationStructure._buffer));

	VkMemoryRequirements memoryRequirements{};
	vkGetBufferMemoryRequirements(VulkanEngine::cinstance->_device, accelerationStructure._buffer, &memoryRequirements);

	VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo{};
	memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

	VkMemoryAllocateInfo memoryAllocateInfo{};
	memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
	memoryAllocateInfo.allocationSize = memoryRequirements.size;
	memoryAllocateInfo.memoryTypeIndex = VulkanEngine::cinstance->find_memory_type_index(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VK_CHECK(vkAllocateMemory(VulkanEngine::cinstance->_device, &memoryAllocateInfo, nullptr, &accelerationStructure._memory));
	VK_CHECK(vkBindBufferMemory(VulkanEngine::cinstance->_device, accelerationStructure._buffer, accelerationStructure._memory, 0));
}

void Renderer::create_depth_buffer()
{
	VkExtent3D depthImageExtent = {
		VulkanEngine::cinstance->_windowExtent.width,
		VulkanEngine::cinstance->_windowExtent.height,
		1
	};

	_depthFormat = VK_FORMAT_D32_SFLOAT;

	VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);

	VmaAllocationCreateInfo dimg_allocinfo = {};
	dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	dimg_allocinfo.requiredFlags = VkMemoryPropertyFlagBits(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(VulkanEngine::cinstance->_allocator, &dimg_info, &dimg_allocinfo, &_depthImage._image, &_depthImage._allocation, nullptr);

	VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthFormat, _depthImage._image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK(vkCreateImageView(VulkanEngine::cinstance->_device, &dview_info, nullptr, &_depthImageView));


	//Create Sampler
	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info(VK_FILTER_NEAREST);

	vkCreateSampler(VulkanEngine::cinstance->_device, &samplerInfo, nullptr, &_defaultSampler);
}

void Renderer::create_deferred_attachments()
{
	VkExtent3D attachmentExtent{
	VulkanEngine::cinstance->_windowExtent.width,
	VulkanEngine::cinstance->_windowExtent.height,
	1
	};

	_positionFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_normalFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_albedoFormat = VK_FORMAT_R8G8B8A8_UNORM;

	VkImageCreateInfo position_igm = vkinit::image_create_info(_positionFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, attachmentExtent);
	VkImageCreateInfo normal_igm = vkinit::image_create_info(_normalFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, attachmentExtent);
	VkImageCreateInfo albedo_igm = vkinit::image_create_info(_albedoFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, attachmentExtent);

	VmaAllocationCreateInfo img_alloc_info = {};
	img_alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	img_alloc_info.requiredFlags = VkMemoryPropertyFlagBits(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(VulkanEngine::cinstance->_allocator, &position_igm, &img_alloc_info, &_positionImage._image, &_positionImage._allocation, nullptr);
	vmaCreateImage(VulkanEngine::cinstance->_allocator, &normal_igm, &img_alloc_info, &_normalImage._image, &_normalImage._allocation, nullptr);
	vmaCreateImage(VulkanEngine::cinstance->_allocator, &albedo_igm, &img_alloc_info, &_albedoImage._image, &_albedoImage._allocation, nullptr);

	VkImageViewCreateInfo position_view_igm = vkinit::imageview_create_info(VK_FORMAT_R16G16B16A16_SFLOAT, _positionImage._image, VK_IMAGE_ASPECT_COLOR_BIT);
	VkImageViewCreateInfo normal_view_igm = vkinit::imageview_create_info(VK_FORMAT_R16G16B16A16_SFLOAT, _normalImage._image, VK_IMAGE_ASPECT_COLOR_BIT);
	VkImageViewCreateInfo albedo_view_igm = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, _albedoImage._image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(VulkanEngine::cinstance->_device, &position_view_igm, nullptr, &_positionImageView));
	VK_CHECK(vkCreateImageView(VulkanEngine::cinstance->_device, &normal_view_igm, nullptr, &_normalImageView));
	VK_CHECK(vkCreateImageView(VulkanEngine::cinstance->_device, &albedo_view_igm, nullptr, &_albedoImageView));

	VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(VulkanEngine::cinstance->_device, _positionImageView, nullptr);
		vmaDestroyImage(VulkanEngine::cinstance->_allocator, _positionImage._image, _positionImage._allocation);
		vkDestroyImageView(VulkanEngine::cinstance->_device, _normalImageView, nullptr);
		vmaDestroyImage(VulkanEngine::cinstance->_allocator, _normalImage._image, _normalImage._allocation);
		vkDestroyImageView(VulkanEngine::cinstance->_device, _albedoImageView, nullptr);
		vmaDestroyImage(VulkanEngine::cinstance->_allocator, _albedoImage._image, _albedoImage._allocation);
		});
}

void Renderer::init_commands()
{
	//create a command pool for commands submitted to the graphics queue.
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateCommandPool(VulkanEngine::cinstance->_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

		//allocat the default command buffer that we will use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(VulkanEngine::cinstance->_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));

		VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
			vkDestroyCommandPool(VulkanEngine::cinstance->_device, _frames[i]._commandPool, nullptr);
			});
	}

	VK_CHECK(vkCreateCommandPool(VulkanEngine::cinstance->_device, &commandPoolInfo, nullptr, &_deferredCommandPool));

	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_deferredCommandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(VulkanEngine::cinstance->_device, &cmdAllocInfo, &_deferredCommandBuffer));

	VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(VulkanEngine::cinstance->_device, _deferredCommandPool, nullptr);
		});
}

void Renderer::init_framebuffers()
{
	//OFFSCREEN FRAMEBUFFER

	std::array<VkImageView, 4> attachments;
	attachments[0] = _positionImageView;
	attachments[1] = _normalImageView;
	attachments[2] = _albedoImageView;
	attachments[3] = _depthImageView;

	VkFramebufferCreateInfo fb_info = {};
	fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb_info.pNext = nullptr;
	fb_info.renderPass = _deferredRenderPass;
	fb_info.attachmentCount = static_cast<uint32_t>(attachments.size());
	fb_info.pAttachments = attachments.data();
	fb_info.width = VulkanEngine::cinstance->_windowExtent.width;
	fb_info.height = VulkanEngine::cinstance->_windowExtent.height;
	fb_info.layers = 1;

	VK_CHECK(vkCreateFramebuffer(VulkanEngine::cinstance->_device, &fb_info, nullptr, &_offscreen_framebuffer));

	//SWAPCHAIN FRAMEBUFFERS

	VkFramebufferCreateInfo sc_fb_info = {};
	sc_fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	sc_fb_info.pNext = nullptr;

	sc_fb_info.renderPass = _defaultRenderPass;
	sc_fb_info.attachmentCount = 1;
	sc_fb_info.width = VulkanEngine::cinstance->_windowExtent.width;
	sc_fb_info.height = VulkanEngine::cinstance->_windowExtent.height;
	sc_fb_info.layers = 1;

	const uint32_t swapchain_imagecount = VulkanEngine::cinstance->_swapchainImages.size();
	_framebuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

	for (int i = 0; i < swapchain_imagecount; i++)
	{
		std::array<VkImageView, 2> attachments = { VulkanEngine::cinstance->_swapchainImageViews[i], _depthImageView };

		sc_fb_info.attachmentCount = static_cast<uint32_t>(attachments.size());
		sc_fb_info.pAttachments = attachments.data();

		VK_CHECK(vkCreateFramebuffer(VulkanEngine::cinstance->_device, &sc_fb_info, nullptr, &_framebuffers[i]));

		VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
			vkDestroyFramebuffer(VulkanEngine::cinstance->_device, _framebuffers[i], nullptr);
			vkDestroyImageView(VulkanEngine::cinstance->_device, VulkanEngine::cinstance->_swapchainImageViews[i], nullptr);
			});
	}
}

void Renderer::init_sync_structures()
{
	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = nullptr;
	semaphoreCreateInfo.flags = 0;

	VkSemaphoreCreateInfo offscreenSemaphoreInfo = {};
	offscreenSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	offscreenSemaphoreInfo.pNext = nullptr;
	offscreenSemaphoreInfo.flags = 0;

	VK_CHECK(vkCreateSemaphore(VulkanEngine::cinstance->_device, &offscreenSemaphoreInfo, nullptr, &_offscreenSemaphore));

	VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
		vkDestroySemaphore(VulkanEngine::cinstance->_device, _offscreenSemaphore, nullptr);
		});

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateFence(VulkanEngine::cinstance->_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

		VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
			vkDestroyFence(VulkanEngine::cinstance->_device, _frames[i]._renderFence, nullptr);
			});

		VK_CHECK(vkCreateSemaphore(VulkanEngine::cinstance->_device, &semaphoreCreateInfo, nullptr, &_frames[i]._presentSemaphore));
		VK_CHECK(vkCreateSemaphore(VulkanEngine::cinstance->_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));

		VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
			vkDestroySemaphore(VulkanEngine::cinstance->_device, _frames[i]._presentSemaphore, nullptr);
			vkDestroySemaphore(VulkanEngine::cinstance->_device, _frames[i]._renderSemaphore, nullptr);
			});
	}
}

void Renderer::init_default_render_pass()
{
	VkAttachmentDescription color_attachment = {};
	color_attachment.format = VulkanEngine::cinstance->_swapchainImageFormat;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref = {};
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription depth_attachment = {};
	depth_attachment.flags = 0;
	depth_attachment.format = _depthFormat;
	depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depth_attachment_ref = {};
	depth_attachment_ref.attachment = 1;
	depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;
	subpass.pDepthStencilAttachment = &depth_attachment_ref;

	VkAttachmentDescription attachments[2] = { color_attachment, depth_attachment };

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.attachmentCount = 2;
	render_pass_info.pAttachments = &attachments[0];
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;

	VK_CHECK(vkCreateRenderPass(VulkanEngine::cinstance->_device, &render_pass_info, nullptr, &_defaultRenderPass));

	VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
		vkDestroyRenderPass(VulkanEngine::cinstance->_device, _defaultRenderPass, nullptr);
		});
}

void Renderer::init_deferred_render_pass()
{
	//gBuffers Pass

	VkAttachmentDescription position_attachment = {};
	position_attachment.format = _positionFormat;
	position_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	position_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	position_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	position_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	position_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	position_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	position_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentDescription normal_attachment = {};
	normal_attachment.format = _normalFormat;
	normal_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	normal_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	normal_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	normal_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	normal_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	normal_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	normal_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentDescription albedo_attachment = {};
	albedo_attachment.format = _albedoFormat;
	albedo_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	albedo_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	albedo_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	albedo_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	albedo_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	albedo_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	albedo_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentDescription depth_attachment = {};
	depth_attachment.format = _depthFormat;
	depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	std::array<VkAttachmentDescription, 4> attachment_descriptions = { position_attachment, normal_attachment, albedo_attachment, depth_attachment };

	VkAttachmentReference position_ref;
	position_ref.attachment = 0;
	position_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference normal_ref;
	normal_ref.attachment = 1;
	normal_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference albedo_ref;
	albedo_ref.attachment = 2;
	albedo_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	std::array<VkAttachmentReference, 3> color_references = { position_ref, normal_ref, albedo_ref };

	VkAttachmentReference depth_ref;
	depth_ref.attachment = 3;
	depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = static_cast<uint32_t>(color_references.size());
	subpass.pColorAttachments = color_references.data();
	subpass.pDepthStencilAttachment = &depth_ref;

	std::array<VkSubpassDependency, 2> dependencies;

	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo deferred_pass = {};
	deferred_pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	deferred_pass.pNext = nullptr;
	deferred_pass.attachmentCount = static_cast<uint32_t>(attachment_descriptions.size());
	deferred_pass.pAttachments = attachment_descriptions.data();
	deferred_pass.subpassCount = 1;
	deferred_pass.pSubpasses = &subpass;
	deferred_pass.dependencyCount = static_cast<uint32_t>(dependencies.size());
	deferred_pass.pDependencies = dependencies.data();

	VK_CHECK(vkCreateRenderPass(VulkanEngine::cinstance->_device, &deferred_pass, nullptr, &_deferredRenderPass));

	VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
		vkDestroyRenderPass(VulkanEngine::cinstance->_device, _deferredRenderPass, nullptr);
		});
}

void GRAPHICS::Renderer::init_gbuffers_descriptors()
{
	VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = 0;
	pool_info.maxSets = 10;
	pool_info.poolSizeCount = 1;
	pool_info.pPoolSizes = &pool_size;

	vkCreateDescriptorPool(VulkanEngine::cinstance->_device, &pool_info, nullptr, &_gbuffersPool);

	//gbuffers
	VkDescriptorSetLayoutBinding position_bind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
	VkDescriptorSetLayoutBinding normal_bind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1);
	VkDescriptorSetLayoutBinding albedo_bind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2);

	std::array<VkDescriptorSetLayoutBinding, 3> deferred_set_layouts = { position_bind, normal_bind, albedo_bind };

	VkDescriptorSetLayoutCreateInfo deferred_layout_info = {};
	deferred_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	deferred_layout_info.pNext = nullptr;
	deferred_layout_info.flags = 0;
	deferred_layout_info.bindingCount = static_cast<uint32_t>(deferred_set_layouts.size());
	deferred_layout_info.pBindings = deferred_set_layouts.data();

	VK_CHECK(vkCreateDescriptorSetLayout(VulkanEngine::cinstance->_device, &deferred_layout_info, nullptr, &_gbuffersSetLayout));

	VkDescriptorSetAllocateInfo deferred_set_alloc = {};
	deferred_set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	deferred_set_alloc.pNext = nullptr;
	deferred_set_alloc.descriptorPool = _gbuffersPool;
	deferred_set_alloc.descriptorSetCount = 1;
	deferred_set_alloc.pSetLayouts = &_gbuffersSetLayout;

	vkAllocateDescriptorSets(VulkanEngine::cinstance->_device, &deferred_set_alloc, &_gbuffersDescriptorSet);

	VkDescriptorImageInfo position_descriptor_image;
	position_descriptor_image.sampler = _defaultSampler;
	position_descriptor_image.imageView = _positionImageView;
	position_descriptor_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorImageInfo normal_descriptor_image;
	normal_descriptor_image.sampler = _defaultSampler;
	normal_descriptor_image.imageView = _normalImageView;
	normal_descriptor_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorImageInfo albedo_descriptor_image;
	albedo_descriptor_image.sampler = _defaultSampler;
	albedo_descriptor_image.imageView = _albedoImageView;
	albedo_descriptor_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet position_texture = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _gbuffersDescriptorSet, &position_descriptor_image, 0);
	VkWriteDescriptorSet normal_texture = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _gbuffersDescriptorSet, &normal_descriptor_image, 1);
	VkWriteDescriptorSet albedo_texture = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _gbuffersDescriptorSet, &albedo_descriptor_image, 2);

	std::array<VkWriteDescriptorSet, 3> setWrites = { position_texture, normal_texture, albedo_texture };

	vkUpdateDescriptorSets(VulkanEngine::cinstance->_device, static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0, nullptr);
}

void GRAPHICS::Renderer::create_pipelines()
{
	create_forward_pipelines();
	create_deferred_pipelines();
}

void Renderer::record_deferred_command_buffers(RenderObject* first, int count)
{
	//FIRST PASS

	VkCommandBufferBeginInfo deferredCmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

	VK_CHECK(vkBeginCommandBuffer(_deferredCommandBuffer, &deferredCmdBeginInfo));

	VkClearValue first_clearValue;
	first_clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

	VkClearValue first_depthClear;
	first_depthClear.depthStencil.depth = 1.0f;

	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(_deferredRenderPass, VulkanEngine::cinstance->_windowExtent, _offscreen_framebuffer);

	std::array<VkClearValue, 4> first_clearValues = { first_clearValue, first_clearValue, first_clearValue, first_depthClear };

	rpInfo.clearValueCount = static_cast<uint32_t>(first_clearValues.size());
	rpInfo.pClearValues = first_clearValues.data();

	vkCmdBeginRenderPass(_deferredCommandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(_deferredCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _deferredPipeline);

	Mesh* lastMesh = nullptr;
	for (int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];

		vkCmdBindDescriptorSets(_deferredCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, VulkanEngine::cinstance->_deferredPipelineLayout, 0, 1, &VulkanEngine::cinstance->_camDescriptorSet, 0, nullptr);

		vkCmdBindDescriptorSets(_deferredCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, VulkanEngine::cinstance->_deferredPipelineLayout, 1, 1, &VulkanEngine::cinstance->_objectDescriptorSet, 0, nullptr);

		if (object._material->textureSet != VK_NULL_HANDLE)
		{
			vkCmdBindDescriptorSets(_deferredCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, VulkanEngine::cinstance->_deferredPipelineLayout, 2, 1, &object._material->textureSet, 0, nullptr);
		}

		if (object._mesh != lastMesh)
		{
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(_deferredCommandBuffer, 0, 1, &object._mesh->_vertexBuffer._buffer, &offset);
			vkCmdBindIndexBuffer(_deferredCommandBuffer, object._mesh->_indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);
		}

		vkCmdDrawIndexed(_deferredCommandBuffer, static_cast<uint32_t>(object._mesh->_indices.size()), 1, 0, 0, i);
	}

	vkCmdEndRenderPass(_deferredCommandBuffer);

	VK_CHECK(vkEndCommandBuffer(_deferredCommandBuffer));
}

int Renderer::get_current_frame_index()
{
	return VulkanEngine::cinstance->_frameNumber % FRAME_OVERLAP;
}

void Renderer::render_forward()
{
	VK_CHECK(vkWaitForFences(VulkanEngine::cinstance->_device, 1, &_frames[get_current_frame_index()]._renderFence, true, UINT64_MAX));
	VK_CHECK(vkResetFences(VulkanEngine::cinstance->_device, 1, &_frames[get_current_frame_index()]._renderFence));

	//request image from the swapchain
	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(VulkanEngine::cinstance->_device, VulkanEngine::cinstance->_swapchain, 0, _frames[get_current_frame_index()]._presentSemaphore, nullptr, &swapchainImageIndex));

	VK_CHECK(vkResetCommandBuffer(_frames[get_current_frame_index()]._mainCommandBuffer, 0));

	VkCommandBuffer cmd = _frames[get_current_frame_index()]._mainCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	VkClearValue clearValue;
	float flash = abs(sin(_frameNumber / 120.0f));
	clearValue.color = { {0.0f, 0.0f, flash, 1.0f} };

	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.0f;

	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(_defaultRenderPass, VulkanEngine::cinstance->_windowExtent, _framebuffers[swapchainImageIndex]);

	std::array<VkClearValue, 2> clearValues = { clearValue, depthClear };

	rpInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	rpInfo.pClearValues = clearValues.data();

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	draw_forward(cmd, VulkanEngine::cinstance->_renderables.data(), VulkanEngine::cinstance->_renderables.size());

	vkCmdEndRenderPass(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = vkinit::submit_info(&cmd);

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	submit.pWaitDstStageMask = &waitStage;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &_frames[get_current_frame_index()]._presentSemaphore;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &_frames[get_current_frame_index()]._renderSemaphore;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, _frames[get_current_frame_index()]._renderFence));

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.pSwapchains = &VulkanEngine::cinstance->_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &_frames[get_current_frame_index()]._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

	VulkanEngine::cinstance->_frameNumber++;
}

void Renderer::render_deferred()
{
	VK_CHECK(vkWaitForFences(VulkanEngine::cinstance->_device, 1, &_frames[get_current_frame_index()]._renderFence, true, UINT64_MAX));
	VK_CHECK(vkResetFences(VulkanEngine::cinstance->_device, 1, &_frames[get_current_frame_index()]._renderFence));

	//request image from the swapchain
	uint32_t swapchainImageIndex;
	//VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, 0, get_current_frame()._presentSemaphore, nullptr, &swapchainImageIndex));
	VkResult result = vkAcquireNextImageKHR(VulkanEngine::cinstance->_device, VulkanEngine::cinstance->_swapchain, 0, _frames[get_current_frame_index()]._presentSemaphore, nullptr, &swapchainImageIndex);

	VkCommandBuffer cmd = _frames[get_current_frame_index()]._mainCommandBuffer;

	VulkanEngine::cinstance->update_descriptors(VulkanEngine::cinstance->_renderables.data(), VulkanEngine::cinstance->_renderables.size());

	draw_deferred(cmd, swapchainImageIndex);

	VkSubmitInfo offscreenSubmit = vkinit::submit_info(&_deferredCommandBuffer);

	VkPipelineStageFlags offscreenWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	offscreenSubmit.pWaitDstStageMask = &offscreenWaitStage;
	offscreenSubmit.waitSemaphoreCount = 1;
	offscreenSubmit.pWaitSemaphores = &_frames[get_current_frame_index()]._presentSemaphore;
	offscreenSubmit.signalSemaphoreCount = 1;
	offscreenSubmit.pSignalSemaphores = &_offscreenSemaphore;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &offscreenSubmit, nullptr));

	VkSubmitInfo renderSubmit = vkinit::submit_info(&_frames[get_current_frame_index()]._mainCommandBuffer);

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	renderSubmit.pWaitDstStageMask = &waitStage;
	renderSubmit.waitSemaphoreCount = 1;
	renderSubmit.pWaitSemaphores = &_offscreenSemaphore;
	renderSubmit.signalSemaphoreCount = 1;
	renderSubmit.pSignalSemaphores = &_frames[get_current_frame_index()]._renderSemaphore;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &renderSubmit, _frames[get_current_frame_index()]._renderFence));

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.pSwapchains = &VulkanEngine::cinstance->_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &_frames[get_current_frame_index()]._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

	VulkanEngine::cinstance->_frameNumber++;
}

void GRAPHICS::Renderer::draw_forward(VkCommandBuffer cmd, RenderObject* first, int count)
{
	int frameIndex = _frameNumber % FRAME_OVERLAP;

	Mesh* lastMesh = nullptr;
	Material* lastMaterial = nullptr;
	for (int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];

		if (object._material != lastMaterial)
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object._material->pipeline);
			lastMaterial = object._material;

			uint32_t uniform_offset = VulkanEngine::cinstance->pad_uniform_buffer_size(sizeof(GPUSceneData) * frameIndex);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object._material->pipelineLayout, 0, 1, &VulkanEngine::cinstance->_frames[get_current_frame_index()].globalDescriptor, 1, &uniform_offset);

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object._material->pipelineLayout, 1, 1, &VulkanEngine::cinstance->_frames[get_current_frame_index()].objectDescriptor, 0, nullptr);

			if (object._material->textureSet != VK_NULL_HANDLE)
			{
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object._material->pipelineLayout, 2, 1, &object._material->textureSet, 0, nullptr);
			}
		}
		
		if (object._mesh != lastMesh)
		{
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &object._mesh->_vertexBuffer._buffer, &offset);
			vkCmdBindIndexBuffer(cmd, object._mesh->_indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT16);
		}

		vkCmdDrawIndexed(cmd, static_cast<uint32_t>(object._mesh->_indices.size()), 1, 0, 0, 0);
	}
}

void GRAPHICS::Renderer::draw_deferred(VkCommandBuffer cmd, int imageIndex)
{
	//SECOND PASS

	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	VkClearValue clearValue;
	float flash = abs(sin(_frameNumber / 120.0f));
	clearValue.color = { {0.0f, 0.0f, flash, 1.0f} };

	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.0f;

	VkRenderPassBeginInfo light_rpInfo = vkinit::renderpass_begin_info(_defaultRenderPass, VulkanEngine::cinstance->_windowExtent, _framebuffers[imageIndex]);

	std::array<VkClearValue, 2> clearValues = { clearValue, depthClear };

	light_rpInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	light_rpInfo.pClearValues = clearValues.data();

	vkCmdBeginRenderPass(cmd, &light_rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _lightPipeline);

	int frameIndex = _frameNumber % FRAME_OVERLAP;
	uint32_t uniform_offset = VulkanEngine::cinstance->pad_uniform_buffer_size(sizeof(GPUSceneData) * frameIndex);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, VulkanEngine::cinstance->_lightPipelineLayout, 0, 1, &VulkanEngine::cinstance->_frames[get_current_frame_index()].globalDescriptor, 1, &uniform_offset);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, VulkanEngine::cinstance->_lightPipelineLayout, 1, 1, &_gbuffersDescriptorSet, 0, nullptr);

	//deferred quad
	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &deferred_quad._vertexBuffer._buffer, &offset);
	vkCmdBindIndexBuffer(cmd, deferred_quad._indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(cmd, static_cast<uint32_t>(deferred_quad._indices.size()), 1, 0, 0, 0);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRenderPass(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));
}

void GRAPHICS::Renderer::create_forward_pipelines()
{
	//FORWARD PIPELINES

//default vertex shader for a mesh
	VkShaderModule meshVertShader;
	if (!VulkanEngine::cinstance->load_shader_module("../shaders/tri_mesh.vert.spv", &meshVertShader))
	{
		std::cout << "Error when building the triangle vertex shader module" << std::endl;
	}
	else {
		std::cout << "Red Triangle vertex shader succesfully loaded" << std::endl;
	}

	//shader for default material
	VkShaderModule colorMeshShader;
	if (!VulkanEngine::cinstance->load_shader_module("../shaders/default_lit.frag.spv", &colorMeshShader))
	{
		std::cout << "Error when building the triangle fragment shader module" << std::endl;
	}
	else
	{
		std::cout << "Triangle fragment shader succesfully loaded" << std::endl;
	}

	//shader for textured material
	VkShaderModule texturedMeshShader;
	if (!VulkanEngine::cinstance->load_shader_module("../shaders/textured_lit.frag.spv", &texturedMeshShader))
	{
		std::cout << "Error when building the textured mesh shader" << std::endl;
	}
	else
	{
		std::cout << "Textured mesh shader succesfully loaded" << endl;
	}

	// Layouts

	//mesh layout
	VkPipelineLayoutCreateInfo mesh_pipeline_layout_info = vkinit::pipeline_layout_create_info();

	VkPushConstantRange push_constant;
	push_constant.offset = 0;
	push_constant.size = sizeof(MeshPushConstants);
	push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	mesh_pipeline_layout_info.pPushConstantRanges = &push_constant;
	mesh_pipeline_layout_info.pushConstantRangeCount = 1;

	std::array<VkDescriptorSetLayout, 3> setLayouts = { VulkanEngine::cinstance->_globalSetLayout, VulkanEngine::cinstance->_objectSetLayout, VulkanEngine::cinstance->_singleTextureSetLayout };

	mesh_pipeline_layout_info.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
	mesh_pipeline_layout_info.pSetLayouts = setLayouts.data();

	VK_CHECK(vkCreatePipelineLayout(VulkanEngine::cinstance->_device, &mesh_pipeline_layout_info, nullptr, &VulkanEngine::cinstance->_forwardPipelineLayout));

	//PIPELINES
	PipelineBuilder pipelineBuilder;

	//color mesh pipeline

	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();
	//connect the pipeline builder vertex input info to the one we get from Vertex
	VertexInputDescription vertexDescription = Vertex::get_vertex_description();
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();

	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();


	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float)VulkanEngine::cinstance->_windowExtent.width;
	pipelineBuilder._viewport.height = (float)VulkanEngine::cinstance->_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;

	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = VulkanEngine::cinstance->_windowExtent;

	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();
	pipelineBuilder._colorBlendAttachment.push_back(vkinit::color_blend_attachment_state());

	//add shaders
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, meshVertShader));

	//make sure that triangleFragShader is holding the compiled colored_triangle.frag
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, colorMeshShader));

	pipelineBuilder._pipelineLayout = VulkanEngine::cinstance->_forwardPipelineLayout;

	//build the mesh triangle pipeline
	_forwardPipeline = pipelineBuilder.build_pipeline(VulkanEngine::cinstance->_device, _defaultRenderPass);

	VulkanEngine::cinstance->create_material(_forwardPipeline, VulkanEngine::cinstance->_forwardPipelineLayout, "defaultmesh");


	//texture pipeline

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, meshVertShader));

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, texturedMeshShader));

	VkPipeline texPipeline = pipelineBuilder.build_pipeline(VulkanEngine::cinstance->_device, _defaultRenderPass);
	VulkanEngine::cinstance->create_material(texPipeline, VulkanEngine::cinstance->_forwardPipelineLayout, "texturedmesh");

	//deleting all of the vulkan shaders
	vkDestroyShaderModule(VulkanEngine::cinstance->_device, texturedMeshShader, nullptr);
	vkDestroyShaderModule(VulkanEngine::cinstance->_device, colorMeshShader, nullptr);
	vkDestroyShaderModule(VulkanEngine::cinstance->_device, meshVertShader, nullptr);

	//adding the pipelines to the deletion queue
	VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
		vkDestroyPipeline(VulkanEngine::cinstance->_device, _forwardPipeline, nullptr);
		vkDestroyPipeline(VulkanEngine::cinstance->_device, texPipeline, nullptr);

		vkDestroyPipelineLayout(VulkanEngine::cinstance->_device, VulkanEngine::cinstance->_forwardPipelineLayout, nullptr);
		});
}

void GRAPHICS::Renderer::create_deferred_pipelines()
{
	//DEFERRED PIPELINES

	//SHADERS LOADING

	VkShaderModule deferredVertex;
	if (!VulkanEngine::cinstance->load_shader_module("../shaders/deferred.vert.spv", &deferredVertex))
	{
		std::cout << "Error when building the deferred vertex shader" << std::endl;
	}
	else
	{
		std::cout << "Deferred vertex shader succesfully loaded" << endl;
	}

	VkShaderModule deferredFrag;
	if (!VulkanEngine::cinstance->load_shader_module("../shaders/deferred.frag.spv", &deferredFrag))
	{
		std::cout << "Error when building the deferred frag shader" << std::endl;
	}
	else
	{
		std::cout << "Frag vertex shader succesfully loaded" << endl;
	}

	VkShaderModule lightVertex;
	if (!VulkanEngine::cinstance->load_shader_module("../shaders/light.vert.spv", &lightVertex))
	{
		std::cout << "Error when building the light vertex shader" << std::endl;
	}
	else
	{
		std::cout << "Light vertex shader succesfully loaded" << endl;
	}

	VkShaderModule lightFrag;
	if (!VulkanEngine::cinstance->load_shader_module("../shaders/light.frag.spv", &lightFrag))
	{
		std::cout << "Error when building the light frag shader" << std::endl;
	}
	else
	{
		std::cout << "Light frag shader succesfully loaded" << endl;
	}

	//LAYOUTS
	VkPipelineLayoutCreateInfo layoutInfo = vkinit::pipeline_layout_create_info();

	std::array<VkDescriptorSetLayout, 3> deferredSetLayouts = { VulkanEngine::cinstance->_camSetLayout, VulkanEngine::cinstance->_objectSetLayout, VulkanEngine::cinstance->_singleTextureSetLayout };

	layoutInfo.pushConstantRangeCount = 0;
	layoutInfo.pPushConstantRanges = nullptr;
	layoutInfo.setLayoutCount = static_cast<uint32_t>(deferredSetLayouts.size());
	layoutInfo.pSetLayouts = deferredSetLayouts.data();

	VK_CHECK(vkCreatePipelineLayout(VulkanEngine::cinstance->_device, &layoutInfo, nullptr, &VulkanEngine::cinstance->_deferredPipelineLayout));

	std::array<VkDescriptorSetLayout, 2> lightSetLayouts = { VulkanEngine::cinstance->_globalSetLayout, _gbuffersSetLayout };

	layoutInfo.setLayoutCount = static_cast<uint32_t>(lightSetLayouts.size());
	layoutInfo.pSetLayouts = lightSetLayouts.data();

	VK_CHECK(vkCreatePipelineLayout(VulkanEngine::cinstance->_device, &layoutInfo, nullptr, &VulkanEngine::cinstance->_lightPipelineLayout));

	//DEFERRED PIPELINE CREATION
	PipelineBuilder pipelineBuilder;

	//Deferred Vertex Info
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	VertexInputDescription vertexDescription = Vertex::get_vertex_description();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexDescription.attributes.size());
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexDescription.bindings.size());
	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();

	//Deferred Assembly Info
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	//Viewport and Scissor
	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float)VulkanEngine::cinstance->_windowExtent.width;
	pipelineBuilder._viewport.height = (float)VulkanEngine::cinstance->_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;

	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = VulkanEngine::cinstance->_windowExtent;

	//Deferred Depth Stencil
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	//Deferred Rasterizer
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

	//Deferred Multisampling
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	//Deferred Color Blend Attachment
	pipelineBuilder._colorBlendAttachment.push_back(vkinit::color_blend_attachment_state());
	pipelineBuilder._colorBlendAttachment.push_back(vkinit::color_blend_attachment_state());
	pipelineBuilder._colorBlendAttachment.push_back(vkinit::color_blend_attachment_state());

	//Deferred Shaders
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, deferredVertex));
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, deferredFrag));

	//Deferred Layout
	pipelineBuilder._pipelineLayout = VulkanEngine::cinstance->_deferredPipelineLayout;

	_deferredPipeline = pipelineBuilder.build_pipeline(VulkanEngine::cinstance->_device, _deferredRenderPass);

	//LIGHT PIPELINE CREATION

	//Light Depth Stencil
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(false, false, VK_COMPARE_OP_ALWAYS);

	//Deferred Color Blend Attachment
	pipelineBuilder._colorBlendAttachment.clear();
	pipelineBuilder._colorBlendAttachment.push_back(vkinit::color_blend_attachment_state());

	//Light Shaders
	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, lightVertex));
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, lightFrag));

	//Light Layout
	pipelineBuilder._pipelineLayout = VulkanEngine::cinstance->_lightPipelineLayout;

	_lightPipeline = pipelineBuilder.build_pipeline(VulkanEngine::cinstance->_device, _defaultRenderPass);


	//DELETIONS

	vkDestroyShaderModule(VulkanEngine::cinstance->_device, lightFrag, nullptr);
	vkDestroyShaderModule(VulkanEngine::cinstance->_device, lightVertex, nullptr);
	vkDestroyShaderModule(VulkanEngine::cinstance->_device, deferredFrag, nullptr);
	vkDestroyShaderModule(VulkanEngine::cinstance->_device, deferredVertex, nullptr);

	VulkanEngine::cinstance->_mainDeletionQueue.push_function([=]() {
		vkDestroyPipeline(VulkanEngine::cinstance->_device, _deferredPipeline, nullptr);
		vkDestroyPipeline(VulkanEngine::cinstance->_device, _lightPipeline, nullptr);

		vkDestroyPipelineLayout(VulkanEngine::cinstance->_device, VulkanEngine::cinstance->_deferredPipelineLayout, nullptr);
		vkDestroyPipelineLayout(VulkanEngine::cinstance->_device, VulkanEngine::cinstance->_lightPipelineLayout, nullptr);
		});
}

VkPipeline PipelineBuilder::build_pipeline(VkDevice device, VkRenderPass pass)
{
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.pNext = nullptr;

	viewportState.viewportCount = 1;
	viewportState.pViewports = &_viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &_scissor;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.pNext = nullptr;

	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = static_cast<uint32_t>(_colorBlendAttachment.size());
	colorBlending.pAttachments = _colorBlendAttachment.data();


	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = nullptr;

	pipelineInfo.stageCount = static_cast<uint32_t>(_shaderStages.size());
	pipelineInfo.pStages = _shaderStages.data();
	pipelineInfo.pVertexInputState = &_vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &_inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pDepthStencilState = &_depthStencil;
	pipelineInfo.pRasterizationState = &_rasterizer;
	pipelineInfo.pMultisampleState = &_multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.layout = _pipelineLayout;
	pipelineInfo.renderPass = pass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	VkPipeline newPipeline;
	if (vkCreateGraphicsPipelines(
		device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS)
	{
		std::cout << "failed to create pipeline\n";
		return VK_NULL_HANDLE;
	}
	else
	{
		return newPipeline;
	}
}