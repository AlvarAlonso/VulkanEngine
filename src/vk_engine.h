// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include "vk_render_engine.h"
#include "vk_renderer.h"
#include "Camera.h"

const glm::vec3 camera_default_position = { 0.0f, 0.0f, 2.5f };

//constexpr unsigned int FRAME_OVERLAP = 2;

const int MAX_OBJECTS = 10000;

struct Texture {
	AllocatedImage image;
	VkImageView imageView;
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

	Scene* scene;
	
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
	VkPipelineLayout _texPipelineLayout;
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
	VkPhysicalDeviceDescriptorIndexingFeatures _enabledIndexingFeatures{};
	VkPhysicalDeviceBufferDeviceAddressFeatures _enabledBufferDeviceAddressFeatures{};
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR _enabledRayTracingPipelineFeatures{};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR _enabledAccelerationStructureFeatures{};

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
	void load_images();

	void update_descriptors(RenderObject* first, int count);
	void update_descriptors_forward(RenderObject* first, int count);

	Material* create_material(const std::string& name);
	Material* get_material(const std::string& name);

	Mesh* get_mesh(const std::string& name);

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

	void load_meshes();

	// Assets

	void get_enabled_features();
};
