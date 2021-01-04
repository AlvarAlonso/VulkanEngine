#include "vk_renderer.h"
#include "vk_initializers.h"
#include "vk_engine.h"
#include "vk_utils.h"
#include <iostream>

#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>

#include <array>

Renderer::Renderer()
{
	re = new RenderEngine();
	re->init();
	
	_physicalDevice = re->_physicalDevice;
	_device = RenderEngine::_device;
	_allocator = re->_allocator;
	_graphicsQueue = re->_graphicsQueue;
	_graphicsQueueFamily = re->_graphicsQueueFamily;
	_renderMode = RENDER_MODE_DEFERRED;

	init_renderer();
}

void Renderer::cleanup()
{
	re->cleanup();
}

SDL_Window* Renderer::get_sdl_window()
{
	return re->_window;
}

void Renderer::init_renderer()
{
	init_commands();
	init_sync_structures();
	create_descriptor_buffers();
	init_descriptors();

	deferred_quad.create_quad(1);

	Mesh mesh("../assets/lost_empire.obj");
	someMesh = mesh;

	RenderObject object;
	object._model = glm::mat4(1);
	object._mesh = &someMesh;

	_renderables.push_back(object);

	//deferred_quad = someMesh;
}

void Renderer::draw_scene()
{
	if(!isDeferredCommandInit)
	{
		record_deferred_command_buffers(_renderables.data(), _renderables.size());
		isDeferredCommandInit = true;
	}

	//ImGui::Render();
	
	if(_renderMode == RENDER_MODE_FORWARD)
	{
		render_forward();
	}
	else if(_renderMode == RENDER_MODE_DEFERRED)
	{
		render_deferred();
	}
	else if(_renderMode == RENDER_MODE_RAYTRACING)
	{
		render_raytracing();
	}
}

void Renderer::init_raytracing()
{
	// Get the ray tracing and accelertion structure related function pointers required by this sample
	vkGetBufferDeviceAddressKHR = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(vkGetDeviceProcAddr(_device, "vkGetBufferDeviceAddressKHR"));
	vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(_device, "vkCmdBuildAccelerationStructuresKHR"));
	vkBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(_device, "vkBuildAccelerationStructuresKHR"));
	vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(_device, "vkCreateAccelerationStructureKHR"));
	vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(_device, "vkDestroyAccelerationStructureKHR"));
	vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(_device, "vkGetAccelerationStructureBuildSizesKHR"));
	vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(_device, "vkGetAccelerationStructureDeviceAddressKHR"));
	vkCmdTraceRaysKHR = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(vkGetDeviceProcAddr(_device, "vkCmdTraceRaysKHR"));
	vkGetRayTracingShaderGroupHandlesKHR = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(vkGetDeviceProcAddr(_device, "vkGetRayTracingShaderGroupHandlesKHR"));
	vkCreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(vkGetDeviceProcAddr(_device, "vkCreateRayTracingPipelinesKHR"));

	re->_rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
	VkPhysicalDeviceProperties2 deviceProperties2{};
	deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	deviceProperties2.pNext = &re->_rayTracingPipelineProperties;
	vkGetPhysicalDeviceProperties2(_physicalDevice, &deviceProperties2);

	// Requesting ray tracing features
	re->_accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	VkPhysicalDeviceFeatures2 deviceFeatures2{};
	deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	deviceFeatures2.pNext = &re->_accelerationStructureFeatures;
	vkGetPhysicalDeviceFeatures2(_physicalDevice, &deviceFeatures2);

	create_bottom_level_acceleration_structure();

	create_top_level_acceleration_structure();

	create_storage_image();

	create_uniform_buffer();

	create_raytracing_pipeline();

	create_shader_binding_table();

	create_raytracing_descriptor_sets();
}

void Renderer::create_bottom_level_acceleration_structure()
{
	std::vector<BlasInput> allBlas;
	allBlas.reserve(_renderables.size());

	_bottomLevelAS.reserve(allBlas.size());
	_transformBuffers.reserve(allBlas.size());

	for(const auto& renderable : _renderables)
	{
		auto blas = renderable_to_vulkan_geometry(renderable);
		allBlas.push_back(blas);
	}

	build_blas(allBlas);
}

void Renderer::create_top_level_acceleration_structure()
{
	std::vector<VkAccelerationStructureInstanceKHR> instances;
	instances.reserve(_renderables.size());

	for (int i = 0; i < _renderables.size(); i++)
	{
		glm::mat4 model = glm::transpose(_renderables[i]._model);

		VkTransformMatrixKHR transformMatrix = {
			model[0].x, model[0].y, model[0].z, model[0].w,
			model[1].x, model[1].y, model[1].z, model[1].w,
			model[2].x, model[2].y, model[2].z, model[2].w,
		};

		VkAccelerationStructureInstanceKHR instance{};
		instance.transform = transformMatrix;
		instance.instanceCustomIndex = i;
		instance.mask = 0xFF;
		instance.instanceShaderBindingTableRecordOffset = 0;
		instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		instance.accelerationStructureReference = _bottomLevelAS[i]._deviceAddress;

		instances.push_back(instance);
	}

	// Buffer for instance data
	AllocatedBuffer instancesBuffer;
	instancesBuffer = vkutil::create_buffer(_allocator,
		sizeof(VkAccelerationStructureInstanceKHR) * instances.size(),
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VMA_MEMORY_USAGE_CPU_TO_GPU);

	void* data;
	vmaMapMemory(_allocator, instancesBuffer._allocation, &data);
	memcpy(data, instances.data(), sizeof(VkAccelerationStructureInstanceKHR) * instances.size());
	vmaUnmapMemory(_allocator, instancesBuffer._allocation);

	VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
	instanceDataDeviceAddress.deviceAddress = vkutil::get_buffer_device_address(_device, instancesBuffer._buffer);
	

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

	uint32_t primitive_count = instances.size();

	VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
	accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	vkGetAccelerationStructureBuildSizesKHR(
		_device,
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

	vkCreateAccelerationStructureKHR(_device, &accelerationStructureCreateInfo, nullptr, &_topLevelAS._handle);

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
	accelerationStructureBuildRangeInfo.primitiveCount = instances.size();
	accelerationStructureBuildRangeInfo.primitiveOffset = 0;
	accelerationStructureBuildRangeInfo.firstVertex = 0;
	accelerationStructureBuildRangeInfo.transformOffset = 0;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };

	if (re->_accelerationStructureFeatures.accelerationStructureHostCommands)
	{
		// Implementation supports building acceleration structure building on host
		vkBuildAccelerationStructuresKHR(
			_device,
			VK_NULL_HANDLE,
			1,
			&accelerationBuildGeometryInfo,
			accelerationBuildStructureRangeInfos.data());
	}
	else
	{
		// Acceleration structure needs to be build on the device
		vkupload::immediate_submit([&](VkCommandBuffer cmd)
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
	_topLevelAS._deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(_device, &accelerationDeviceAddressInfo);

	re->_mainDeletionQueue.push_function([=]() {
		vkDestroyAccelerationStructureKHR(_device, _topLevelAS._handle, nullptr);
		});

	delete_scratch_buffer(scratchBuffer);

	vmaDestroyBuffer(_allocator, instancesBuffer._buffer, instancesBuffer._allocation);
}

void Renderer::create_storage_image()
{
	VkExtent3D extent = 
	{
		re->_windowExtent.width,
		re->_windowExtent.height,
		1
	};

	VkImageCreateInfo image = vkinit::image_create_info(VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT, extent);
	image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK(vkCreateImage(_device, &image, nullptr, &_storageImage));

	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements(_device, _storageImage, &memReqs);

	VkMemoryAllocateInfo memoryAllocateInfo = {};
	memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocateInfo.allocationSize = memReqs.size;
	memoryAllocateInfo.memoryTypeIndex = vkutil::find_memory_type_index(_physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VK_CHECK(vkAllocateMemory(_device, &memoryAllocateInfo, nullptr, &_storageImageMemory));
	VK_CHECK(vkBindImageMemory(_device, _storageImage, _storageImageMemory, 0));

	VkImageViewCreateInfo colorImageView = vkinit::imageview_create_info(VK_FORMAT_B8G8R8A8_UNORM, _storageImage, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(_device, &colorImageView, nullptr, &_storageImageView));

	vkupload::immediate_submit([&](VkCommandBuffer cmd) {
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

void Renderer::create_uniform_buffer()
{
	_ubo = vkutil::create_buffer(_allocator, sizeof(uniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VMA_MEMORY_USAGE_CPU_TO_GPU);

	update_uniform_buffers();
}

void Renderer::update_uniform_buffers()
{
	//glm::vec3 camPos = { 0.0f, 0.0f, -2.5f };
	//glm::mat4 view = glm::translate(glm::mat4(1.0f), camPos);
	glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1700.0f / 900.0f, 0.1f, 512.0f);
	projection[1][1] *= -1;

	void* data;
	vmaMapMemory(_allocator, _ubo._allocation, &data);
	uniformData.projInverse = glm::inverse(projection);
	uniformData.viewInverse = glm::inverse(VulkanEngine::cinstance->camera->getView());
	memcpy(data, &uniformData, sizeof(uniformData));
	vmaUnmapMemory(_allocator, _ubo._allocation);
}

void Renderer::create_raytracing_pipeline()
{
	VkDescriptorSetLayoutBinding accelerationStructureLayoutBinding{};
	accelerationStructureLayoutBinding.binding = 0;
	accelerationStructureLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	accelerationStructureLayoutBinding.descriptorCount = 1;
	accelerationStructureLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

	VkDescriptorSetLayoutBinding resultImageLayoutBinding{};
	resultImageLayoutBinding.binding = 1;
	resultImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	resultImageLayoutBinding.descriptorCount = 1;
	resultImageLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

	VkDescriptorSetLayoutBinding uniformBufferBinding{};
	uniformBufferBinding.binding = 2;
	uniformBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uniformBufferBinding.descriptorCount = 1;
	uniformBufferBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

	VkDescriptorSetLayoutBinding vertexBufferBinding{};
	vertexBufferBinding.binding = 3;
	vertexBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	vertexBufferBinding.descriptorCount = _renderables.size();
	vertexBufferBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

	VkDescriptorSetLayoutBinding indexBufferBinding{};
	indexBufferBinding.binding = 4;
	indexBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	indexBufferBinding.descriptorCount = _renderables.size();
	indexBufferBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

	VkDescriptorSetLayoutBinding transformBufferBinding{};
	transformBufferBinding.binding = 5;
	transformBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	transformBufferBinding.descriptorCount = _renderables.size();
	transformBufferBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

	std::vector<VkDescriptorSetLayoutBinding> bindings({
		accelerationStructureLayoutBinding,
		resultImageLayoutBinding,
		uniformBufferBinding,
		vertexBufferBinding,
		indexBufferBinding,
		transformBufferBinding
		});

	VkDescriptorSetLayoutCreateInfo desc_set_layout_info{};
	desc_set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	desc_set_layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
	desc_set_layout_info.pBindings = bindings.data();
	VK_CHECK(vkCreateDescriptorSetLayout(_device, &desc_set_layout_info, nullptr, &_rayTracingSetLayout));

	VkPipelineLayoutCreateInfo pipeline_layout_info{};
	pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &_rayTracingSetLayout;
	VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_rayTracingPipelineLayout));

	/*
		Setup ray tracing shader groups
	*/
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

	// Ray generation group
	{
		VkShaderModule raygenShader;
		if (!vkutil::load_shader_module(_device, "../shaders/raygen.rgen.spv", &raygenShader))
		{
			std::cout << "Error when building the ray generation shader module" << std::endl;
		}
		else {
			std::cout << "Ray generation shader succesfully loaded" << std::endl;
		}

		VkPipelineShaderStageCreateInfo shader_stage_info = vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_RAYGEN_BIT_KHR, raygenShader);

		shaderStages.push_back(shader_stage_info);
		VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
		shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		_shaderGroups.push_back(shaderGroup);
	}

	// Miss group
	{
		VkShaderModule missShader;
		if (!vkutil::load_shader_module(_device, "../shaders/miss.rmiss.spv", &missShader))
		{
			std::cout << "Error when building the miss shader module" << std::endl;
		}
		else {
			std::cout << "Miss shader succesfully loaded" << std::endl;
		}

		VkPipelineShaderStageCreateInfo shader_stage_info = vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_MISS_BIT_KHR, missShader);

		shaderStages.push_back(shader_stage_info);
		VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
		shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		_shaderGroups.push_back(shaderGroup);
	}

	// Closest hit group
	{
		VkShaderModule closestHitShader;
		if (!vkutil::load_shader_module(_device, "../shaders/closestHit.rchit.spv", &closestHitShader))
		{
			std::cout << "Error when building the closest hit shader module" << std::endl;
		}
		else {
			std::cout << "Closest hit shader succesfully loaded" << std::endl;
		}

		VkPipelineShaderStageCreateInfo shader_stage_info = vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, closestHitShader);

		shaderStages.push_back(shader_stage_info);
		VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
		shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
		shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.closestHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		_shaderGroups.push_back(shaderGroup);
	}

	/*
		Create the ray tracing pipeline
	*/
	VkRayTracingPipelineCreateInfoKHR raytracing_pipeline_info{};
	raytracing_pipeline_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
	raytracing_pipeline_info.stageCount = static_cast<uint32_t>(shaderStages.size());
	raytracing_pipeline_info.pStages = shaderStages.data();
	raytracing_pipeline_info.groupCount = static_cast<uint32_t>(_shaderGroups.size());
	raytracing_pipeline_info.pGroups = _shaderGroups.data();
	raytracing_pipeline_info.maxPipelineRayRecursionDepth = 1;
	raytracing_pipeline_info.layout = _rayTracingPipelineLayout;
	VK_CHECK(vkCreateRayTracingPipelinesKHR(_device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &raytracing_pipeline_info, nullptr, &_rayTracingPipeline));
}

void Renderer::create_shader_binding_table()
{
	const uint32_t handleSize = re->_rayTracingPipelineProperties.shaderGroupHandleSize;
	const uint32_t handleSizeAligned = vkutil::get_aligned_size(re->_rayTracingPipelineProperties.shaderGroupHandleSize, re->_rayTracingPipelineProperties.shaderGroupHandleAlignment);
	const uint32_t groupCount = static_cast<uint32_t>(_shaderGroups.size());
	const uint32_t sbtSize = groupCount * handleSizeAligned;

	std::vector<uint8_t> shaderHandleStorage(sbtSize);
	VK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(_device, _rayTracingPipeline, 0, groupCount, sbtSize, shaderHandleStorage.data()));

	const VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	const VmaMemoryUsage memoryUsageFlags = VMA_MEMORY_USAGE_CPU_TO_GPU;

	_raygenShaderBindingTable = vkutil::create_buffer(
		_allocator, handleSize, bufferUsageFlags, memoryUsageFlags);

	_missShaderBindingTable = vkutil::create_buffer(
		_allocator, handleSize, bufferUsageFlags, memoryUsageFlags);

	_hitShaderBindingTable = vkutil::create_buffer(
		_allocator, handleSize, bufferUsageFlags, memoryUsageFlags);

	// Copy handles
	void* raygen_data;
	vmaMapMemory(_allocator, _raygenShaderBindingTable._allocation, &raygen_data);
	memcpy(raygen_data, shaderHandleStorage.data(), handleSize);
	vmaUnmapMemory(_allocator, _raygenShaderBindingTable._allocation);

	void* miss_data;
	vmaMapMemory(_allocator, _missShaderBindingTable._allocation, &miss_data);
	memcpy(miss_data, shaderHandleStorage.data() + handleSizeAligned, handleSize);
	vmaUnmapMemory(_allocator, _missShaderBindingTable._allocation);

	void* hit_data;
	vmaMapMemory(_allocator, _hitShaderBindingTable._allocation, &hit_data);
	memcpy(hit_data, shaderHandleStorage.data() + handleSizeAligned * 2, handleSize);
	vmaUnmapMemory(_allocator, _hitShaderBindingTable._allocation);
}

void Renderer::create_raytracing_descriptor_sets()
{
	std::vector<VkDescriptorPoolSize> poolSizes = {
		{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}
	};

	VkDescriptorPoolCreateInfo dp_info = {};
	dp_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dp_info.pNext = nullptr;
	dp_info.flags = 0;
	dp_info.maxSets = 1;
	dp_info.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	dp_info.pPoolSizes = poolSizes.data();

	VK_CHECK(vkCreateDescriptorPool(_device, &dp_info, nullptr, &_rayTracingDescriptorPool));

	re->_mainDeletionQueue.push_function([=]() {
		vkDestroyDescriptorPool(_device, _rayTracingDescriptorPool, nullptr);
		});

	VkDescriptorSetAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc_info.pNext = nullptr;
	alloc_info.descriptorPool = _rayTracingDescriptorPool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &_rayTracingSetLayout;

	VK_CHECK(vkAllocateDescriptorSets(_device, &alloc_info, &_rayTracingDescriptorSet));

	VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo{};
	descriptorAccelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
	descriptorAccelerationStructureInfo.pAccelerationStructures = &_topLevelAS._handle;

	VkWriteDescriptorSet accelerationStructureWrite{};
	accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	// The specialized acceleration structure descriptor has to be chained
	accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
	accelerationStructureWrite.dstSet = _rayTracingDescriptorSet;
	accelerationStructureWrite.dstBinding = 0;
	accelerationStructureWrite.descriptorCount = 1;
	accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

	VkDescriptorImageInfo storageImageDescriptor{};
	storageImageDescriptor.imageView = _storageImageView;
	storageImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkDescriptorBufferInfo uboBufferDescriptor{};
	uboBufferDescriptor.offset = 0;
	uboBufferDescriptor.buffer = _ubo._buffer;
	uboBufferDescriptor.range = sizeof(uniformData);

	std::vector<VkDescriptorBufferInfo> verticesBufferInfos;
	std::vector<VkDescriptorBufferInfo> indicesBufferInfos;
	std::vector<VkDescriptorBufferInfo> transformBufferInfos;

	std::vector<RenderObject>& renderables = _renderables;

	verticesBufferInfos.reserve(renderables.size());
	indicesBufferInfos.reserve(renderables.size());
	transformBufferInfos.reserve(renderables.size());

	for(int i = 0; i < renderables.size(); i++)
	{
		std::vector<rtVertex> rtvs;
		rtvs.reserve(renderables[i]._mesh->_vertices.size());

		for(const auto& vertex : renderables[i]._mesh->_vertices)
		{
			rtVertex rtv = { {glm::vec4(vertex.position, 1.0f)}, {glm::vec4(vertex.normal, 1.0f)}, {glm::vec4(vertex.uv, 1.0f, 1.0f)} };
			rtvs.push_back(rtv);
		}

		AllocatedBuffer rtVertexBuffer = vkutil::create_buffer(_allocator, rtvs.size() * sizeof(rtVertex), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		VkDescriptorBufferInfo vertexBufferInfo{};
		vertexBufferInfo.offset = 0;
		vertexBufferInfo.buffer = rtVertexBuffer._buffer;
		vertexBufferInfo.range = sizeof(rtVertex) * rtvs.size();

		void* vertexData;
		vmaMapMemory(_allocator, rtVertexBuffer._allocation, &vertexData);
		memcpy(vertexData, rtvs.data(), sizeof(rtVertex) * rtvs.size());
		vmaUnmapMemory(_allocator, rtVertexBuffer._allocation);

		verticesBufferInfos.push_back(vertexBufferInfo);

		VkDescriptorBufferInfo indexBufferInfo{};
		indexBufferInfo.offset = 0;
		indexBufferInfo.buffer = renderables[i]._mesh->_indexBuffer._buffer;
		indexBufferInfo.range = sizeof(uint32_t) * renderables[i]._mesh->_indices.size();

		indicesBufferInfos.push_back(indexBufferInfo);

		AllocatedBuffer transformBuffer = vkutil::create_buffer(_allocator, sizeof(glm::mat4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		VkDescriptorBufferInfo transformBufferInfo{};
		transformBufferInfo.offset = 0;
		transformBufferInfo.buffer = transformBuffer._buffer;
		transformBufferInfo.range = sizeof(glm::mat4);

		transformBufferInfos.push_back(transformBufferInfo);

		void* data;
		vmaMapMemory(_allocator, transformBuffer._allocation, &data);
		memcpy(data, &renderables[i]._model, sizeof(glm::mat4));
		vmaUnmapMemory(_allocator, transformBuffer._allocation);
	}

	VkWriteDescriptorSet resultImageWrite = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _rayTracingDescriptorSet, &storageImageDescriptor, 1);
	VkWriteDescriptorSet uniformBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _rayTracingDescriptorSet, &uboBufferDescriptor, 2);
	VkWriteDescriptorSet vertexBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rayTracingDescriptorSet, verticesBufferInfos.data(), 3, renderables.size());
	VkWriteDescriptorSet indexBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rayTracingDescriptorSet, indicesBufferInfos.data(), 4, renderables.size());
	VkWriteDescriptorSet transformBufferWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _rayTracingDescriptorSet, transformBufferInfos.data(), 5, renderables.size());

	std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
		accelerationStructureWrite,
		resultImageWrite,
		uniformBufferWrite,
		vertexBufferWrite,
		indexBufferWrite,
		transformBufferWrite
	};

	vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
}

void Renderer::record_raytracing_command_buffer(VkCommandBuffer cmd, uint32_t swapchainImageIndex)
{
	VkCommandBufferBeginInfo cmdBufInfo = vkinit::command_buffer_begin_info();

	VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBufInfo));
	
	/*
		Setup the buffer regions pointing to the shaders in our shader binding table
	*/

	const uint32_t handleSizeAligned = vkutil::get_aligned_size(re->_rayTracingPipelineProperties.shaderGroupHandleSize, re->_rayTracingPipelineProperties.shaderGroupHandleAlignment);

	VkBufferDeviceAddressInfoKHR raygenDeviceAddressInfo{};
	raygenDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	raygenDeviceAddressInfo.buffer = _raygenShaderBindingTable._buffer;

	VkStridedDeviceAddressRegionKHR raygenShaderSbtEntry{};
	raygenShaderSbtEntry.deviceAddress = vkGetBufferDeviceAddress(_device, &raygenDeviceAddressInfo);
	raygenShaderSbtEntry.stride = handleSizeAligned;
	raygenShaderSbtEntry.size = handleSizeAligned;

	VkBufferDeviceAddressInfoKHR missDeviceAddressInfo{};
	missDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	missDeviceAddressInfo.buffer = _missShaderBindingTable._buffer;

	VkStridedDeviceAddressRegionKHR missShaderSbtEntry{};
	missShaderSbtEntry.deviceAddress = vkGetBufferDeviceAddress(_device, &missDeviceAddressInfo);
	missShaderSbtEntry.stride = handleSizeAligned;
	missShaderSbtEntry.size = handleSizeAligned;

	VkBufferDeviceAddressInfoKHR hitDeviceAddressInfo{};
	hitDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	hitDeviceAddressInfo.buffer = _hitShaderBindingTable._buffer;

	VkStridedDeviceAddressRegionKHR hitShaderSbtEntry{};
	hitShaderSbtEntry.deviceAddress = vkGetBufferDeviceAddress(_device, &hitDeviceAddressInfo);
	hitShaderSbtEntry.stride = handleSizeAligned;
	hitShaderSbtEntry.size = handleSizeAligned;

	VkStridedDeviceAddressRegionKHR callableShaderSbtEntry{};

	/*
		Dispatch the ray tracing commands
	*/
	
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _rayTracingPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _rayTracingPipelineLayout, 0, 1, &_rayTracingDescriptorSet, 0, 0);
	
	vkCmdTraceRaysKHR(
		cmd,
		&raygenShaderSbtEntry,
		&missShaderSbtEntry,
		&hitShaderSbtEntry,
		&callableShaderSbtEntry,
		re->_windowExtent.width,
		re->_windowExtent.height,
		1);
	
	/*
		Copy ray tracing output to swap chain image
	*/
	
	// Prepare current swap chain image as transfer destination
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = re->_swapchainImages[swapchainImageIndex];
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);
	}

	// Prepare ray tracing output image as transfer source
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = _storageImage;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);
	}

	VkImageCopy copyRegion{};
	copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	copyRegion.srcOffset = { 0, 0, 0 };
	copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	copyRegion.dstOffset = { 0, 0, 0 };
	copyRegion.extent = { re->_windowExtent.width, re->_windowExtent.height, 1 };
	vkCmdCopyImage(cmd, _storageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, re->_swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
		
	// Transition swap chain image back for presentation
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = re->_swapchainImages[swapchainImageIndex];
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = 0;

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);
	}
	
	// Transition ray tracing output image back to general layout
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = _storageImage;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);
	}
	
	VK_CHECK(vkEndCommandBuffer(cmd));
}

RayTracingScratchBuffer Renderer::create_scratch_buffer(VkDeviceSize size)
{
	RayTracingScratchBuffer scratchBuffer{};

	VkBufferCreateInfo bufferCreateInfo{};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = size;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	VK_CHECK(vkCreateBuffer(_device, &bufferCreateInfo, nullptr, &scratchBuffer._buffer));

	VkMemoryRequirements memoryRequirements{};
	vkGetBufferMemoryRequirements(_device, scratchBuffer._buffer, &memoryRequirements);

	VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo{};
	memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

	VkMemoryAllocateInfo memoryAllocateInfo = {};
	memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
	memoryAllocateInfo.allocationSize = memoryRequirements.size;
	memoryAllocateInfo.memoryTypeIndex = vkutil::find_memory_type_index(_physicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK(vkAllocateMemory(_device, &memoryAllocateInfo, nullptr, &scratchBuffer._memory));
	VK_CHECK(vkBindBufferMemory(_device, scratchBuffer._buffer, scratchBuffer._memory, 0));

	VkBufferDeviceAddressInfoKHR bufferDeviceAddressInfo{};
	bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	bufferDeviceAddressInfo.buffer = scratchBuffer._buffer;
	scratchBuffer._deviceAddress = vkGetBufferDeviceAddressKHR(_device, &bufferDeviceAddressInfo);

	return scratchBuffer;
}

void Renderer::delete_scratch_buffer(RayTracingScratchBuffer& scratchBuffer)
{
	if(scratchBuffer._memory != VK_NULL_HANDLE) {
		vkFreeMemory(_device, scratchBuffer._memory, nullptr);
	}
	if(scratchBuffer._buffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(_device, scratchBuffer._buffer, nullptr);
	}
}

void Renderer::create_acceleration_structure_buffer(AccelerationStructure &accelerationStructure, VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo)
{
	VkBufferCreateInfo bufferCreateInfo{};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = buildSizeInfo.accelerationStructureSize;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

	VK_CHECK(vkCreateBuffer(_device, &bufferCreateInfo, nullptr, &accelerationStructure._buffer));

	VkMemoryRequirements memoryRequirements{};
	vkGetBufferMemoryRequirements(_device, accelerationStructure._buffer, &memoryRequirements);

	VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo{};
	memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

	VkMemoryAllocateInfo memoryAllocateInfo{};
	memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
	memoryAllocateInfo.allocationSize = memoryRequirements.size;
	memoryAllocateInfo.memoryTypeIndex = vkutil::find_memory_type_index(_physicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VK_CHECK(vkAllocateMemory(_device, &memoryAllocateInfo, nullptr, &accelerationStructure._memory));
	VK_CHECK(vkBindBufferMemory(_device, accelerationStructure._buffer, accelerationStructure._memory, 0));

	re->_mainDeletionQueue.push_function([=]() {
		vkFreeMemory(_device, accelerationStructure._memory, nullptr);
		vkDestroyBuffer(_device, accelerationStructure._buffer, nullptr);
		});
}

void Renderer::init_commands()
{
	//create a command pool for commands submitted to the graphics queue.
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

		//allocat the default command buffer that we will use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));

		re->_mainDeletionQueue.push_function([=]() {
			vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
			});
	}

	VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_deferredCommandPool));

	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_deferredCommandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_deferredCommandBuffer));

	re->_mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(_device, _deferredCommandPool, nullptr);
		});
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

	VK_CHECK(vkCreateSemaphore(_device, &offscreenSemaphoreInfo, nullptr, &_offscreenSemaphore));

	re->_mainDeletionQueue.push_function([=]() {
		vkDestroySemaphore(_device, _offscreenSemaphore, nullptr);
		});

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

		re->_mainDeletionQueue.push_function([=]() {
			vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
			});

		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._presentSemaphore));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));

		re->_mainDeletionQueue.push_function([=]() {
			vkDestroySemaphore(_device, _frames[i]._presentSemaphore, nullptr);
			vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
			});
	}
}

void Renderer::create_descriptor_buffers()
{
	const size_t sceneParamBufferSize = FRAME_OVERLAP * vkutil::get_aligned_size(sizeof(GPUSceneData), re->_gpuProperties.limits.minUniformBufferOffsetAlignment);

	_sceneParameterBuffer = vkutil::create_buffer(_allocator, sceneParamBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		_frames[i].objectBuffer = vkutil::create_buffer(_allocator, sizeof(GPUObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		_frames[i].cameraBuffer = vkutil::create_buffer(_allocator, sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		//cam buffer
		_camBuffer = vkutil::create_buffer(_allocator, sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	}

	//deferred
	_objectBuffer = vkutil::create_buffer(_allocator, sizeof(GPUObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
}

void Renderer::record_deferred_command_buffers(RenderObject* first, int count)
{
	//FIRST PASS

	VkCommandBufferBeginInfo deferredCmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

	VK_CHECK(vkBeginCommandBuffer(_deferredCommandBuffer, &deferredCmdBeginInfo));

	VkClearValue first_clearValue, position, normal;
	first_clearValue.color = { {0.3f, 0.0f, 0.0f, 1.0f} };
	position.color = { 0.0f, 0.3f, 0.0f, 1.0f };
	normal.color = { 0.0f, 0.0f, 0.3f, 1.0f };

	VkClearValue first_depthClear;
	first_depthClear.depthStencil.depth = 1.0f;

	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(re->_deferredRenderPass, re->_windowExtent, re->_offscreen_framebuffer);

	std::array<VkClearValue, 4> first_clearValues = { position, normal, first_clearValue, first_depthClear };

	rpInfo.clearValueCount = static_cast<uint32_t>(first_clearValues.size());
	rpInfo.pClearValues = first_clearValues.data();

	vkCmdBeginRenderPass(_deferredCommandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(_deferredCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_deferredPipeline);

	Mesh* lastMesh = nullptr;
	for (int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];

		vkCmdBindDescriptorSets(_deferredCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_deferredPipelineLayout, 0, 1, &_camDescriptorSet, 0, nullptr);

		vkCmdBindDescriptorSets(_deferredCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_deferredPipelineLayout, 1, 1, &_objectDescriptorSet, 0, nullptr);

		if (object._material != nullptr)
		{
			if(object._material->albedoTexture != VK_NULL_HANDLE)
			{
				vkCmdBindDescriptorSets(_deferredCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_deferredPipelineLayout, 2, 1, &object._material->albedoTexture, 0, nullptr);
			}
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

void Renderer::init_descriptors()
{
	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.pNext = nullptr;
		allocInfo.descriptorPool = re->_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &re->_globalSetLayout;

		vkAllocateDescriptorSets(_device, &allocInfo, &_frames[i].globalDescriptor);

		VkDescriptorSetAllocateInfo objectSetAlloc = {};
		objectSetAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		objectSetAlloc.pNext = nullptr;
		objectSetAlloc.descriptorPool = re->_descriptorPool;
		objectSetAlloc.descriptorSetCount = 1;
		objectSetAlloc.pSetLayouts = &re->_objectSetLayout;

		vkAllocateDescriptorSets(_device, &objectSetAlloc, &_frames[i].objectDescriptor);

		VkDescriptorBufferInfo cameraInfo = {};
		cameraInfo.buffer = _frames[i].cameraBuffer._buffer;
		cameraInfo.offset = 0;
		cameraInfo.range = sizeof(GPUCameraData);

		VkDescriptorBufferInfo sceneInfo = {};
		sceneInfo.buffer = _sceneParameterBuffer._buffer;
		sceneInfo.offset = 0;
		sceneInfo.range = sizeof(GPUSceneData);

		VkDescriptorBufferInfo objectBufferInfo;
		objectBufferInfo.buffer = _frames[i].objectBuffer._buffer;
		objectBufferInfo.offset = 0;
		objectBufferInfo.range = sizeof(GPUObjectData) * MAX_OBJECTS;

		VkWriteDescriptorSet cameraWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _frames[i].globalDescriptor, &cameraInfo, 0);

		VkWriteDescriptorSet sceneWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, _frames[i].globalDescriptor, &sceneInfo, 1);

		VkWriteDescriptorSet objectWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _frames[i].objectDescriptor, &objectBufferInfo, 0);

		std::array<VkWriteDescriptorSet, 3> setWrites = { cameraWrite, sceneWrite, objectWrite };

		vkUpdateDescriptorSets(_device, static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0, nullptr);
	}

	//DEFERRED DESCRIPTORS

	VkDescriptorSetAllocateInfo objectSetAlloc = {};
	objectSetAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	objectSetAlloc.pNext = nullptr;
	objectSetAlloc.descriptorPool = re->_descriptorPool;
	objectSetAlloc.descriptorSetCount = 1;
	objectSetAlloc.pSetLayouts = &re->_objectSetLayout;

	VkDescriptorBufferInfo objectBufferInfo;
	objectBufferInfo.buffer = _objectBuffer._buffer;
	objectBufferInfo.offset = 0;
	objectBufferInfo.range = sizeof(GPUObjectData) * MAX_OBJECTS;

	vkAllocateDescriptorSets(_device, &objectSetAlloc, &_objectDescriptorSet);

	VkWriteDescriptorSet objectWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _objectDescriptorSet, &objectBufferInfo, 0);

	vkUpdateDescriptorSets(_device, 1, &objectWrite, 0, nullptr);

	VkDescriptorSetAllocateInfo cameraSetAllocInfo = {};
	cameraSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	cameraSetAllocInfo.pNext = nullptr;
	cameraSetAllocInfo.descriptorPool = re->_descriptorPool;
	cameraSetAllocInfo.descriptorSetCount = 1;
	cameraSetAllocInfo.pSetLayouts = &re->_camSetLayout;

	vkAllocateDescriptorSets(_device, &cameraSetAllocInfo, &_camDescriptorSet);

	VkDescriptorBufferInfo camBufferInfo = {};
	camBufferInfo.buffer = _camBuffer._buffer;
	camBufferInfo.offset = 0;
	camBufferInfo.range = sizeof(GPUCameraData);

	VkWriteDescriptorSet camWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _camDescriptorSet, &camBufferInfo, 0);

	vkUpdateDescriptorSets(_device, 1, &camWrite, 0, nullptr);
}

void Renderer::update_descriptors_forward(RenderObject* first, size_t count)
{
	//Update buffers info
	glm::vec3 camPos = { 0.0f, -50.0f, -10.0f };
	glm::mat4 view = glm::translate(glm::mat4(1.0f), camPos);
	glm::mat4 projection = glm::perspective(glm::radians(70.0f), 1700.0f / 900.0f, 0.1f, 200.0f);
	projection[1][1] *= -1;

	GPUCameraData camData;
	camData.projection = projection;
	camData.view = VulkanEngine::cinstance->camera->getView();
	camData.viewproj = projection * VulkanEngine::cinstance->camera->getView();

	void* data;
	vmaMapMemory(_allocator, get_current_frame().cameraBuffer._allocation, &data);
	memcpy(data, &camData, sizeof(GPUCameraData));
	vmaUnmapMemory(_allocator, get_current_frame().cameraBuffer._allocation);

	void* firstPassData;
	vmaMapMemory(_allocator, _camBuffer._allocation, &firstPassData);
	memcpy(firstPassData, &camData, sizeof(GPUCameraData));
	vmaUnmapMemory(_allocator, _camBuffer._allocation);

	float framed = { _frameNumber / 120.0f };

	_sceneParameters.ambientColor = { sin(framed), 0, cos(framed), 1 };

	char* sceneData;
	vmaMapMemory(_allocator, _sceneParameterBuffer._allocation, (void**)&sceneData);

	int frameIndex = _frameNumber % FRAME_OVERLAP;

	sceneData += vkutil::get_aligned_size(sizeof(GPUSceneData), re->_gpuProperties.limits.minUniformBufferOffsetAlignment) * frameIndex;

	memcpy(sceneData, &_sceneParameters, sizeof(GPUSceneData));

	vmaUnmapMemory(_allocator, _sceneParameterBuffer._allocation);

	void* objectData;
	vmaMapMemory(_allocator, _objectBuffer._allocation, &objectData);

	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;

	for (int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];
		objectSSBO[i].modelMatrix = object._model;
	}

	vmaUnmapMemory(_allocator, _objectBuffer._allocation);
}

void Renderer::update_descriptors(RenderObject* first, size_t count)
{
	/*
	//Update buffers info
	glm::vec3 camPos = { 0.0f, -50.0f, -10.0f };
	glm::mat4 view = glm::translate(glm::mat4(1.0f), camPos);
	glm::mat4 projection = glm::perspective(glm::radians(70.0f), 1700.0f / 900.0f, 0.1f, 200.0f);
	projection[1][1] *= -1;

	GPUCameraData camData;
	camData.projection = projection;
	camData.view = VulkanEngine::cinstance->camera->getView();
	camData.viewproj = projection * VulkanEngine::cinstance->camera->getView();
	*/
	glm::vec3 camPos = { 0.0f, 0.0f, 2.5f };
	glm::mat4 view = glm::translate(glm::mat4(1.0f), camPos);
	glm::mat4 projection = glm::perspective(glm::radians(70.0f), 1700.0f / 900.0f, 0.1f, 200.0f);
	projection[1][1] *= -1;

	GPUCameraData camData;
	camData.projection = projection;
	camData.view = view;
	camData.viewproj = projection * view;
	
	void* data;
	vmaMapMemory(_allocator, get_current_frame().cameraBuffer._allocation, &data);
	memcpy(data, &camData, sizeof(GPUCameraData));
	vmaUnmapMemory(_allocator, get_current_frame().cameraBuffer._allocation);

	float framed = { _frameNumber / 120.0f };

	_sceneParameters.ambientColor = { sin(framed), 0, cos(framed), 1 };

	char* sceneData;
	vmaMapMemory(_allocator, _sceneParameterBuffer._allocation, (void**)&sceneData);

	int frameIndex = _frameNumber % FRAME_OVERLAP;

	sceneData += vkutil::get_aligned_size(sizeof(GPUSceneData), re->_gpuProperties.limits.minUniformBufferOffsetAlignment) * frameIndex;

	memcpy(sceneData, &_sceneParameters, sizeof(GPUSceneData));

	vmaUnmapMemory(_allocator, _sceneParameterBuffer._allocation);

	void* objectData;
	vmaMapMemory(_allocator, get_current_frame().objectBuffer._allocation, &objectData);

	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;

	for (int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];
		objectSSBO[i].modelMatrix = object._model;
	}

	vmaUnmapMemory(_allocator, get_current_frame().objectBuffer._allocation);
}

int Renderer::get_current_frame_index()
{
	return _frameNumber % FRAME_OVERLAP;
}

void Renderer::render_forward()
{
	VK_CHECK(vkWaitForFences(_device, 1, &_frames[get_current_frame_index()]._renderFence, true, UINT64_MAX));
	VK_CHECK(vkResetFences(_device, 1, &_frames[get_current_frame_index()]._renderFence));

	//request image from the swapchain
	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_device, re->_swapchain, 0, _frames[get_current_frame_index()]._presentSemaphore, nullptr, &swapchainImageIndex));

	VK_CHECK(vkResetCommandBuffer(_frames[get_current_frame_index()]._mainCommandBuffer, 0));

	VkCommandBuffer cmd = _frames[get_current_frame_index()]._mainCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	VkClearValue clearValue;
	float flash = abs(sin(_frameNumber / 120.0f));
	clearValue.color = { {0.0f, 0.0f, flash, 1.0f} };

	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.0f;

	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(re->_defaultRenderPass, re->_windowExtent, re->_framebuffers[swapchainImageIndex]);

	std::array<VkClearValue, 2> clearValues = { clearValue, depthClear };

	rpInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	rpInfo.pClearValues = clearValues.data();

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	draw_forward(cmd, _renderables.data(), _renderables.size());

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

	presentInfo.pSwapchains = &re->_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &_frames[get_current_frame_index()]._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

	_frameNumber++;
}

void Renderer::render_deferred()
{
	VK_CHECK(vkWaitForFences(_device, 1, &_frames[get_current_frame_index()]._renderFence, true, UINT64_MAX));
	VK_CHECK(vkResetFences(_device, 1, &_frames[get_current_frame_index()]._renderFence));

	//request image from the swapchain
	uint32_t swapchainImageIndex;
	//VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, 0, get_current_frame()._presentSemaphore, nullptr, &swapchainImageIndex));
	VkResult result = vkAcquireNextImageKHR(_device, re->_swapchain, 0, _frames[get_current_frame_index()]._presentSemaphore, nullptr, &swapchainImageIndex);

	int idx = get_current_frame_index();
	VkCommandBuffer cmd = _frames[get_current_frame_index()]._mainCommandBuffer;

	update_descriptors(_renderables.data(), _renderables.size());

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

	presentInfo.pSwapchains = &re->_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &_frames[get_current_frame_index()]._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

	_frameNumber++;
}

void Renderer::render_raytracing()
{
	VK_CHECK(vkWaitForFences(_device, 1, &_frames[get_current_frame_index()]._renderFence, true, UINT64_MAX));
	VK_CHECK(vkResetFences(_device, 1, &_frames[get_current_frame_index()]._renderFence));

	//request image from the swapchain
	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_device, re->_swapchain, 0, _frames[get_current_frame_index()]._presentSemaphore, nullptr, &swapchainImageIndex));

	update_uniform_buffers();

	VkCommandBuffer cmd = _frames[get_current_frame_index()]._mainCommandBuffer;

	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	record_raytracing_command_buffer(cmd, swapchainImageIndex);

	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

	VkSubmitInfo submit_info = vkinit::submit_info(&cmd);
	submit_info.pWaitDstStageMask = waitStages;
	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitSemaphores = &_frames[get_current_frame_index()]._presentSemaphore;
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores = &_frames[get_current_frame_index()]._renderSemaphore;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit_info, _frames[get_current_frame_index()]._renderFence));

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.pSwapchains = &re->_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &_frames[get_current_frame_index()]._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));


	_frameNumber++;
}

void Renderer::draw_forward(VkCommandBuffer cmd, RenderObject* first, int count)
{
	int frameIndex = _frameNumber % FRAME_OVERLAP;

	Mesh* lastMesh = nullptr;
	Material* lastMaterial = nullptr;
	for (int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];

		if (object._material != lastMaterial)
		{
			lastMaterial = object._material;

			uint32_t uniform_offset = vkutil::get_aligned_size(sizeof(GPUSceneData) * frameIndex, re->_gpuProperties.limits.minUniformBufferOffsetAlignment);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_forwardPipelineLayout, 0, 1, &_frames[get_current_frame_index()].globalDescriptor, 1, &uniform_offset);

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_forwardPipelineLayout, 1, 1, &_frames[get_current_frame_index()].objectDescriptor, 0, nullptr);

			if (object._material->albedoTexture != VK_NULL_HANDLE)
			{
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_texPipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_forwardPipelineLayout, 2, 1, &object._material->albedoTexture, 0, nullptr);
			}
			else
			{
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_forwardPipeline);
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

void Renderer::draw_deferred(VkCommandBuffer cmd, int imageIndex)
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

	VkRenderPassBeginInfo light_rpInfo = vkinit::renderpass_begin_info(re->_defaultRenderPass, re->_windowExtent, re->_framebuffers[imageIndex]);

	std::array<VkClearValue, 2> clearValues = { clearValue, depthClear };

	light_rpInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	light_rpInfo.pClearValues = clearValues.data();

	vkCmdBeginRenderPass(cmd, &light_rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_lightPipeline);

	int frameIndex = _frameNumber % FRAME_OVERLAP;
	uint32_t uniform_offset = vkutil::get_aligned_size(sizeof(GPUSceneData) * frameIndex, re->_gpuProperties.limits.minUniformBufferOffsetAlignment);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_lightPipelineLayout, 0, 1, &_frames[get_current_frame_index()].globalDescriptor, 1, &uniform_offset);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, re->_lightPipelineLayout, 1, 1, &re->_gbuffersDescriptorSet, 0, nullptr);

	//deferred quad
	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &deferred_quad._vertexBuffer._buffer, &offset);
	vkCmdBindIndexBuffer(cmd, deferred_quad._indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(cmd, static_cast<uint32_t>(deferred_quad._indices.size()), 1, 0, 0, 0);

	//ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRenderPass(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));
}

BlasInput Renderer::renderable_to_vulkan_geometry(RenderObject renderable)
{
	BlasInput input;

	// Setup identity transform matrix

	VkTransformMatrixKHR transformMatrix = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f
	};

	// Create buffers
	VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
	VmaMemoryUsage memoryUsageFlags = VMA_MEMORY_USAGE_CPU_TO_GPU;

	AllocatedBuffer transformBuffer = vkutil::create_buffer(_allocator, sizeof(VkTransformMatrixKHR), bufferUsageFlags, memoryUsageFlags);
	_transformBuffers.push_back(transformBuffer);

	void* transformData;
	vmaMapMemory(_allocator, transformBuffer._allocation, &transformData);
	memcpy(transformData, &transformMatrix, sizeof(VkTransformMatrixKHR));
	vmaUnmapMemory(_allocator, transformBuffer._allocation);

	VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
	VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
	VkDeviceOrHostAddressConstKHR transformBufferDeviceAddress{};

	vertexBufferDeviceAddress.deviceAddress = vkutil::get_buffer_device_address(_device, renderable._mesh->_vertexBuffer._buffer);
	indexBufferDeviceAddress.deviceAddress = vkutil::get_buffer_device_address(_device, renderable._mesh->_indexBuffer._buffer);
	transformBufferDeviceAddress.deviceAddress = vkutil::get_buffer_device_address(_device, transformBuffer._buffer);

	// Build
	VkAccelerationStructureGeometryKHR accelerationStructureGeometry{};
	accelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	accelerationStructureGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	accelerationStructureGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	accelerationStructureGeometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
	accelerationStructureGeometry.geometry.triangles.maxVertex = renderable._mesh->_vertices.size();
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

	const uint32_t numTriangles = renderable._mesh->_indices.size() / 3;
	VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
	accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	vkGetAccelerationStructureBuildSizesKHR(
		_device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&accelerationStructureBuildGeometryInfo,
		&numTriangles,
		&accelerationStructureBuildSizesInfo);

	VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
	accelerationStructureBuildRangeInfo.primitiveCount = numTriangles;
	accelerationStructureBuildRangeInfo.primitiveOffset = 0;
	accelerationStructureBuildRangeInfo.firstVertex = 0;
	accelerationStructureBuildRangeInfo.transformOffset = 0;

	input._accelerationStructureGeometry = accelerationStructureGeometry;
	input._accelerationStructureBuildGeometryInfo = accelerationStructureBuildGeometryInfo;
	input._accelerationStructureBuildSizesInfo = accelerationStructureBuildSizesInfo;
	input._accelerationStructureBuildRangeInfo = accelerationStructureBuildRangeInfo;

	return input;
}

void Renderer::build_blas(const std::vector<BlasInput>& input, VkBuildAccelerationStructureFlagsKHR flags)
{
	for (const auto& blasInput : input)
	{
		AccelerationStructure newAccelerationStructure{};

		create_acceleration_structure_buffer(newAccelerationStructure, blasInput._accelerationStructureBuildSizesInfo);

		VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
		accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		accelerationStructureCreateInfo.buffer = newAccelerationStructure._buffer;
		accelerationStructureCreateInfo.size = blasInput._accelerationStructureBuildSizesInfo.accelerationStructureSize;
		accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

		vkCreateAccelerationStructureKHR(_device, &accelerationStructureCreateInfo, nullptr, &newAccelerationStructure._handle);

		// Create a small scratch buffer used during build of the bottom level acceleration structure
		RayTracingScratchBuffer scratchBuffer = create_scratch_buffer(blasInput._accelerationStructureBuildSizesInfo.buildScratchSize);

		VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{};
		accelerationBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		accelerationBuildGeometryInfo.dstAccelerationStructure = newAccelerationStructure._handle;
		accelerationBuildGeometryInfo.geometryCount = 1;
		accelerationBuildGeometryInfo.pGeometries = &blasInput._accelerationStructureGeometry;
		accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer._deviceAddress;

		VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo = blasInput._accelerationStructureBuildRangeInfo;
		std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &buildRangeInfo };

		if (re->_accelerationStructureFeatures.accelerationStructureHostCommands)
		{
			// Implementation supports building acceleration structure building on host
			vkBuildAccelerationStructuresKHR(
				_device,
				VK_NULL_HANDLE,
				1,
				&accelerationBuildGeometryInfo,
				accelerationBuildStructureRangeInfos.data());
		}
		else
		{
			// Acceleration structure needs to be build on the device
			vkupload::immediate_submit([&](VkCommandBuffer cmd)
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
		accelerationDeviceAddressInfo.accelerationStructure = newAccelerationStructure._handle;
		newAccelerationStructure._deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(_device, &accelerationDeviceAddressInfo);

		_bottomLevelAS.push_back(newAccelerationStructure);
		delete_scratch_buffer(scratchBuffer);
	}
}

FrameData& Renderer::get_current_frame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}

/*
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
*/