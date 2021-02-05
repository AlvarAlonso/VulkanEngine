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

	scene = new Scene();
	scene->generate_sample_scene();

	renderer->currentScene = scene;

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

void VulkanEngine::load_meshes()
{
	Mesh tree("../assets/MapleTree.obj");
	Mesh tree_leaves("../assets/MapleTreeLeaves.obj");
	Mesh tree_stem("../assets/MapleTreeStem.obj");
}

void VulkanEngine::load_images()
{
	VKE::Texture* defaultTexture = new VKE::Texture();

	vkutil::load_image_from_file(*this, "../assets/plain.jpg", defaultTexture->_image);

	VkImageViewCreateInfo imageinfo = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, defaultTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &imageinfo, nullptr, &defaultTexture->_imageView);

	defaultTexture->_id = VKE::Texture::sTexturesLoaded.size();
	defaultTexture->register_texture("default");


	VKE::Texture* grassTexture = new VKE::Texture();

	vkutil::load_image_from_file(*this, "../assets/grass.jpg", grassTexture->_image);

	VkImageViewCreateInfo imageinfo2 = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, grassTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &imageinfo2, nullptr, &grassTexture->_imageView);

	grassTexture->_id = VKE::Texture::sTexturesLoaded.size();
	grassTexture->register_texture("grass");

	RenderEngine::_mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(RenderEngine::_device, defaultTexture->_imageView, nullptr);
		vkDestroyImageView(RenderEngine::_device, grassTexture->_imageView, nullptr);
		});
}
