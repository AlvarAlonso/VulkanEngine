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

	renderer = new Renderer();
	
	_window = renderer->get_sdl_window();

	load_images();

	load_meshes();

	/*
	scene = new Scene();
	scene->generate_sample_scene();
	_renderables = scene->_renderables;
	*/
	//init_imgui();

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
					_pipelineSelected += 1;
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

		/*
		//imgui new frame
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL2_NewFrame(_window);

		ImGui::NewFrame();

		//imgui commands
		ImGui::ShowDemoWindow();
		*/
		//draw();
		renderer->draw_scene();
	}
}

/*
void VulkanEngine::init_imgui()
{
	//Create Descriptor Pool for IMGUI
	VkDescriptorPoolSize pool_sizes[] =
	{
		{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
		{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
		{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
		{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
		{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;
	
	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &imguiPool));

	//Initialize imgui library

	//core imgui structures
	ImGui::CreateContext();

	//initializes imgui for SDL
	ImGui_ImplSDL2_InitForVulkan(_window);

	//initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = _instance;
	init_info.PhysicalDevice = _physicalDevice;
	init_info.Device = _device;
	init_info.Queue = _graphicsQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;

	ImGui_ImplVulkan_Init(&init_info, renderer->_defaultRenderPass);

	//execute a gpu command to upload imgui font textures
	immediate_submit([&](VkCommandBuffer cmd) {
		ImGui_ImplVulkan_CreateFontsTexture(cmd);
	});

	//clear font textures from cpu data
	ImGui_ImplVulkan_DestroyFontUploadObjects();

	//add to destroy the imgui created structures
	_mainDeletionQueue.push_function([=]() {
		vkDestroyDescriptorPool(_device, imguiPool, nullptr);
		ImGui_ImplVulkan_Shutdown();
		});
}
*/

void VulkanEngine::load_meshes()
{
	Mesh monkeyMesh("../assets/monkey_smooth.obj");

	Mesh lostEmpire("../assets/lost_empire.obj");

	_meshes["monkey"] = monkeyMesh;
	_meshes["empire"] = lostEmpire;
}

Material* VulkanEngine::create_material(const std::string& name)
{
	Material mat;
	mat.albedoTexture = VK_NULL_HANDLE;
	_materials[name] = mat;
	return &_materials[name];
}

Material* VulkanEngine::get_material(const std::string& name)
{
	auto it = _materials.find(name);
	if(it == _materials.end())
	{
		return nullptr;
	}
	else
	{
		return &(*it).second;
	}
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

	Texture lostEmpire;

	vkutil::load_image_from_file(*this, "../assets/lost_empire-RGBA.png", lostEmpire.image);
	
	VkImageViewCreateInfo imageinfo = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, lostEmpire.image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &imageinfo, nullptr, &lostEmpire.imageView);

	_loadedTextures["empire_diffuse"] = lostEmpire;


	Texture defaultTexture;

	vkutil::load_image_from_file(*this, "../assets/plain.jpg", defaultTexture.image);

	VkImageViewCreateInfo imageinfo2 = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, defaultTexture.image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &imageinfo2, nullptr, &defaultTexture.imageView);

	_loadedTextures["default"] = defaultTexture;
}
