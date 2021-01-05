#pragma once

#include "vk_scene.h"

struct RenderObject;
struct SDL_Window;
struct RenderEngine;

constexpr unsigned int FRAME_OVERLAP = 2;

enum RenderMode {
	RENDER_MODE_FORWARD = 0,
	RENDER_MODE_DEFERRED,
	RENDER_MODE_RAYTRACING
};

struct GPUObjectData {
	glm::mat4 modelMatrix;
};

struct FrameData {
	VkSemaphore _presentSemaphore, _renderSemaphore;
	VkFence _renderFence;

	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	AllocatedBuffer cameraBuffer;
	VkDescriptorSet globalDescriptor;

	AllocatedBuffer objectBuffer;
	VkDescriptorSet objectDescriptor;
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

struct BlasInput {
	VkAccelerationStructureGeometryKHR _accelerationStructureGeometry;
	VkAccelerationStructureBuildGeometryInfoKHR _accelerationStructureBuildGeometryInfo;
	VkAccelerationStructureBuildSizesInfoKHR _accelerationStructureBuildSizesInfo;
	VkAccelerationStructureBuildRangeInfoKHR _accelerationStructureBuildRangeInfo;
};

class Renderer
{
public:

	RenderMode _renderMode;

	Renderer();
	
	void cleanup();

	SDL_Window* get_sdl_window();

	void draw_scene();

	Mesh deferred_quad;

	AllocatedBuffer _camBuffer;
	AllocatedBuffer _objectBuffer;

	FrameData _frames[FRAME_OVERLAP];

	//Commands
	VkCommandPool _forwardCommandPool;
	VkCommandPool _deferredCommandPool;

	VkCommandBuffer _deferredCommandBuffer;
	VkSemaphore _offscreenSemaphore;

	VkDescriptorSet _camDescriptorSet;
	VkDescriptorSet _objectDescriptorSet;

	GPUSceneData _sceneParameters;
	AllocatedBuffer _sceneParameterBuffer;

	//RAY TRACING PIPELINE
		
	//raytracing function pointers
	PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR;
	PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
	PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
	PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
	PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
	PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
	PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR;
	PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;
	PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR;
	PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR;

	std::vector<AccelerationStructure> _bottomLevelAS{};
	AccelerationStructure _topLevelAS{};

	AllocatedBuffer _vertexBuffer;
	AllocatedBuffer _indexBuffer;
	std::vector<uint32_t> _indexCount;
	std::vector<AllocatedBuffer> _transformBuffers;
	std::vector<AllocatedBuffer> _modelBuffers;
	std::vector<VkRayTracingShaderGroupCreateInfoKHR> _shaderGroups{};
	std::vector<AllocatedBuffer> _rtVertexBuffers;
	AllocatedBuffer _raygenShaderBindingTable;
	AllocatedBuffer _missShaderBindingTable;
	AllocatedBuffer _hitShaderBindingTable;

	VkPipeline _rayTracingPipeline;
	VkPipelineLayout _rayTracingPipelineLayout;
	VkDescriptorPool _rayTracingDescriptorPool;
	VkDescriptorSet _rayTracingDescriptorSet;
	VkDescriptorSetLayout _rayTracingSetLayout;
	AllocatedBuffer _ubo;

	struct UniformData {
		glm::mat4 viewInverse;
		glm::mat4 projInverse;
	} uniformData;

	//storage image
	VkImage _storageImage;
	VkDeviceMemory _storageImageMemory;
	VkImageView _storageImageView;

private:

	RenderEngine* re;
	VkPhysicalDevice _physicalDevice;
	VkDevice _device;
	VmaAllocator _allocator;

	int _frameNumber{ 0 };

	bool isDeferredCommandInit = false;

	//Queues
	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	//INIT RENDER STRUCTURES AND PIPELINES

	//Init ray tracing structures

	void init_renderer();

	//support functions
	int get_current_frame_index();

	void init_raytracing();

	// Init functions

	void create_bottom_level_acceleration_structure();

	void create_top_level_acceleration_structure();

	void create_storage_image();

	void create_uniform_buffer();

	void update_uniform_buffers();

	void create_raytracing_pipeline();

	void create_shader_binding_table();

	void create_raytracing_descriptor_sets();

	void record_raytracing_command_buffer(VkCommandBuffer cmd, uint32_t swapchainImageIndex);

	RayTracingScratchBuffer create_scratch_buffer(VkDeviceSize size);

	void delete_scratch_buffer(RayTracingScratchBuffer& scratchBuffer);

	void create_acceleration_structure_buffer(AccelerationStructure& accelerationStructure, VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo);

	//init raster structures

	void init_commands();

	void init_sync_structures();

	void create_descriptor_buffers();

	void record_deferred_command_buffers(RenderObject* first, int count);

	void init_descriptors();

	//update descriptors
	void update_descriptors_forward(RenderObject* first, size_t count);

	void update_descriptors(RenderObject* first, size_t count);

	//draw functions
	void render_forward();

	void render_deferred();

	void render_raytracing();

	void draw_forward(VkCommandBuffer cmd, RenderObject* first, int count);

	void draw_deferred(VkCommandBuffer cmd, int imageIndex);		

	FrameData& get_current_frame();

	BlasInput renderable_to_vulkan_geometry(RenderObject renderable);

	void build_blas(const std::vector<BlasInput>& input, VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
};