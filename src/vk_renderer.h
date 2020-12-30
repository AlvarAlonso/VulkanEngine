#pragma once

#include "vk_utils.h"
#include "vk_scene.h"

struct RenderObject;

constexpr unsigned int FRAME_OVERLAP = 2;

enum RenderMode {
	RENDER_MODE_FORWARD = 0,
	RENDER_MODE_DEFERRED,
	RENDER_MODE_RAYTRACING
};

struct sFrameData {
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
	
	void init_renderer();

	void draw_scene();

	void create_pipelines();

	//support functions
	int get_current_frame_index();

	void init_raytracing();

	//Render passes
	VkRenderPass _defaultRenderPass;
	VkRenderPass _deferredRenderPass;

	//Depth Buffer
	VkImageView _depthImageView;
	AllocatedImage _depthImage;
	VkFormat _depthFormat;

	//deferred attachments
	VkImageView _positionImageView;
	AllocatedImage _positionImage;
	VkFormat _positionFormat;

	VkImageView _normalImageView;
	AllocatedImage _normalImage;
	VkFormat _normalFormat;

	VkImageView _albedoImageView;
	AllocatedImage _albedoImage;
	VkFormat _albedoFormat;

	VkDescriptorPool _gbuffersPool;
	VkDescriptorSetLayout _gbuffersSetLayout;
	VkDescriptorSet _gbuffersDescriptorSet;

	Mesh deferred_quad;

	//Framebuffers
	std::vector<VkFramebuffer> _framebuffers;
	VkFramebuffer _offscreen_framebuffer;

	//Commands
	VkCommandPool _forwardCommandPool;
	VkCommandPool _deferredCommandPool;

	VkCommandBuffer _deferredCommandBuffer;
	sFrameData _frames[FRAME_OVERLAP];
	VkSemaphore _offscreenSemaphore;

	//Pipelines
	VkPipeline _forwardPipeline;
	VkPipeline _texPipeline;
	VkPipeline _deferredPipeline;
	VkPipeline _lightPipeline;

	//RAY TRACING PIPELINE
		
	//raytracing function pointers
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

	void create_depth_buffer();

	void create_deferred_attachments();

	void init_commands();

	void init_framebuffers();

	void init_sync_structures();

	void init_default_render_pass();

	void init_deferred_render_pass();

	void init_gbuffers_descriptors();

	void record_deferred_command_buffers(RenderObject* first, int count);

	//draw functions
	void render_forward();

	void render_deferred();

	void render_raytracing();

	void draw_forward(VkCommandBuffer cmd, RenderObject* first, int count);

	void draw_deferred(VkCommandBuffer cmd, int imageIndex);		

	void create_forward_pipelines();

	void create_deferred_pipelines();

	BlasInput renderable_to_vulkan_geometry(RenderObject renderable);

	void build_blas(const std::vector<BlasInput>& input, VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
};

class PipelineBuilder {
public:
	std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
	VkPipelineVertexInputStateCreateInfo _vertexInputInfo;
	VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
	VkViewport _viewport;
	VkRect2D _scissor;
	VkPipelineDepthStencilStateCreateInfo _depthStencil;
	VkPipelineRasterizationStateCreateInfo _rasterizer;
	std::vector<VkPipelineColorBlendAttachmentState> _colorBlendAttachment;
	VkPipelineMultisampleStateCreateInfo _multisampling;
	VkPipelineLayout _pipelineLayout;

	VkPipeline build_pipeline(VkDevice device, VkRenderPass pass);
};