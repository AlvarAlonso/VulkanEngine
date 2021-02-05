#pragma once

#include "vk_render_engine.h"
#include "vk_scene.h"

namespace VKE
{
	struct MaterialToShader;
}

struct RenderObject;
struct SDL_Window;
struct RenderEngine;

constexpr unsigned int FRAME_OVERLAP = 2;

RenderMode operator++(RenderMode& m, int);

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

class Renderer
{
public:

	RenderMode _renderMode;

	Renderer();

	Scene* currentScene;
	
	void cleanup();

	SDL_Window* get_sdl_window();

	void switch_render_mode();

	void draw_scene();

	Mesh render_quad;

	AllocatedBuffer _camBuffer; //cam parameters
	AllocatedBuffer _objectBuffer; //models
	AllocatedBuffer _sceneBuffer; //lights
	AllocatedBuffer _materialBuffer;
	AllocatedBuffer _materialIndicesBuffer;
	AllocatedBuffer _texturesBuffer;
	AllocatedBuffer _textureIndicesBuffer;

	std::vector<VKE::MaterialToShader> _materialInfos;

	FrameData _frames[FRAME_OVERLAP];

	//Commands
	VkCommandPool _forwardCommandPool;
	VkCommandPool _deferredCommandPool;

	VkCommandBuffer _deferredCommandBuffer;
	VkCommandBuffer _raytracingCommandBuffer;
	VkSemaphore _offscreenSemaphore;

	VkDescriptorSet _camDescriptorSet;
	VkDescriptorSet _objectDescriptorSet;
	VkDescriptorSet _materialsDescriptorSet;

	GPUSceneData _sceneParameters;
	AllocatedBuffer _sceneParameterBuffer;

	//RAY TRACING PIPELINE

	VkDescriptorSet _rayTracingDescriptorSet;
	AllocatedBuffer _ubo;

	struct UniformData {
		glm::mat4 viewInverse;
		glm::mat4 projInverse;
		glm::vec4 position;
	} uniformData;

private:

	RenderEngine* re;
	VkPhysicalDevice _physicalDevice;
	VkDevice _device;
	VmaAllocator _allocator;

	int _frameNumber{ 0 };

	bool isDeferredCommandInit = false;
	bool areAccelerationStructuresInit = false;

	//Queues
	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	//INIT RENDER STRUCTURES AND PIPELINES

	//Init ray tracing structures

	void init_renderer();

	//support functions
	int get_current_frame_index();

	// Init functions

	void create_uniform_buffer();

	void update_uniform_buffers(RenderObject* first, size_t count);

	void create_raytracing_descriptor_sets();

	void record_raytracing_command_buffer();

	void record_pospo_command_buffer(VkCommandBuffer cmd, uint32_t swapchainImageIndex);

	//create commands and sync structures

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
};