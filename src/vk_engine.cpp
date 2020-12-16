#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include <iostream>
#include "vk_types.h"

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

#include <fstream>
#include <array>

#include <chrono>

VulkanEngine* VulkanEngine::cinstance = nullptr;
GRAPHICS::Renderer* renderer = nullptr;

void VulkanEngine::init()
{
	cinstance = this;

	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);
	
	_window = SDL_CreateWindow(
		"Vulkan Engine",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width,
		_windowExtent.height,
		window_flags
	);
	
	init_vulkan();

	init_swapchain();

	init_commands();

	init_sync_structures();

	init_descriptor_layouts();

	renderer = new GRAPHICS::Renderer();
	renderer->init_renderer();

	init_descriptors();

	init_pipelines();

	init_deferred_pipelines();

	load_images();

	load_meshes();

	init_scene();

	init_imgui();

	//everything went fine
	_isInitialized = true;
}
void VulkanEngine::cleanup()
{	
	if (_isInitialized) {

		vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, UINT64_MAX);

		_mainDeletionQueue.flush();
		
		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		
		vkDestroyDevice(_device, nullptr);
		vkDestroyInstance(_instance, nullptr);
		
		SDL_DestroyWindow(_window);
	}
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
			ImGui_ImplSDL2_ProcessEvent(&e);

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
					if (_pipelineSelected > 1)
					{
						_pipelineSelected = 0;
					}

					if(_pipelineSelected == 0)
					{
						std::cout << "Using Forward Rendering" << std::endl;
					}
					else
					{
						std::cout << "Using Deferred Rendering" << std::endl;
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
		ImGui::ShowDemoWindow();

		//draw();
		renderer->draw_scene();
	}
}

FrameData& VulkanEngine::get_current_frame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}

void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_uploadContext._commandPool, 1);
	
	VkCommandBuffer cmd;
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &cmd));

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = vkinit::submit_info(&cmd);


	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, _uploadContext._uploadFence));
	
	vkWaitForFences(_device, 1, &_uploadContext._uploadFence, true, UINT64_MAX);
	vkResetFences(_device, 1, &_uploadContext._uploadFence);

	vkResetCommandPool(_device, _uploadContext._commandPool, 0);
}

void VulkanEngine::init_vulkan()
{
	vkb::InstanceBuilder builder;

	//make the Vulkan instance, with basic debug features
	auto inst_ret = builder.set_app_name("Vulkan Engine")
		.request_validation_layers(true)
		.require_api_version(1, 1, 0)
		.use_default_debug_messenger()
		.build();

	vkb::Instance vkb_inst = inst_ret.value();

	//store the instance
	_instance = vkb_inst.instance;
	//store the debug messenger
	_debug_messenger = vkb_inst.debug_messenger;


	// get the surface of the window we opened with SDL
	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

	//use vkbootstrap to select a gpu
	//We want a gpu that can write to the SDL surface and supports Vulkan 1.1
	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 1)
		.set_surface(_surface)
		.select()
		.value();

	//create the final Vulkan device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };

	vkb::Device vkbDevice = deviceBuilder.build().value();

	// Get the VkDevice handle used in the rest of a Vulkan application
	_device = vkbDevice.device;
	_physicalDevice = physicalDevice.physical_device;

	//get graphics Queue
	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _physicalDevice;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	vmaCreateAllocator(&allocatorInfo, &_allocator);

	vkGetPhysicalDeviceProperties(_physicalDevice, &_gpuProperties);

	std::cout << "The GPU has a minimum buffer alignment of" << _gpuProperties.limits.minUniformBufferOffsetAlignment << std::endl;
}

void VulkanEngine::init_swapchain()
{
	vkb::SwapchainBuilder swapchainBuilder{ _physicalDevice, _device, _surface };

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.use_default_format_selection()
		//use vsync present mode
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(_windowExtent.width, _windowExtent.height)
		.build()
		.value();

	//store swapchain and its related images
	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();

	_swapchainImageFormat = vkbSwapchain.image_format;

	_mainDeletionQueue.push_function([=]() {
		vkDestroySwapchainKHR(_device, _swapchain, nullptr);
	});
}

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

void VulkanEngine::init_commands()
{
	VkCommandPoolCreateInfo uploadCommandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily);
	//create pool for upload context
	VK_CHECK(vkCreateCommandPool(_device, &uploadCommandPoolInfo, nullptr, &_uploadContext._commandPool));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(_device, _uploadContext._commandPool, nullptr);
		});
}

void VulkanEngine::init_descriptor_layouts()
{
}

void VulkanEngine::init_sync_structures()
{
	VkFenceCreateInfo uploadFenceCreateInfo = {};
	uploadFenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	uploadFenceCreateInfo.pNext = nullptr;
	uploadFenceCreateInfo.flags = 0;

	VK_CHECK(vkCreateFence(_device, &uploadFenceCreateInfo, nullptr, &_uploadContext._uploadFence));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyFence(_device, _uploadContext._uploadFence, nullptr);
	});
}

void VulkanEngine::init_descriptors()
{
	std::vector<VkDescriptorPoolSize> sizes = {
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10}
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = 0;
	pool_info.maxSets = 10;
	pool_info.poolSizeCount = static_cast<uint32_t>(sizes.size());
	pool_info.pPoolSizes = sizes.data();

	vkCreateDescriptorPool(_device, &pool_info, nullptr, &_descriptorPool);

	const size_t sceneParamBufferSize = FRAME_OVERLAP * pad_uniform_buffer_size(sizeof(GPUSceneData));

	_sceneParameterBuffer = create_buffer(sceneParamBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	VkDescriptorSetLayoutBinding cameraBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);

	VkDescriptorSetLayoutBinding sceneBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1);

	std::array<VkDescriptorSetLayoutBinding, 2> bindings = { cameraBind, sceneBind };

	VkDescriptorSetLayoutCreateInfo setInfo = {};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.pNext = nullptr;

	setInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	setInfo.flags = 0;
	setInfo.pBindings = bindings.data();

	vkCreateDescriptorSetLayout(_device, &setInfo, nullptr, &_globalSetLayout);

	VkDescriptorSetLayoutCreateInfo camSetInfo = {};
	camSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	camSetInfo.pNext = nullptr;
	
	camSetInfo.bindingCount = 1;
	camSetInfo.flags = 0;
	camSetInfo.pBindings = &cameraBind;

	vkCreateDescriptorSetLayout(_device, &camSetInfo, nullptr, &_camSetLayout);

	VkDescriptorSetLayoutBinding objectBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);

	VkDescriptorSetLayoutCreateInfo set2Info = {};
	set2Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set2Info.pNext = nullptr;
	set2Info.bindingCount = 1;
	set2Info.flags = 0;
	set2Info.pBindings = &objectBind;

	vkCreateDescriptorSetLayout(_device, &set2Info, nullptr, &_objectSetLayout);

	VkDescriptorSetLayoutBinding textureBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);

	VkDescriptorSetLayoutCreateInfo set3Info = {};
	set3Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set3Info.pNext = nullptr;
	set3Info.bindingCount = 1;
	set3Info.flags = 0;
	set3Info.pBindings = &textureBind;

	vkCreateDescriptorSetLayout(_device, &set3Info, nullptr, &_singleTextureSetLayout);

	const int MAX_OBJECTS = 10000;
	for(int i = 0; i < FRAME_OVERLAP; i++)
	{
		_frames[i].objectBuffer = create_buffer(sizeof(GPUObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		_frames[i].cameraBuffer = create_buffer(sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.pNext = nullptr;
		allocInfo.descriptorPool = _descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &_globalSetLayout;

		vkAllocateDescriptorSets(_device, &allocInfo, &_frames[i].globalDescriptor);
	
		VkDescriptorSetAllocateInfo objectSetAlloc = {};
		objectSetAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		objectSetAlloc.pNext = nullptr;
		objectSetAlloc.descriptorPool = _descriptorPool;
		objectSetAlloc.descriptorSetCount = 1;
		objectSetAlloc.pSetLayouts = &_objectSetLayout;

		vkAllocateDescriptorSets(_device, &objectSetAlloc, &_frames[i].objectDescriptor);
	
	
		VkDescriptorBufferInfo cameraInfo = {};
		cameraInfo.buffer = _frames[i].cameraBuffer._buffer;
		cameraInfo.offset = 0;
		cameraInfo.range = sizeof(GPUCameraData);

		VkDescriptorBufferInfo sceneInfo = {};
		sceneInfo.buffer = _sceneParameterBuffer._buffer;
		sceneInfo.offset = 0;
		sceneInfo.range = sizeof(GPUSceneData);

		VkDescriptorBufferInfo objectBufferInfo;
		objectBufferInfo.buffer = _frames[i].objectBuffer._buffer;
		objectBufferInfo.offset = 0;
		objectBufferInfo.range = sizeof(GPUObjectData) * MAX_OBJECTS;

		VkWriteDescriptorSet cameraWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _frames[i].globalDescriptor, &cameraInfo, 0);

		VkWriteDescriptorSet sceneWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, _frames[i].globalDescriptor, &sceneInfo, 1);

		VkWriteDescriptorSet objectWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _frames[i].objectDescriptor, &objectBufferInfo, 0);

		std::array<VkWriteDescriptorSet, 3> setWrites = { cameraWrite, sceneWrite, objectWrite };

		vkUpdateDescriptorSets(_device, static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0, nullptr);
	}

	//DEFERRED DESCRIPTORS
	_objectBuffer = create_buffer(sizeof(GPUObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	VkDescriptorSetAllocateInfo objectSetAlloc = {};
	objectSetAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	objectSetAlloc.pNext = nullptr;
	objectSetAlloc.descriptorPool = _descriptorPool;
	objectSetAlloc.descriptorSetCount = 1;
	objectSetAlloc.pSetLayouts = &_objectSetLayout;

	VkDescriptorBufferInfo objectBufferInfo;
	objectBufferInfo.buffer = _objectBuffer._buffer;
	objectBufferInfo.offset = 0;
	objectBufferInfo.range = sizeof(GPUObjectData) * MAX_OBJECTS;

	vkAllocateDescriptorSets(_device, &objectSetAlloc, &_objectDescriptorSet);

	VkWriteDescriptorSet objectWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _objectDescriptorSet, &objectBufferInfo, 0);

	vkUpdateDescriptorSets(_device, 1, &objectWrite, 0, nullptr);

	//gbuffers
	VkDescriptorSetLayoutBinding position_bind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
	VkDescriptorSetLayoutBinding normal_bind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1);
	VkDescriptorSetLayoutBinding albedo_bind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2);

	std::array<VkDescriptorSetLayoutBinding, 3> deferred_set_layouts = { position_bind, normal_bind, albedo_bind };

	VkDescriptorSetLayoutCreateInfo deferred_layout_info = {};
	deferred_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	deferred_layout_info.pNext = nullptr;
	deferred_layout_info.flags = 0;
	deferred_layout_info.bindingCount = static_cast<uint32_t>(deferred_set_layouts.size());
	deferred_layout_info.pBindings = deferred_set_layouts.data();

	VK_CHECK(vkCreateDescriptorSetLayout(_device, &deferred_layout_info, nullptr, &_gbuffersSetLayout));

	VkDescriptorSetAllocateInfo deferred_set_alloc = {};
	deferred_set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	deferred_set_alloc.pNext = nullptr;
	deferred_set_alloc.descriptorPool = _descriptorPool;
	deferred_set_alloc.descriptorSetCount = 1;
	deferred_set_alloc.pSetLayouts = &_gbuffersSetLayout;

	vkAllocateDescriptorSets(_device, &deferred_set_alloc, &_deferred_descriptor_set);


	VkDescriptorImageInfo position_descriptor_image;
	position_descriptor_image.sampler = renderer->_defaultSampler;
	position_descriptor_image.imageView = renderer->_positionImageView;
	position_descriptor_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorImageInfo normal_descriptor_image;
	normal_descriptor_image.sampler = renderer->_defaultSampler;
	normal_descriptor_image.imageView = renderer->_normalImageView;
	normal_descriptor_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorImageInfo albedo_descriptor_image;
	albedo_descriptor_image.sampler = renderer->_defaultSampler;
	albedo_descriptor_image.imageView = renderer->_albedoImageView;
	albedo_descriptor_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet position_texture = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _deferred_descriptor_set, &position_descriptor_image, 0);
	VkWriteDescriptorSet normal_texture = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _deferred_descriptor_set, &normal_descriptor_image, 1);
	VkWriteDescriptorSet albedo_texture = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _deferred_descriptor_set, &albedo_descriptor_image, 2);

	std::array<VkWriteDescriptorSet, 3> setWrites = { position_texture, normal_texture, albedo_texture };

	vkUpdateDescriptorSets(_device, static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0, nullptr);

	//cam buffer
	_camBuffer = create_buffer(sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	VkDescriptorSetAllocateInfo cameraSetAllocInfo = {};
	cameraSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	cameraSetAllocInfo.pNext = nullptr;
	cameraSetAllocInfo.descriptorPool = _descriptorPool;
	cameraSetAllocInfo.descriptorSetCount = 1;
	cameraSetAllocInfo.pSetLayouts = &_camSetLayout;
	
	vkAllocateDescriptorSets(_device, &cameraSetAllocInfo, &_camDescriptorSet);

	VkDescriptorBufferInfo camBufferInfo = {};
	camBufferInfo.buffer = _camBuffer._buffer;
	camBufferInfo.offset = 0;
	camBufferInfo.range = sizeof(GPUCameraData);

	VkWriteDescriptorSet camWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _camDescriptorSet, &camBufferInfo, 0);

	vkUpdateDescriptorSets(_device, 1, &camWrite, 0, nullptr);
}

void VulkanEngine::init_pipelines()
{
	//default vertex shader for a mesh
	VkShaderModule meshVertShader;
	if (!load_shader_module("../shaders/tri_mesh.vert.spv", &meshVertShader))
	{
		std::cout << "Error when building the triangle vertex shader module" << std::endl;
	}
	else {
		std::cout << "Red Triangle vertex shader succesfully loaded" << std::endl;
	}

	//shader for default material
	VkShaderModule colorMeshShader;
	if(!load_shader_module("../shaders/default_lit.frag.spv", &colorMeshShader))
	{
		std::cout << "Error when building the triangle fragment shader module" << std::endl;
	}
	else 
	{
		std::cout << "Triangle fragment shader succesfully loaded" << std::endl;
	}

	//shader for textured material
	VkShaderModule texturedMeshShader;
	if (!load_shader_module("../shaders/textured_lit.frag.spv", &texturedMeshShader))
	{
		std::cout << "Error when building the textured mesh shader" << std::endl;
	}
	else
	{
		std::cout << "Textured mesh shader succesfully loaded" << endl;
	}

	// Layouts

	//mesh layout
	VkPipelineLayoutCreateInfo mesh_pipeline_layout_info = vkinit::pipeline_layout_create_info();

	VkPushConstantRange push_constant;
	push_constant.offset = 0;
	push_constant.size = sizeof(MeshPushConstants);
	push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	mesh_pipeline_layout_info.pPushConstantRanges = &push_constant;
	mesh_pipeline_layout_info.pushConstantRangeCount = 1;

	std::array<VkDescriptorSetLayout, 3> setLayouts = { _globalSetLayout, _objectSetLayout, _singleTextureSetLayout };

	mesh_pipeline_layout_info.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
	mesh_pipeline_layout_info.pSetLayouts = setLayouts.data();

	VK_CHECK(vkCreatePipelineLayout(_device, &mesh_pipeline_layout_info, nullptr, &_meshPipelineLayout));

	//PIPELINES
	PipelineBuilder pipelineBuilder;
	
	//color mesh pipeline

	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();
	//connect the pipeline builder vertex input info to the one we get from Vertex
	VertexInputDescription vertexDescription = Vertex::get_vertex_description();
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();

	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();


	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float)_windowExtent.width;
	pipelineBuilder._viewport.height = (float)_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;

	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = _windowExtent;

	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();
	pipelineBuilder._colorBlendAttachment.push_back(vkinit::color_blend_attachment_state());

	//add shaders
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, meshVertShader));

	//make sure that triangleFragShader is holding the compiled colored_triangle.frag
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, colorMeshShader));

	pipelineBuilder._pipelineLayout = _meshPipelineLayout;

	//build the mesh triangle pipeline
	_meshPipeline = pipelineBuilder.build_pipeline(_device, renderer->_defaultRenderPass);
	
	create_material(_meshPipeline, _meshPipelineLayout, "defaultmesh");


	//texture pipeline

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, meshVertShader));

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, texturedMeshShader));

	VkPipeline texPipeline = pipelineBuilder.build_pipeline(_device, renderer->_defaultRenderPass);
	create_material(texPipeline, _meshPipelineLayout, "texturedmesh");

	//deleting all of the vulkan shaders
	vkDestroyShaderModule(_device, texturedMeshShader, nullptr);
	vkDestroyShaderModule(_device, colorMeshShader, nullptr);
	vkDestroyShaderModule(_device, meshVertShader, nullptr);

	//adding the pipelines to the deletion queue
	_mainDeletionQueue.push_function([=]() {
		vkDestroyPipeline(_device, _meshPipeline, nullptr);
		vkDestroyPipeline(_device, texPipeline, nullptr);

		vkDestroyPipelineLayout(_device, _trianglePipelineLayout, nullptr);
		vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
	});
}

void VulkanEngine::init_deferred_pipelines()
{
	//SHADERS LOADING

	VkShaderModule deferredVertex;
	if(!load_shader_module("../shaders/deferred.vert.spv", &deferredVertex))
	{
		std::cout << "Error when building the deferred vertex shader" << std::endl;
	}
	else
	{
		std::cout << "Deferred vertex shader succesfully loaded" << endl;
	}

	VkShaderModule deferredFrag;
	if (!load_shader_module("../shaders/deferred.frag.spv", &deferredFrag))
	{
		std::cout << "Error when building the deferred frag shader" << std::endl;
	}
	else
	{
		std::cout << "Frag vertex shader succesfully loaded" << endl;
	}

	VkShaderModule lightVertex;
	if (!load_shader_module("../shaders/light.vert.spv", &lightVertex))
	{
		std::cout << "Error when building the light vertex shader" << std::endl;
	}
	else
	{
		std::cout << "Light vertex shader succesfully loaded" << endl;
	}

	VkShaderModule lightFrag;
	if (!load_shader_module("../shaders/light.frag.spv", &lightFrag))
	{
		std::cout << "Error when building the light frag shader" << std::endl;
	}
	else
	{
		std::cout << "Light frag shader succesfully loaded" << endl;
	}

	//LAYOUTS
	VkPipelineLayoutCreateInfo layoutInfo = vkinit::pipeline_layout_create_info();

	std::array<VkDescriptorSetLayout, 3> deferredSetLayouts = {_camSetLayout, _objectSetLayout, _singleTextureSetLayout};

	layoutInfo.pushConstantRangeCount = 0;
	layoutInfo.pPushConstantRanges = nullptr;
	layoutInfo.setLayoutCount = static_cast<uint32_t>(deferredSetLayouts.size());
	layoutInfo.pSetLayouts = deferredSetLayouts.data();
	
	VK_CHECK(vkCreatePipelineLayout(_device, &layoutInfo, nullptr, &_deferredPipelineLayout));

	std::array<VkDescriptorSetLayout, 2> lightSetLayouts = { _globalSetLayout, _gbuffersSetLayout };

	layoutInfo.setLayoutCount = static_cast<uint32_t>(lightSetLayouts.size());
	layoutInfo.pSetLayouts = lightSetLayouts.data();

	VK_CHECK(vkCreatePipelineLayout(_device, &layoutInfo, nullptr, &_lightPipelineLayout));

	//DEFERRED PIPELINE CREATION
	PipelineBuilder pipelineBuilder;

	//Deferred Vertex Info
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	VertexInputDescription vertexDescription = Vertex::get_vertex_description();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexDescription.attributes.size());
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexDescription.bindings.size());
	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();

	//Deferred Assembly Info
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	//Viewport and Scissor
	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float)_windowExtent.width;
	pipelineBuilder._viewport.height = (float)_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;
	
	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = _windowExtent;

	//Deferred Depth Stencil
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	//Deferred Rasterizer
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

	//Deferred Multisampling
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	//Deferred Color Blend Attachment
	pipelineBuilder._colorBlendAttachment.push_back(vkinit::color_blend_attachment_state());
	pipelineBuilder._colorBlendAttachment.push_back(vkinit::color_blend_attachment_state());
	pipelineBuilder._colorBlendAttachment.push_back(vkinit::color_blend_attachment_state());

	//Deferred Shaders
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, deferredVertex));
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, deferredFrag));

	//Deferred Layout
	pipelineBuilder._pipelineLayout = _deferredPipelineLayout;

	_deferredPipeline = pipelineBuilder.build_pipeline(_device, renderer->_deferredRenderPass);

	//LIGHT PIPELINE CREATION

	//Light Depth Stencil
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(false, false, VK_COMPARE_OP_ALWAYS);

	//Deferred Color Blend Attachment
	pipelineBuilder._colorBlendAttachment.clear();
	pipelineBuilder._colorBlendAttachment.push_back(vkinit::color_blend_attachment_state());

	//Light Shaders
	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, lightVertex));
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, lightFrag));

	//Light Layout
	pipelineBuilder._pipelineLayout = _lightPipelineLayout;

	_lightPipeline = pipelineBuilder.build_pipeline(_device, renderer->_defaultRenderPass);

	
	//DELETIONS
	
	vkDestroyShaderModule(_device, lightFrag, nullptr);
	vkDestroyShaderModule(_device, lightVertex, nullptr);
	vkDestroyShaderModule(_device, deferredFrag, nullptr);
	vkDestroyShaderModule(_device, deferredVertex, nullptr);

	_mainDeletionQueue.push_function([=]() {
		vkDestroyPipeline(_device, _deferredPipeline, nullptr);
		vkDestroyPipeline(_device, _lightPipeline, nullptr);

		vkDestroyPipelineLayout(_device, _deferredPipelineLayout, nullptr);
		vkDestroyPipelineLayout(_device, _lightPipelineLayout, nullptr);
		});
}


void VulkanEngine::init_scene()
{
	/*
	RenderObject monkey;
	monkey.mesh = get_mesh("monkey");
	monkey.material = get_material("defaultmesh");
	monkey.transformMatrix = glm::mat4(1.0f);

	_renderables.push_back(monkey);

	for(int x = -20; x < 20; x++)
	{
		for(int y = -20; y <= 20; y++)
		{
			RenderObject tri;
			tri.mesh = get_mesh("triangle");
			tri.material = get_material("defaultmesh");
			glm::mat4 translation = glm::translate(glm::mat4{ 1.0 }, glm::vec3(x, 0, y));
			glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(0.2, 0.2, 0.2));
			tri.transformMatrix = translation * scale;

			_renderables.push_back(tri);
		}
	}
	*/

	camera = new Camera(camera_default_position);

	RenderObject map;
	map.mesh = get_mesh("empire");
	map.material = get_material("texturedmesh");
	map.transformMatrix = glm::translate(glm::vec3{ 5, -10, 0 });

	_renderables.push_back(map);

	RenderObject monkey;
	monkey.mesh = get_mesh("monkey");
	monkey.material = get_material("defaultmesh");
	monkey.transformMatrix = glm::mat4(1.0f);

	_renderables.push_back(monkey);

	Material* texturedMat = get_material("texturedmesh");

	//allocate the descriptor set for single-texture to use on the material
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.pNext = nullptr;
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = _descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &_singleTextureSetLayout;

	vkAllocateDescriptorSets(_device, &allocInfo, &texturedMat->textureSet);

	//write to the descriptor set so that it points to our empire_diffuse texture
	VkDescriptorImageInfo imageBufferInfo;
	imageBufferInfo.sampler = renderer->_defaultSampler;
	imageBufferInfo.imageView = _loadedTextures["empire_diffuse"].imageView;
	imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet texture1 = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, texturedMat->textureSet, &imageBufferInfo, 0);

	vkUpdateDescriptorSets(_device, 1, &texture1, 0, nullptr);
}

bool VulkanEngine::load_shader_module(const char* filePath, VkShaderModule* outShaderModule)
{
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if(!file.is_open()) {
		return false;
	}

	size_t fileSize = (size_t)file.tellg();

	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	file.seekg(0);

	file.read((char*)buffer.data(), fileSize);

	file.close();

	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	VkShaderModule shaderModule;
	if(vkCreateShaderModule(_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		return false;
	}

	*outShaderModule = shaderModule;
	return true;
}

void VulkanEngine::load_meshes()
{
	/*
	Mesh triangleMesh;
	triangleMesh._vertices.resize(3);

	triangleMesh._vertices[0].position = { 1.0f, 1.0f, 0.0f };
	triangleMesh._vertices[1].position = { -1.0f, 1.0f, 0.0f };
	triangleMesh._vertices[2].position = { 0.0f, -1.0f, 0.0f };

	triangleMesh._vertices[0].normal = { 0.0f, 1.0f, 0.0f };
	triangleMesh._vertices[1].normal = { 0.0f, 1.0f, 0.0f };
	triangleMesh._vertices[2].normal = { 0.0f, 1.0f, 0.0f };
	
	triangleMesh._vertices[0].color = { 0.0f, 1.0f, 0.0f };
	triangleMesh._vertices[1].color = { 0.0f, 1.0f, 0.0f };
	triangleMesh._vertices[2].color = { 0.0f, 1.0f, 0.0f };
	
	triangleMesh._indices.resize(3);
	
	triangleMesh._indices = { 0, 1, 2 };
	*/
	Mesh monkeyMesh("../assets/monkey_smooth.obj");

	Mesh lostEmpire("../assets/lost_empire.obj");

	//_meshes["triangle"] = triangleMesh;
	_meshes["monkey"] = monkeyMesh;
	_meshes["empire"] = lostEmpire;

	//quad for deferred
	//deferred_quad.create_quad();
}

Material* VulkanEngine::create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name)
{
	Material mat;
	mat.pipeline = pipeline;
	mat.pipelineLayout = layout;
	mat.textureSet = VK_NULL_HANDLE;
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

void VulkanEngine::update_descriptors(RenderObject* first, int count)
{
	//Update buffers info
	glm::vec3 camPos = { 0.0f, -50.0f, -10.0f };
	glm::mat4 view = glm::translate(glm::mat4(1.0f), camPos);
	glm::mat4 projection = glm::perspective(glm::radians(70.0f), 1700.0f / 900.0f, 0.1f, 200.0f);
	projection[1][1] *= -1;

	GPUCameraData camData;
	camData.projection = projection;
	camData.view = camera->getView();
	camData.viewproj = projection * camera->getView();

	void* data;
	vmaMapMemory(_allocator, get_current_frame().cameraBuffer._allocation, &data);
	memcpy(data, &camData, sizeof(GPUCameraData));
	vmaUnmapMemory(_allocator, get_current_frame().cameraBuffer._allocation);

	void* firstPassData;
	vmaMapMemory(_allocator, _camBuffer._allocation, &firstPassData);
	memcpy(firstPassData, &camData, sizeof(GPUCameraData));
	vmaUnmapMemory(_allocator, _camBuffer._allocation);

	float framed = { _frameNumber / 120.0f };

	_sceneParameters.ambientColor = { sin(framed), 0, cos(framed), 1 };

	char* sceneData;
	vmaMapMemory(_allocator, _sceneParameterBuffer._allocation, (void**)&sceneData);

	int frameIndex = _frameNumber % FRAME_OVERLAP;

	sceneData += pad_uniform_buffer_size(sizeof(GPUSceneData)) * frameIndex;

	memcpy(sceneData, &_sceneParameters, sizeof(GPUSceneData));

	vmaUnmapMemory(_allocator, _sceneParameterBuffer._allocation);

	void* objectData;
	vmaMapMemory(_allocator, _objectBuffer._allocation, &objectData);

	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;

	for (int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];
		objectSSBO[i].modelMatrix = object.transformMatrix;
	}

	vmaUnmapMemory(_allocator, _objectBuffer._allocation);
}

void VulkanEngine::update_descriptors_forward(RenderObject* first, int count)
{
	glm::vec3 camPos = { 0.0f, -50.0f, -10.0f };
	glm::mat4 view = glm::translate(glm::mat4(1.0f), camPos);
	glm::mat4 projection = glm::perspective(glm::radians(70.0f), 1700.0f / 900.0f, 0.1f, 200.0f);
	projection[1][1] *= -1;

	GPUCameraData camData;
	camData.projection = projection;
	camData.view = view;
	camData.viewproj = projection * view;

	void* data;
	vmaMapMemory(_allocator, get_current_frame().cameraBuffer._allocation, &data);
	memcpy(data, &camData, sizeof(GPUCameraData));
	vmaUnmapMemory(_allocator, get_current_frame().cameraBuffer._allocation);

	float framed = { _frameNumber / 120.0f };

	_sceneParameters.ambientColor = { sin(framed), 0, cos(framed), 1 };

	char* sceneData;
	vmaMapMemory(_allocator, _sceneParameterBuffer._allocation, (void**)&sceneData);

	int frameIndex = _frameNumber % FRAME_OVERLAP;

	sceneData += pad_uniform_buffer_size(sizeof(GPUSceneData)) * frameIndex;

	memcpy(sceneData, &_sceneParameters, sizeof(GPUSceneData));

	vmaUnmapMemory(_allocator, _sceneParameterBuffer._allocation);

	void* objectData;
	vmaMapMemory(_allocator, get_current_frame().objectBuffer._allocation, &objectData);

	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;

	for (int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];
		objectSSBO[i].modelMatrix = object.transformMatrix;
	}

	vmaUnmapMemory(_allocator, get_current_frame().objectBuffer._allocation);
}

AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;

	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memoryUsage;

	AllocatedBuffer newBuffer;

	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo,
		&newBuffer._buffer,
		&newBuffer._allocation,
		nullptr));

	return newBuffer;
}

void VulkanEngine::load_images()
{
	Texture lostEmpire;

	vkutil::load_image_from_file(*this, "../assets/lost_empire-RGBA.png", lostEmpire.image);
	
	VkImageViewCreateInfo imageinfo = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, lostEmpire.image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(_device, &imageinfo, nullptr, &lostEmpire.imageView);

	_loadedTextures["empire_diffuse"] = lostEmpire;
}

size_t VulkanEngine::pad_uniform_buffer_size(size_t originalSize)
{
	size_t minUboAlignment = _gpuProperties.limits.minUniformBufferOffsetAlignment;
	size_t alignedSize = originalSize;
	if(minUboAlignment > 0)
	{
		alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
	}

	return alignedSize;
}

VkPipeline PipelineBuilder::build_pipeline(VkDevice device, VkRenderPass pass)
{
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.pNext = nullptr;

	viewportState.viewportCount = 1;
	viewportState.pViewports = &_viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &_scissor;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.pNext = nullptr;

	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = static_cast<uint32_t>(_colorBlendAttachment.size());
	colorBlending.pAttachments = _colorBlendAttachment.data();


	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = nullptr;

	pipelineInfo.stageCount = static_cast<uint32_t>(_shaderStages.size());
	pipelineInfo.pStages = _shaderStages.data();
	pipelineInfo.pVertexInputState = &_vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &_inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pDepthStencilState = &_depthStencil;
	pipelineInfo.pRasterizationState = &_rasterizer;
	pipelineInfo.pMultisampleState = &_multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.layout = _pipelineLayout;
	pipelineInfo.renderPass = pass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	VkPipeline newPipeline;
	if (vkCreateGraphicsPipelines(
		device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS)
	{
		std::cout << "failed to create pipeline\n";
		return VK_NULL_HANDLE;
	}
	else
	{
		return newPipeline;
	}
}
