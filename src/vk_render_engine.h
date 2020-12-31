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

struct MeshPushConstants {
	glm::vec4 data;
	glm::mat4 render_matrix;
};

struct UploadContext {
	VkFence _uploadFence;
	VkCommandPool _commandPool;
};

struct Image {
	VkImageView _view;
	VkImage _image;
	VmaAllocation _allocation;
	VkFormat _format;
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

	bool _isInitialized{ false };

	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	VkPhysicalDeviceProperties _gpuProperties;

	// Vulkan Components

	// Core 
	VkInstance _instance;
	VkDebugUtilsMessengerEXT _debug_messenger;
	VkPhysicalDevice _physicalDevice;
	VkDevice _device;
	VkSurfaceKHR _surface;

	// Swapchain
	VkSwapchainKHR _swapchain;
	VkFormat _swapchainImageFormat;

	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;

	// Queues
	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	// Samplers
	VkSampler _defaultSampler;

	//Deletion
	DeletionQueue _mainDeletionQueue;
	VmaAllocator _allocator;

	// Upload Context for immediate submit
	UploadContext _uploadContext;

	// Scene Descriptors
	// - Descriptor Pool
	VkDescriptorPool _descriptorPool;
	// - Descriptor Layouts
	VkDescriptorSetLayout _globalSetLayout;
	VkDescriptorSetLayout _objectSetLayout;
	VkDescriptorSetLayout _singleTextureSetLayout;
	VkDescriptorSetLayout _camSetLayout;

	// - Pipeline Layouts
	VkPipelineLayout _forwardPipelineLayout;
	VkPipelineLayout _texPipelineLayout;
	VkPipelineLayout _deferredPipelineLayout;
	VkPipelineLayout _lightPipelineLayout;

	// Pipelines
	VkPipeline _forwardPipeline;
	VkPipeline _texPipeline;
	VkPipeline _deferredPipeline;
	VkPipeline _lightPipeline;

	//Depth Buffer
	Image _depthImage;

	// Deferred
	// - Deferred attachments
	Image _positionImage;
	Image _normalImage;
	Image _albedoImage;

	// - Descriptors
	VkDescriptorPool _gbuffersPool;
	VkDescriptorSetLayout _gbuffersSetLayout;
	VkDescriptorSet _gbuffersDescriptorSet;

	Mesh deferred_quad;

	// Render passes
	VkRenderPass _defaultRenderPass;
	VkRenderPass _deferredRenderPass;

	// Framebuffers
	std::vector<VkFramebuffer> _framebuffers;
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

	void* deviceCreatepNextChain = nullptr;

	void init();

private:
	void init_vulkan();

	void init_swapchain();

	void init_command_pools();

	void init_sync_structures();

	void init_descriptor_set_pool();

	void init_descriptor_set_layouts();

	void init_depth_buffer();

	void init_deferred_attachments();

	void init_render_passes();

	void init_framebuffers();

	void init_pipelines();

	void init_gbuffer_descriptors();

	void get_enabled_features();
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