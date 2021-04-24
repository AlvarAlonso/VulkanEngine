#pragma once

#include "vk_render_engine.h"
#include "vk_scene.h"

namespace VKE
{
	struct MaterialToShader;
}

class Camera;
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

	// Shader flags
	FlagsPushConstant _shaderFlags;
	RtPushConstant	  _rtPushConstant;

	VKE::Mesh render_quad;

	AllocatedBuffer _camBuffer; //cam parameters
	AllocatedBuffer _objectBuffer;
	AllocatedBuffer _transformBuffer;
	AllocatedBuffer _sceneBuffer; //lights
	AllocatedBuffer _materialBuffer;
	AllocatedBuffer _primitiveInfoBuffer;
	AllocatedBuffer _texturesBuffer;
	AllocatedBuffer _textureIndicesBuffer;

	Camera* _lightCamera;
	AllocatedBuffer _lightCamBuffer;

	std::vector<VKE::MaterialToShader> _materialInfos;

	FrameData _frames[FRAME_OVERLAP];

	glm::mat4 _lastFrame_viewProj;

	//Commands
	VkCommandPool _forwardCommandPool;
	VkCommandPool _deferredCommandPool;

	VkCommandBuffer _dsmCommandBuffer;
	VkCommandBuffer _skyboxCommandBuffer;
	VkCommandBuffer _gbuffersCommandBuffer;
	VkCommandBuffer _rtShadowsCommandBuffer;
	VkCommandBuffer _rtFinalCommandBuffer;
	VkSemaphore		_dsmSemaphore;
	VkSemaphore		_skyboxSemaphore;
	VkSemaphore		_gbufferSemaphore;
	VkSemaphore		_rtShadowsSemaphore;
	VkSemaphore		_rtFinalSemaphore;

	VkDescriptorSet _skyboxDescriptorSet;
	VkDescriptorSet _camDescriptorSet;
	VkDescriptorSet _objectDescriptorSet;
	VkDescriptorSet _materialsDescriptorSet;
	VkDescriptorSet _lightCamDescriptorSet;
	VkDescriptorSet _deepShadowMapDescriptorSet;

	GPUSceneData	_sceneParameters;
	AllocatedBuffer _sceneParameterBuffer;

	//RAY TRACING PIPELINE

	VkDescriptorSet _rtFinalDescriptorSet;
	VkDescriptorSet _rtShadowsDescriptorSet;
	VkDescriptorSet	_denoiserDescriptorSet;
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

	void update_uniform_buffers();

	void create_raytracing_descriptor_sets();

	void record_deep_shadow_map_command_buffer(RenderObject* first, int count);

	void record_skybox_command_buffer();

	void record_gbuffers_command_buffers(RenderObject* first, int count);

	void record_rtShadows_command_buffer();

	void record_rtFinal_command_buffer();

	void record_pospo_command_buffer(VkCommandBuffer cmd, uint32_t swapchainImageIndex);

	//create commands and sync structures

	void init_commands();

	void init_sync_structures();

	void create_descriptor_buffers();

	void init_descriptors();

	//update descriptors

	void update_descriptors(RenderObject* first, size_t count);

	//draw functions

	void render_raytracing();

	FrameData& get_current_frame();

	void reset_frame();

	void update_frame();
};