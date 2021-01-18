#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "vk_engine.h"
#include "vk_textures.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>

#include <glm/gtx/transform.hpp>

#include "vk_initializers.h"

#include "VkBootstrap.h"

#include <array>

#include <chrono>

VulkanEngine* VulkanEngine::cinstance = nullptr;
Renderer* renderer = nullptr;

void VulkanEngine::init()
{
	cinstance = this;

	camera = new Camera(camera_default_position);
	camera->_speed = 0.1f;

	renderer = new Renderer();
	
	_window = renderer->get_sdl_window();

	load_images();

	load_meshes();

	create_materials();

	scene = new Scene();
	scene->generate_sample_scene();

	renderer->currentScene = scene;

	//_renderables = scene->_renderables;

	//everything went fine
	_isInitialized = true;
}

void VulkanEngine::cleanup()
{	
	renderer->cleanup();
}

void VulkanEngine::run()
{
	SDL_Event e;
	bool bQuit = false;
	double lastFrame = 0.0f;
	int xMouseOld, yMouseOld;
	SDL_GetMouseState(&xMouseOld, &yMouseOld);

	//main loop
	while (!bQuit)
	{
		double currentTime = SDL_GetTicks();
		float dt = float(currentTime - lastFrame);
		lastFrame = currentTime;

		//Handle events on queue
		while (SDL_PollEvent(&e) != 0)
		{
			//ImGui_ImplSDL2_ProcessEvent(&e);

			int x, y;
			SDL_GetMouseState(&x, &y);

			//close the window when user alt-f4s or clicks the X button			
			if (e.type == SDL_QUIT)
			{
				bQuit = true;
			}
			else if(e.type == SDL_KEYDOWN)
			{
				if (e.key.keysym.sym == SDLK_SPACE)
				{
					_pipelineSelected++;
					if (_pipelineSelected > 2)
					{
						_pipelineSelected = 0;
					}

					if(_pipelineSelected == 0)
					{
						std::cout << "Using Forward Rendering" << std::endl;
					}
					else if(_pipelineSelected == 1)
					{
						std::cout << "Using Deferred Rendering" << std::endl;
					}
					else
					{
						std::cout << "Using Raytracing Rendering" << std::endl;
					}
				}

				if(e.key.keysym.sym == SDLK_ESCAPE)
				{
					mouse_locked = !mouse_locked;
				}

				if (e.key.keysym.sym == SDLK_w)
				{
					camera->processKeyboard(FORWARD, dt);
				}
				if (e.key.keysym.sym == SDLK_a) 
				{
					camera->processKeyboard(LEFT, dt);
				}
				if (e.key.keysym.sym == SDLK_s) 
				{
					camera->processKeyboard(BACKWARD, dt);
				}
				if (e.key.keysym.sym == SDLK_d) 
				{
					camera->processKeyboard(RIGHT, dt);
				}
				if (e.key.keysym.sym == SDLK_UP) 
				{
					camera->processKeyboard(UP, dt);
				}
				if (e.key.keysym.sym == SDLK_DOWN) 
				{
					camera->processKeyboard(DOWN, dt);
				}
				if (e.key.keysym.sym == SDLK_q)
				{
					camera->rotate(-5 * dt, 0);
				}
				if (e.key.keysym.sym == SDLK_e)
				{
					camera->rotate(5 * dt, 0);
				}
			}
		}

		if(mouse_locked)
		{
			int xMouse, yMouse;
			SDL_GetMouseState(&xMouse, &yMouse);

			int xMouseDiff = xMouse - xMouseOld;
			int yMouseDiff = yMouse - yMouseOld;

			camera->rotate(xMouseDiff, -yMouseDiff);

			int window_width, window_height;
			SDL_GetWindowSize(_window, &window_width, &window_height);

			int center_x = (int)floor(window_width * 0.5f);
			int center_y = (int)floor(window_height * 0.5f);

			SDL_WarpMouseInWindow(_window, center_x, center_y); //put the mouse back in the middle of the screen
			xMouseOld = center_x;
			yMouseOld = center_y;
		}
		
		//imgui new frame
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL2_NewFrame(_window);

		ImGui::NewFrame();

		//imgui commands
		//ImGui::ShowDemoWindow();
		render_imgui();
		
		//draw();
		renderer->draw_scene();
	}
}

void VulkanEngine::render_imgui()
{
	ImGui::Text("MAIN IMGUI DEBUG WINDOW:");
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	static bool check = true;
	ImGui::Checkbox("checkbox", &check);

	glm::mat4 model = glm::transpose(scene->_renderables[0]._model);

	static float &x = model[0].w;
	ImGui::InputFloat("input float", &x, 1.0f, 1.0f, "%.3f");

	static float &y = model[1].w;
	ImGui::InputFloat("input float", &y, 1.0f, 1.0f, "%.3f");

	static float &z = model[2].w;
	ImGui::InputFloat("input float", &z, 1.0f, 1.0f, "%.3f");

	scene->_renderables[0]._model = glm::transpose(model);
	
	/*
	static float x = scene->_lights[0]._position.x;
	ImGui::InputFloat("input float", &x, 1.0f, 1.0f, "%.3f");
	scene->_lights[0]._position.x = x;

	static float y = scene->_lights[0]._position.y;
	ImGui::InputFloat("input float", &y, 1.0f, 1.0f, "%.3f");
	scene->_lights[0]._position.y = y;

	static float z = scene->_lights[0]._position.z;
	ImGui::InputFloat("input float", &z, 1.0f, 1.0f, "%.3f");
	scene->_lights[0]._position.z = z;
	*/
}

void VulkanEngine::create_materials()
{	
	//specular
	create_material({ 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
	
	//refractive
	create_material({ 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.1f, 2.0f });

	//diffuse
	create_material({ 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 0.0f }); //white

	create_material({ 0.5f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 0.0f }); //red

	create_material({ 0.5f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.5f, 0.0f, 0.0f }); //metal red

	create_material({ 0.5f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.5f, 0.0f, 0.0f }); //blue

	create_material({ 0.5f, 0.0f, 0.0f, 1.0f }, { 0.7f, 0.8f, 0.0f, 0.0f }); //metal blue
}

void VulkanEngine::load_meshes()
{
	//Mesh monkeyMesh("../assets/monkey_smooth.obj");

	//Mesh lostEmpire("../assets/lost_empire.obj");

	//Mesh sphere("../assets/sphere.obj");

	//_meshes["monkey"] = monkeyMesh;
	//_meshes["empire"] = lostEmpire;
	//_meshes["sphere"] = sphere;

	Mesh tree("../assets/MapleTree.obj");
	Mesh tree_leaves("../assets/MapleTreeLeaves.obj");
	Mesh tree_stem("../assets/MapleTreeStem.obj");

	_meshes["tree"] = tree;
	_meshes["tree_leaves"] = tree_leaves;
	_meshes["tree_stem"] = tree_stem;
}

Material* VulkanEngine::create_material(const glm::vec4& color, const glm::vec4& properties)
{
	Material mat;
	mat.color = color;
	mat.properties = properties;
	_materials.push_back(mat);
	return &_materials[_materials.size() - 1];
}

Mesh* VulkanEngine::get_mesh(const std::string& name)
{
	auto it = _meshes.find(name);
	if(it == _meshes.end())
	{
		return nullptr;
	}
	else
	{
		return &(*it).second;
	}
}

void VulkanEngine::load_images()
{
	Texture defaultTexture;

	vkutil::load_image_from_file(*this, "../assets/plain.jpg", defaultTexture.image);

	VkImageViewCreateInfo imageinfo = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, defaultTexture.image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &imageinfo, nullptr, &defaultTexture.imageView);

	_loadedTextures.push_back(defaultTexture);

	Texture grassTexture;

	vkutil::load_image_from_file(*this, "../assets/grass.jpg", grassTexture.image);

	VkImageViewCreateInfo imageinfo2 = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, grassTexture.image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &imageinfo2, nullptr, &grassTexture.imageView);

	_loadedTextures.push_back(grassTexture);

	/*
	Texture lostEmpire;

	vkutil::load_image_from_file(*this, "../assets/lost_empire-RGBA.png", lostEmpire.image);

	VkImageViewCreateInfo imageinfo = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, lostEmpire.image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &imageinfo, nullptr, &lostEmpire.imageView);

	_loadedTextures.push_back(lostEmpire);
	*/
}
