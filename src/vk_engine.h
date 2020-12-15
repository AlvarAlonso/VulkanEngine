// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include "vk_renderer.h"
#include "vk_mesh.h"
#include "Camera.h"

#include <glm/glm.hpp>
#include <unordered_map>

const glm::vec3 camera_default_position = { 0.0f, 50.0f, -10.0f };

constexpr unsigned int FRAME_OVERLAP = 2;

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
	VkSemaphore _presentSemaphore, _renderSemaphore;
	VkFence _renderFence;

	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	AllocatedBuffer cameraBuffer;
	VkDescriptorSet globalDescriptor;

	AllocatedBuffer objectBuffer;
	VkDescriptorSet objectDescriptor;
};

struct GPUObjectData {
	glm::mat4 modelMatrix;
};

struct Material {
	VkDescriptorSet textureSet;
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
};

struct RenderObject {
	Mesh* mesh;

	Material* material;

	glm::mat4 transformMatrix;
};

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

	//Render Passes
	VkRenderPass _renderPass;
	VkRenderPass _deferredPass;

	VkCommandPool _deferredCommandPool;
	VkCommandBuffer _deferredCommandBuffer;

	//Framebuffers
	std::vector<VkFramebuffer> _framebuffers;
	VkFramebuffer _offscreen_framebuffer;

	//Pipelines
	VkPipeline _meshPipeline;
	VkPipeline _normalsPipeline;
	VkPipeline _deferredPipeline;
	VkPipeline _lightPipeline;

	VkPipelineLayout _trianglePipelineLayout;
	VkPipelineLayout _meshPipelineLayout;
	VkPipelineLayout _normalPipelineLayout;
	VkPipelineLayout _deferredPipelineLayout;
	VkPipelineLayout _lightPipelineLayout;

	AllocatedBuffer _camBuffer;
	AllocatedBuffer _objectBuffer;

	//Deletion
	DeletionQueue _mainDeletionQueue;

	VmaAllocator _allocator;

	//Depth Buffer
	VkImageView _depthImageView;
	AllocatedImage _depthImage;
	VkFormat _depthFormat;

	//Deferred Attachments
	VkImageView _positionImageView;
	AllocatedImage _positionImage;
	VkFormat _positionFormat;

	VkImageView _normalImageView;
	AllocatedImage _normalImage;
	VkFormat _normalFormat;

	VkImageView _albedoImageView;
	AllocatedImage _albedoImage;
	VkFormat _albedoFormat;

	Mesh deferred_quad;

	// Descriptors
	VkDescriptorPool _descriptorPool;
	VkDescriptorSetLayout _globalSetLayout;
	VkDescriptorSetLayout _objectSetLayout;
	VkDescriptorSetLayout _singleTextureSetLayout;

	VkDescriptorSetLayout _gbuffersSetLayout;
	VkDescriptorSetLayout _camSetLayout;

	VkDescriptorSet _deferred_descriptor_set;
	VkDescriptorSet _camDescriptorSet;
	VkDescriptorSet _objectDescriptorSet;

	VkSemaphore _offscreenSemaphore;

	VkSampler _defaultSampler;

	UploadContext _uploadContext;

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();

	FrameData& get_current_frame();
	
	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	void load_images();

	void update_descriptors(RenderObject* first, int count);
	void update_descriptors_forward(RenderObject* first, int count);
	size_t pad_uniform_buffer_size(size_t originalSize);

private:

	// Init Vulkan Components

	void init_vulkan();

	void init_swapchain(); //initializes depth image

	void init_imgui();

	void init_deferred_attachments(); //initializes gbuffer attachments except depth

	void init_commands();

	void init_default_renderpass();

	void init_offscreen_framebuffer();

	void init_deferred_renderpass();

	void init_framebuffers();

	void init_sync_structures();

	void init_descriptors();

	void init_pipelines();

	void init_deferred_pipelines();

	void record_deferred_command_buffer(RenderObject* first, int count);

	void init_scene();

	bool load_shader_module(const char* filePath, VkShaderModule* outShaderModule);

	void load_meshes();

	// Assets

	void upload_mesh(Mesh& mesh);

	Material* create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);
	Material* get_material(const std::string& name);

	Mesh* get_mesh(const std::string& name);

	void draw_objects(VkCommandBuffer cmd, RenderObject* first, int count);

	void draw_objects_deferred(VkCommandBuffer cmd, int imageIndex);

	// Other


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
