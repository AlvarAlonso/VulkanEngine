// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include "vk_renderer.h"
#include "vk_mesh.h"
#include "Camera.h"
#include "vk_entity.h"

//#define GLM_FORCE_RADIANS
//#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <unordered_map>

const glm::vec3 camera_default_position = { 0.0f, 0.0f, 2.5f };

//constexpr unsigned int FRAME_OVERLAP = 2;

const int MAX_OBJECTS = 10000;

struct Texture {
	AllocatedImage image;
	VkImageView imageView;
};

struct UploadContext {
	VkFence _uploadFence;
	VkCommandPool _commandPool;
};

struct GPUSceneData {
	glm::vec4 fogColor;
	glm::vec4 fogDistances;
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection;
	glm::vec4 sunlightColor;
};

struct GPUCameraData {
	glm::mat4 view;
	glm::mat4 projection;
	glm::mat4 viewproj;
};

struct FrameData {
	AllocatedBuffer cameraBuffer;
	VkDescriptorSet globalDescriptor;

	AllocatedBuffer objectBuffer;
	VkDescriptorSet objectDescriptor;
};

struct GPUObjectData {
	glm::mat4 modelMatrix;
};

/*
struct RenderObject {
	Mesh* mesh;

	Material* material;

	glm::mat4 transformMatrix;
};*/

struct MeshPushConstants {
	glm::vec4 data;
	glm::mat4 render_matrix;
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
		for(auto it = deletors.rbegin(); it != deletors.rend(); it++)
		{
			(*it)();
		}

		deletors.clear();
	}
};

class VulkanEngine {
public:

	float dt;
	bool _isInitialized{ false };
	int _frameNumber {0};
	int _pipelineSelected{ 0 };

	static VulkanEngine* cinstance;

	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	VkPhysicalDeviceProperties _gpuProperties;

	//Scene components
	
	Camera* camera;
	bool mouse_locked = true;

	std::vector<RenderObject> _renderables;

	std::unordered_map<std::string, Material> _materials;
	std::unordered_map<std::string, Mesh> _meshes;

	GPUSceneData _sceneParameters;
	AllocatedBuffer _sceneParameterBuffer;

	//Assets
	std::unordered_map<std::string, Texture> _loadedTextures;

	// Vulkan Components

	VkInstance _instance;
	VkDebugUtilsMessengerEXT _debug_messenger;
	VkPhysicalDevice _physicalDevice;
	VkDevice _device;
	VkSurfaceKHR _surface;

	VkSwapchainKHR _swapchain;
	VkFormat _swapchainImageFormat;

	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	FrameData _frames[FRAME_OVERLAP];

	//Pipelines
	VkPipelineLayout _forwardPipelineLayout;
	VkPipelineLayout _deferredPipelineLayout;
	VkPipelineLayout _lightPipelineLayout;

	AllocatedBuffer _camBuffer;
	AllocatedBuffer _objectBuffer;

	//Deletion
	DeletionQueue _mainDeletionQueue;

	VmaAllocator _allocator;

	// Descriptors
	VkDescriptorPool _descriptorPool;
	VkDescriptorSetLayout _globalSetLayout;
	VkDescriptorSetLayout _objectSetLayout;
	VkDescriptorSetLayout _singleTextureSetLayout;

	VkDescriptorSetLayout _camSetLayout;

	VkDescriptorSet _camDescriptorSet;
	VkDescriptorSet _objectDescriptorSet;

	UploadContext _uploadContext;

	VkSampler _defaultSampler;

	//RAY TRACING

	PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR;

	//pnext features
	VkPhysicalDeviceDescriptorIndexingFeatures			_enabledIndexingFeatures{};
	VkPhysicalDeviceBufferDeviceAddressFeatures         _enabledBufferDeviceAddressFeatures{};
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR       _enabledRayTracingPipelineFeatures{};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR    _enabledAccelerationStructureFeatures{};

	//Properties and features
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR  _rayTracingPipelineProperties{};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR _accelerationStructureFeatures{};

	void* deviceCreatepNextChain = nullptr;

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//run main loop
	void run();

	FrameData& get_current_frame();
	
	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags flags = 0);
	void load_images();

	void update_descriptors(RenderObject* first, int count);
	void update_descriptors_forward(RenderObject* first, int count);
	size_t get_aligned_size(size_t originalSize, uint32_t alignment);
	bool load_shader_module(const char* filePath, VkShaderModule* outShaderModule);
	Material* create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);
	Material* get_material(const std::string& name);

	Mesh* get_mesh(const std::string& name);

	uint32_t find_memory_type_index(uint32_t allowedTypes, VkMemoryPropertyFlags properties);
	uint64_t get_buffer_device_address(VkBuffer buffer);

private:

	// Init Vulkan Components

	void init_vulkan();

	void init_raytracing();

	void init_swapchain(); //initializes depth image

	void init_imgui();

	void init_commands();

	void init_sync_structures();

	void init_descriptor_set_pool();

	void init_descriptor_set_layouts();

	void init_descriptors();

	void init_scene();

	void load_meshes();

	// Assets

	void upload_mesh(Mesh& mesh);

	void get_enabled_features();
};
