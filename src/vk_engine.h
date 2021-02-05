// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include "vk_renderer.h"
#include "Camera.h"

const glm::vec3 camera_default_position = { 0.0f, 0.0f, 5.0f };

//constexpr unsigned int FRAME_OVERLAP = 2;

struct GPUCameraData {
	glm::mat4 view;
	glm::mat4 projection;
	glm::mat4 viewproj;
};

class VulkanEngine {
public:

	struct SDL_Window* _window{ nullptr };
	float dt;
	bool _isInitialized{ false };
	int _pipelineSelected{ 0 };

	static VulkanEngine* cinstance;

	//Scene components

	Scene* scene;
	
	Camera* camera;
	bool mouse_locked = true;

	//initializes everything in the engine
	void init();

	void cleanup();

	//run main loop
	void run();
	
	void load_images();

	void load_meshes();

private:

	void render_imgui();
};
