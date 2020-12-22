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

std::vector<const char*> required_device_extensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
		VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
		VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME,

		// VkRay
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
		//
		//// Required by VK_KHR_acceleration_structure
		VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,

		// Required by VK_KHR_raytracing_pipeline
		VK_KHR_SPIRV_1_4_EXTENSION_NAME,

		// Required by VK_KHR_spirv_1_4
		VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME
};

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

	init_descriptor_set_layouts();

	init_descriptor_set_pool();

	init_descriptors();

	renderer = new GRAPHICS::Renderer();
	renderer->init_renderer();
	renderer->create_pipelines();

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

		vkWaitForFences(_device, 1, &renderer->_frames[renderer->get_current_frame_index()]._renderFence, true, UINT64_MAX);

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
		.enable_extension("VK_KHR_get_physical_device_properties2")
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
		.add_required_extensions(required_device_extensions)
		.select()
		.value();

	get_enabled_features();

	//create the final Vulkan device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };

	vkb::Device vkbDevice = deviceBuilder.add_pNext(deviceCreatepNextChain).build().value();

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

void VulkanEngine::init_raytracing()
{
	vkGetBufferDeviceAddressKHR = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(vkGetDeviceProcAddr(VulkanEngine::cinstance->_device, "vkGetBufferDeviceAddressKHR"));

	_rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
	VkPhysicalDeviceProperties2 deviceProperties2{};
	deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	deviceProperties2.pNext = &_rayTracingPipelineProperties;
	vkGetPhysicalDeviceProperties2(_physicalDevice, &deviceProperties2);

	// Requesting ray tracing features
	_accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	VkPhysicalDeviceFeatures2 deviceFeatures2{};
	deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	deviceFeatures2.pNext = &_accelerationStructureFeatures;
	vkGetPhysicalDeviceFeatures2(_physicalDevice, &deviceFeatures2);
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

void VulkanEngine::init_descriptor_set_pool()
{
	std::vector<VkDescriptorPoolSize> sizes = {
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10}
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = 0;
	pool_info.maxSets = 10;
	pool_info.poolSizeCount = static_cast<uint32_t>(sizes.size());
	pool_info.pPoolSizes = sizes.data();

	vkCreateDescriptorPool(_device, &pool_info, nullptr, &_descriptorPool);
}

void VulkanEngine::init_descriptor_set_layouts()
{
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

	//DESCRIPTOR SET BUFFER CREATION
	const size_t sceneParamBufferSize = FRAME_OVERLAP * get_aligned_size(sizeof(GPUSceneData), _gpuProperties.limits.minUniformBufferOffsetAlignment);

	_sceneParameterBuffer = create_buffer(sceneParamBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	for(int i = 0; i < FRAME_OVERLAP; i++)
	{
		_frames[i].objectBuffer = create_buffer(sizeof(GPUObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		_frames[i].cameraBuffer = create_buffer(sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		//cam buffer
		_camBuffer = create_buffer(sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	}

	//deferred
	_objectBuffer = create_buffer(sizeof(GPUObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
}

void VulkanEngine::init_descriptors()
{
	for(int i = 0; i < FRAME_OVERLAP; i++)
	{
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

void VulkanEngine::init_scene()
{
	camera = new Camera(camera_default_position);

	RenderObject map;
	map._mesh = get_mesh("empire");
	map._material = get_material("texturedmesh");
	map._model = glm::translate(glm::vec3{ 5, -10, 0 });

	_renderables.push_back(map);

	RenderObject monkey;
	monkey._mesh = get_mesh("monkey");
	monkey._material = get_material("defaultmesh");
	monkey._model = glm::mat4(1.0f);

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
	Mesh monkeyMesh("../assets/monkey_smooth.obj");

	Mesh lostEmpire("../assets/lost_empire.obj");

	_meshes["monkey"] = monkeyMesh;
	_meshes["empire"] = lostEmpire;
}

void VulkanEngine::get_enabled_features()
{
	_enabledBufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
	_enabledBufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
	_enabledBufferDeviceAddressFeatures.pNext = nullptr;

	_enabledRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
	_enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
	_enabledRayTracingPipelineFeatures.rayTracingPipelineTraceRaysIndirect = VK_TRUE;
	_enabledRayTracingPipelineFeatures.pNext = &_enabledBufferDeviceAddressFeatures;

	_enabledAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	_enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
	_enabledAccelerationStructureFeatures.pNext = &_enabledRayTracingPipelineFeatures;

	deviceCreatepNextChain = &_enabledAccelerationStructureFeatures;
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

	sceneData += get_aligned_size(sizeof(GPUSceneData), _gpuProperties.limits.minUniformBufferOffsetAlignment) * frameIndex;

	memcpy(sceneData, &_sceneParameters, sizeof(GPUSceneData));

	vmaUnmapMemory(_allocator, _sceneParameterBuffer._allocation);

	void* objectData;
	vmaMapMemory(_allocator, _objectBuffer._allocation, &objectData);

	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;

	for (int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];
		objectSSBO[i].modelMatrix = object._model;
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

	sceneData += get_aligned_size(sizeof(GPUSceneData), _gpuProperties.limits.minUniformBufferOffsetAlignment) * frameIndex;

	memcpy(sceneData, &_sceneParameters, sizeof(GPUSceneData));

	vmaUnmapMemory(_allocator, _sceneParameterBuffer._allocation);

	void* objectData;
	vmaMapMemory(_allocator, get_current_frame().objectBuffer._allocation, &objectData);

	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;

	for (int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];
		objectSSBO[i].modelMatrix = object._model;
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

size_t VulkanEngine::get_aligned_size(size_t originalSize, uint32_t alignment)
{
	//size_t minUboAlignment = _gpuProperties.limits.minUniformBufferOffsetAlignment;
	size_t alignedSize = originalSize;
	if(alignment > 0)
	{
		alignedSize = (alignedSize + alignment - 1) & ~(alignment - 1);
	}

	return alignedSize;
}

uint32_t VulkanEngine::find_memory_type_index(uint32_t allowedTypes, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties memoryProperties;
	vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &memoryProperties);

	for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
	{
		if ((allowedTypes & (1 << i))
			&& (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
		{
			return i;
		}
	}
}

uint64_t VulkanEngine::get_buffer_device_address(VkBuffer buffer)
{
	VkBufferDeviceAddressInfoKHR bufferDeviceAI{};
	bufferDeviceAI.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	bufferDeviceAI.buffer = buffer;
	return vkGetBufferDeviceAddressKHR(_device, &bufferDeviceAI);
}
