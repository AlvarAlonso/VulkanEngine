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

	std::vector<RenderObject> _renderables;

	std::unordered_map<std::string, Material> _materials;
	std::unordered_map<std::string, Mesh> _meshes;

	//Assets
	std::unordered_map<std::string, Texture> _loadedTextures;

	//initializes everything in the engine
	void init();

	void cleanup();

	//run main loop
	void run();
	
	void load_images();

	Material* create_material(const std::string& name);
	Material* get_material(const std::string& name);

	Mesh* get_mesh(const std::string& name);

	void load_meshes();

private:

	void init_imgui();
};
