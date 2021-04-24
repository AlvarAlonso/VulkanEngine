#pragma once

//#define GLM_FORCE_RADIANS
//#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "vk_types.h"
#include "vk_mesh.h"

const int MAX_OBJECTS = 100;
const int MAX_MATERIALS = 100;
const int MAX_TEXTURES = 100;
const int GBUFFER_NUM = 5;
const float SHADOW_BIAS = 0.0f;
const float SHADOW_MAP_WIDTH = 1920.0f;
const float SHADOW_MAP_HEIGHT = 1080.0f;

struct RenderObject;
class Scene;

namespace VKE
{
	class Node;
};

enum RenderMode {
	RENDER_MODE_FORWARD = 0,
	RENDER_MODE_DEFERRED,
	RENDER_MODE_RAYTRACING
};

struct Pospo {
	VkPipeline _pipeline;
	VkPipelineLayout _pipelineLayout;
	VkRenderPass _renderPass;
	VkDescriptorSet _textureSet;
	VkDescriptorSet _additionalTextureSet;
	std::vector<VkFramebuffer> _framebuffers;
};

struct BlasInput {
	VkAccelerationStructureGeometryKHR _accelerationStructureGeometry;
	VkAccelerationStructureBuildGeometryInfoKHR _accelerationStructureBuildGeometryInfo;
	VkAccelerationStructureBuildSizesInfoKHR _accelerationStructureBuildSizesInfo;
	VkAccelerationStructureBuildRangeInfoKHR _accelerationStructureBuildRangeInfo;
};

struct UploadContext {
	VkFence _uploadFence;
	VkCommandPool _commandPool;
};

struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function)
	{
		deletors.push_back(function);
	}

	void flush()
	{
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
		{
			(*it)();
		}

		deletors.clear();
	}
};

class RenderEngine
{
public:

	RenderEngine();

	bool _isInitialized{ false };

	VkExtent2D _windowExtent{ 1920 , 1080 };
	static const uint32_t workgroup_width = 16;
	static const uint32_t workgroup_height = 8;

	struct SDL_Window* _window{ nullptr };

	VkPhysicalDeviceProperties _gpuProperties;

	// Vulkan Components

	// Core 
	VkInstance					_instance;
	VkDebugUtilsMessengerEXT	_debug_messenger;
	static VkPhysicalDevice		_physicalDevice;
	static VkDevice				_device;
	VkSurfaceKHR				_surface;

	// Swapchain
	VkSwapchainKHR				_swapchain;
	VkFormat					_swapchainImageFormat;

	std::vector<VkImage>		_swapchainImages;
	std::vector<VkImageView>	_swapchainImageViews;

	// Queues
	static VkQueue				_graphicsQueue;
	uint32_t					_graphicsQueueFamily;

	// Samplers
	static VkSampler	_defaultSampler;

	//Deletion
	static DeletionQueue _mainDeletionQueue;
	static VmaAllocator _allocator;

	// Upload Context for immediate submit
	static UploadContext _uploadContext;

	// Scene Descriptors
	// - Descriptor Pool
	static VkDescriptorPool			_descriptorPool;
	// - Descriptor Layouts
	VkDescriptorSetLayout			_globalSetLayout;
	VkDescriptorSetLayout			_objectSetLayout;
	static VkDescriptorSetLayout	_materialsSetLayout;
	VkDescriptorSetLayout			_singleTextureSetLayout;
	VkDescriptorSetLayout			_storageTextureSetLayout;
	VkDescriptorSetLayout			_camSetLayout;
	VkDescriptorSetLayout			_skyboxSetLayout;

	// - Pipeline Layouts
	VkPipelineLayout _texPipelineLayout;
	VkPipelineLayout _gbuffersPipelineLayout;
	VkPipelineLayout _lightPipelineLayout;
	VkPipelineLayout _skyboxPipelineLayout;
	VkPipelineLayout _dsmPipelineLayout;

	// Pipelines
	VkPipeline	_texPipeline;
	VkPipeline	_gbuffersPipeline;
	VkPipeline	_skyboxPipeline;
	VkPipeline	_dsmPipeline;

	//Depth Buffer
	Image _depthImage;

	// Skybox pass

	// G-Buffers
	// - G-Buffers attachments
	Image _positionImage;
	Image _normalImage;
	Image _albedoImage;
	Image _motionVectorImage;

	// - G-Buffers Descriptors
	VkDescriptorPool _gbuffersPool;
	VkDescriptorSetLayout _gbuffersSetLayout;
	VkDescriptorSet _gbuffersDescriptorSet;

	// Render passes
	VkRenderPass _defaultRenderPass;
	VkRenderPass _gbuffersRenderPass;
	VkRenderPass _singleAttachmentRenderPass;
	VkRenderPass _skyboxRenderPass;

	// Framebuffers
	std::vector<VkFramebuffer> _framebuffers;
	VkFramebuffer _dsm_framebuffer;
	VkFramebuffer _skybox_framebuffer;
	VkFramebuffer _offscreen_framebuffer;

	//FEATURES
	// - pnext features
	VkPhysicalDeviceDescriptorIndexingFeatures _enabledIndexingFeatures{};
	VkPhysicalDeviceBufferDeviceAddressFeatures _enabledBufferDeviceAddressFeatures{};
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR _enabledRayTracingPipelineFeatures{};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR _enabledAccelerationStructureFeatures{};

	// - Properties and features
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR  _rayTracingPipelineProperties{};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR _accelerationStructureFeatures{};
	VkPhysicalDeviceFeatures _enabledPhysicalDeviceFeatures{};

	void* deviceCreatepNextChain = nullptr;

	//Pointers to functions
	// - Raytracing function pointers
	PFN_vkGetBufferDeviceAddressKHR					vkGetBufferDeviceAddressKHR;
	PFN_vkCreateAccelerationStructureKHR			vkCreateAccelerationStructureKHR;
	PFN_vkDestroyAccelerationStructureKHR			vkDestroyAccelerationStructureKHR;
	PFN_vkGetAccelerationStructureBuildSizesKHR		vkGetAccelerationStructureBuildSizesKHR;
	PFN_vkGetAccelerationStructureDeviceAddressKHR	vkGetAccelerationStructureDeviceAddressKHR;
	PFN_vkCmdBuildAccelerationStructuresKHR			vkCmdBuildAccelerationStructuresKHR;
	PFN_vkBuildAccelerationStructuresKHR			vkBuildAccelerationStructuresKHR;
	PFN_vkCmdTraceRaysKHR							vkCmdTraceRaysKHR;
	PFN_vkGetRayTracingShaderGroupHandlesKHR		vkGetRayTracingShaderGroupHandlesKHR;
	PFN_vkCreateRayTracingPipelinesKHR				vkCreateRayTracingPipelinesKHR;

	//Raytracing attributes
	// - Acceleration Structures
	AccelerationStructure _topLevelAS{};
	std::vector<AccelerationStructure> _bottomLevelAS{};

	std::vector<AllocatedBuffer> _transformBuffers; //for bottom AS

	RtPipeline				_rtShadowsPipeline;
	RtPipeline				_rtFinalPipeline;

	VkPipeline				_denoiserPipeline;
	VkPipelineLayout		_denoiserPipelineLayout;
	VkDescriptorSetLayout	_denoiserSetLayout;
	VkDescriptorPool		_rayTracingDescriptorPool;

	Pospo pospo;

	struct UniformData {
		glm::mat4 viewInverse;
		glm::mat4 projInverse;
	} uniformData;

	//storage image
	VkImage _storageImage;
	VkDeviceMemory _storageImageMemory;
	VkImageView _storageImageView;

	//shadow images
	std::vector<Image> _shadowImages;
	std::vector<Image> _denoisedShadowImages;

	//deep shadow images
	Image _deepShadowImage;
	Image _directionalLightDepthBuffer;

	//init the render engine
	void init();

	//shuts down the engine
	void cleanup();

	//create pipeline and acceleration structures for the current scene
	void create_raytracing_scene_structures(const Scene& scene);

	void create_raster_scene_structures();

	void create_top_level_acceleration_structure(const Scene& scene, bool recreated);

	void reset_imgui();

private:

	//init vulkan core structures

	void init_vulkan();

	void init_swapchain();

	//init sync and descriptor layouts

	void init_command_pools();

	void init_sync_structures();

	void init_descriptor_set_pool();

	void init_descriptor_set_layouts();

	//init raster structures

	void init_raster_structures();

	void init_deferred_attachments();

	void init_render_passes();

	void init_framebuffers();

	void init_pipelines();

	void init_gbuffer_descriptors();

	//init ray tracing structures

	void init_raytracing_structures();

	void create_bottom_level_acceleration_structure(const Scene& scene);

	void create_storage_image();

	void create_deep_shadow_images(const int& lightsCount);

	void create_shadow_images(const int& lightsCount);

	void create_raytracing_pipelines(const Scene& scene);

	void create_pospo_structures();

	void create_shader_binding_table();

	void create_raytracing_descriptor_pool();

	RayTracingScratchBuffer create_scratch_buffer(VkDeviceSize size);

	void delete_scratch_buffer(RayTracingScratchBuffer& scratchBuffer);

	void create_acceleration_structure_buffer(AccelerationStructure& accelerationStructure, VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo);

	void build_blas(const std::vector<BlasInput>& input, VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

	//pnext features
	void get_enabled_features();

	//imgui
	void init_imgui(VkRenderPass renderPass);
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