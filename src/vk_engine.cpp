#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "vk_engine.h"
#include "vk_textures.h"
#include "vk_material.h"
#include "vk_utils.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <extra/imgui/imgui.h>
#include <extra/imgui/imgui_impl_sdl.h>
#include <extra/imgui/imgui_impl_vulkan.h>
#include <extra/imgui/ImGuizmo.h>

#include <glm/gtx/transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "vk_initializers.h"

#include "VkBootstrap.h"

#include <array>

#include <chrono>

VulkanEngine* VulkanEngine::cinstance = nullptr;
Renderer* renderer = nullptr;

void VulkanEngine::init()
{
	Timer timer("Init Function");

	cinstance = this;

	camera = new Camera();
	camera->_speed = 1.0f;
	camera->_position = glm::vec3(0, 150, 100);
	//camera->setOrthographic(-128, 128, -128, 128, -500, 500);

	//camera->_direction = -camera->_position;
	
	renderer = new Renderer();
	
	_window = renderer->get_sdl_window();

	load_images();

	//load_meshes();

	VKE::Texture* texture = VKE::Texture::sTexturesLoaded["default"];
	VKE::Material* defaultMaterial = new VKE::Material(texture);
	defaultMaterial->_id = VKE::Material::sMaterials.size();
	defaultMaterial->register_material("default");

	scene = new Scene();
	scene->load_resources();
	scene->generate_sample_scene();
	//scene->generate_random_sample_scene();

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
					int& rm = renderer->_shaderFlags.flags.z;

					if (rm < 3) 
					{
						rm++; 
					}
					else
					{
						rm = 0;
					}
				}

				if(e.key.keysym.sym == SDLK_r)
				{
					renderer->reset_timers_count();
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

			camera->rotate(xMouseDiff, yMouseDiff);

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
		ImGuizmo::BeginFrame();

		//imgui commands
		render_debug_gizmo();
		render_debug_GUI();

		renderer->draw_scene();
	}
}

void VulkanEngine::render_debug_GUI()
{
	ImGui::Text("MAIN IMGUI DEBUG WINDOW:");
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

	ImGui::SliderFloat("Camera speed", &camera->_speed, 0.1f, 5.0f, "%.3f");

	ImGui::SliderInt("Shadow render mode", &renderer->_rtPushConstant.flags.x, 0, 2, "%d");

	ImGui::SliderInt("Show deep shadow map", &renderer->_shaderFlags.flags.x, 0, 1, "%d");

	ImGui::SliderInt("DSM Show Depth Mode", &renderer->_shaderFlags.flags.y, 0, 1, "%d");

	ImGui::SliderFloat("Shadow Bias", &renderer->_rtPushConstant.frame_bias.y, 0.0f, 5.0f, "%.4f", 2.0f);

	ImGui::SliderFloat("Visibility Bias", &renderer->_rtPushConstant.frame_bias.z, 0.0f, 1.0f, "%.4f", 2.0f);

	ImGui::SliderInt("Kernel Size", &renderer->_rtPushConstant.flags.y, 1, 10, "%d");

	if (ImGui::Button("Use wait idle"))
		renderer->_isUsingWaitIdle = !renderer->_isUsingWaitIdle;

	// Lights
	for(int i = 0; i < scene->_lights.size(); i++)
	{
		if(ImGui::TreeNode(&scene->_lights.at(i), "Light"))
		{
			scene->_lights.at(i).renderInMenu();
			ImGui::TreePop();
		}
	}

	// Render prefabs
	for(int i = 0; i < scene->_renderables.size(); i++)
	{
		if(ImGui::TreeNode(&scene->_renderables.at(i), "Prefab"))
		{
			scene->_renderables.at(i).renderInMenu();
			ImGui::TreePop();
		}
	}
}

void VulkanEngine::render_debug_gizmo()
{
	if (!gizmoEntity)
		return;

	glm::mat4& matrix = gizmoEntity->_model;

	static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);
	static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::WORLD);
	if (ImGui::IsKeyPressed(90))
		mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
	if (ImGui::IsKeyPressed(69))
		mCurrentGizmoOperation = ImGuizmo::ROTATE;
	if (ImGui::IsKeyPressed(82))
		mCurrentGizmoOperation = ImGuizmo::SCALE;
	if (ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE))
		mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
	ImGui::SameLine();
	if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE))
		mCurrentGizmoOperation = ImGuizmo::ROTATE;
	ImGui::SameLine();
	if (ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE))
		mCurrentGizmoOperation = ImGuizmo::SCALE;
	float matrixTranslation[3], matrixRotation[3], matrixScale[3];
	ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(matrix), matrixTranslation, matrixRotation, matrixScale);
	ImGui::InputFloat3("Tr", matrixTranslation, 3);
	ImGui::InputFloat3("Rt", matrixRotation, 3);
	ImGui::InputFloat3("Sc", matrixScale, 3);
	ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, glm::value_ptr(matrix));

	if(mCurrentGizmoOperation != ImGuizmo::SCALE)
	{
		if (ImGui::RadioButton("Local", mCurrentGizmoMode == ImGuizmo::LOCAL))
			mCurrentGizmoMode = ImGuizmo::LOCAL;
		ImGui::SameLine();
		if (ImGui::RadioButton("World", mCurrentGizmoMode == ImGuizmo::WORLD))
			mCurrentGizmoMode = ImGuizmo::WORLD;
	}
	static bool useSnap(false);
	if (ImGui::IsKeyPressed(83))
		useSnap = !useSnap;
	ImGui::Checkbox("", &useSnap);
	ImGui::SameLine();
	static glm::vec3 snap;
	switch(mCurrentGizmoOperation)
	{
	case ImGuizmo::TRANSLATE:
		ImGui::InputFloat3("Snap", &snap.x);
		break;
	case ImGuizmo::ROTATE:
		ImGui::InputFloat("Angle Snap", &snap.x);
		break;
	case ImGuizmo::SCALE:
		ImGui::InputFloat("Scale Snap", &snap.x);
		break;
	}
	ImGuiIO& io = ImGui::GetIO();
	ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
	ImGuizmo::Manipulate(glm::value_ptr(camera->getView()), glm::value_ptr(camera->getProjection()), mCurrentGizmoOperation, 
		mCurrentGizmoMode, glm::value_ptr(matrix), NULL, useSnap ? &snap.x : NULL);
}

void VulkanEngine::load_meshes()
{
	VKE::Mesh::get("../assets/MapleTree.obj");
	VKE::Mesh::get("../assets/MapleTreeLeaves.obj");
	VKE::Mesh::get("../assets/MapleTreeStem.obj");
}

void VulkanEngine::load_images()
{
	VKE::Texture* defaultTexture = new VKE::Texture();

	vkutil::load_image_from_file(&std::string("../assets/plain.jpg"), defaultTexture->_image);

	VkImageViewCreateInfo imageinfo = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, defaultTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &imageinfo, nullptr, &defaultTexture->_imageView);

	defaultTexture->_id = VKE::Texture::sTexturesLoaded.size();
	defaultTexture->register_texture("default");


	VKE::Texture* grassTexture = new VKE::Texture();

	vkutil::load_image_from_file(&std::string("../assets/grass.jpg"), grassTexture->_image);

	VkImageViewCreateInfo imageinfo2 = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, grassTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &imageinfo2, nullptr, &grassTexture->_imageView);

	grassTexture->_id = VKE::Texture::sTexturesLoaded.size();
	grassTexture->register_texture("grass");

	// Maple tree

	VKE::Texture* mapleBarkTexture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/maple/maple_bark.png"), mapleBarkTexture->_image);
	VkImageViewCreateInfo mapleBarkTexView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, mapleBarkTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &mapleBarkTexView, nullptr, &mapleBarkTexture->_imageView);
	mapleBarkTexture->_id = VKE::Texture::sTexturesLoaded.size();
	mapleBarkTexture->register_texture("maple_bark");

	VKE::Texture* mapleLeavesTexture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/maple/maple_leaf.png"), mapleLeavesTexture->_image);
	VkImageViewCreateInfo mapleLeavesTexView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, mapleLeavesTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &mapleLeavesTexView, nullptr, &mapleLeavesTexture->_imageView);
	mapleLeavesTexture->_id = VKE::Texture::sTexturesLoaded.size();
	mapleLeavesTexture->register_texture("maple_leaf");

	VKE::Texture* mapleLeavesOcclusionTexture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/maple/maple_leaf_Mask.jpg"), mapleLeavesOcclusionTexture->_image);
	VkImageViewCreateInfo mapleLeavesOcclusionView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, mapleLeavesOcclusionTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &mapleLeavesOcclusionView, nullptr, &mapleLeavesOcclusionTexture->_imageView);
	mapleLeavesOcclusionTexture->_id = VKE::Texture::sTexturesLoaded.size();
	mapleLeavesOcclusionTexture->register_texture("maple_leaf_occlusion");

	// Oak tree

	VKE::Texture* oakBarkTexture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/oak/bark_tree.jpg"), oakBarkTexture->_image);
	VkImageViewCreateInfo oakBarkTexView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, oakBarkTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &oakBarkTexView, nullptr, &oakBarkTexture->_imageView);
	oakBarkTexture->_id = VKE::Texture::sTexturesLoaded.size();
	oakBarkTexture->register_texture("oak_bark");

	VKE::Texture* oakLeavesTexture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/oak/leaves_01.jpg"), oakLeavesTexture->_image);
	VkImageViewCreateInfo oakLeavesTexView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, oakLeavesTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &oakLeavesTexView, nullptr, &oakLeavesTexture->_imageView);
	oakLeavesTexture->_id = VKE::Texture::sTexturesLoaded.size();
	oakLeavesTexture->register_texture("oak_leaf");

	VKE::Texture* oakLeavesOcclusionTexture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/oak/leaves_alpha_inverted.jpg"), oakLeavesOcclusionTexture->_image);
	VkImageViewCreateInfo oakLeavesOcclusionView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, oakLeavesOcclusionTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &oakLeavesOcclusionView, nullptr, &oakLeavesOcclusionTexture->_imageView);
	oakLeavesOcclusionTexture->_id = VKE::Texture::sTexturesLoaded.size();
	oakLeavesTexture->register_texture("oak_leaf_occlusion");

	// Broadleaf tree

	VKE::Texture* broadleafBarkTexture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/broadleaf/bark.png"), broadleafBarkTexture->_image);
	VkImageViewCreateInfo broadleafBarkTexView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, broadleafBarkTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &broadleafBarkTexView, nullptr, &broadleafBarkTexture->_imageView);
	broadleafBarkTexture->_id = VKE::Texture::sTexturesLoaded.size();
	broadleafBarkTexture->register_texture("broadleaf_bark");

	VKE::Texture* broadleafLeavesTexture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/broadleaf/leaf.png"), broadleafLeavesTexture->_image);
	VkImageViewCreateInfo broadleafLeavesTexView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, broadleafLeavesTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &broadleafLeavesTexView, nullptr, &broadleafLeavesTexture->_imageView);
	broadleafLeavesTexture->_id = VKE::Texture::sTexturesLoaded.size();
	broadleafBarkTexture->register_texture("broadleaf_leaf");

	// Rainforest tree

	VKE::Texture* rainforestBarkTexture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/rainforest_tree/bark.jpg"), rainforestBarkTexture->_image);
	VkImageViewCreateInfo rainforestBarkTexView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, rainforestBarkTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &rainforestBarkTexView, nullptr, &rainforestBarkTexture->_imageView);
	rainforestBarkTexture->_id = VKE::Texture::sTexturesLoaded.size();
	rainforestBarkTexture->register_texture("rainforest_bark");

	VKE::Texture* rainforestLeavesTexture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/rainforest_tree/leaves_winter.png"), rainforestLeavesTexture->_image);
	VkImageViewCreateInfo rainforestLeavesTexView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, rainforestLeavesTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &rainforestLeavesTexView, nullptr, &rainforestLeavesTexture->_imageView);
	rainforestLeavesTexture->_id = VKE::Texture::sTexturesLoaded.size();
	rainforestLeavesTexture->register_texture("rainforest_leaf");

	// Random trees
	VKE::Texture* walnutTexture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/trees/Texture/Walnut_L.jpg"), walnutTexture->_image);
	VkImageViewCreateInfo walnutTexView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, walnutTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &walnutTexView, nullptr, &walnutTexture->_imageView);
	walnutTexture->_id = VKE::Texture::sTexturesLoaded.size();
	walnutTexture->register_texture("walnut");

	VKE::Texture* mossyTexture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/trees/Texture/Mossy_Tr.jpg"), mossyTexture->_image);
	VkImageViewCreateInfo mossyTexView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, mossyTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &mossyTexView, nullptr, &mossyTexture->_imageView);
	mossyTexture->_id = VKE::Texture::sTexturesLoaded.size();
	mossyTexture->register_texture("mossy");

	VKE::Texture* bark_STexture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/trees/Texture/Bark___S.jpg"), bark_STexture->_image);
	VkImageViewCreateInfo bark_STexView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, bark_STexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &bark_STexView, nullptr, &bark_STexture->_imageView);
	bark_STexture->_id = VKE::Texture::sTexturesLoaded.size();
	bark_STexture->register_texture("bark_s");

	VKE::Texture* bark_0Texture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/trees/Texture/Bark___0.jpg"), bark_0Texture->_image);
	VkImageViewCreateInfo bark_0TexView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, bark_0Texture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &bark_0TexView, nullptr, &bark_0Texture->_imageView);
	bark_0Texture->_id = VKE::Texture::sTexturesLoaded.size();
	bark_0Texture->register_texture("bark_0");

	VKE::Texture* bottom_TTexture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/trees/Texture/Bottom_T.jpg"), bottom_TTexture->_image);
	VkImageViewCreateInfo bottom_TTexView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, bottom_TTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &bottom_TTexView, nullptr, &bottom_TTexture->_imageView);
	bottom_TTexture->_id = VKE::Texture::sTexturesLoaded.size();
	bottom_TTexture->register_texture("bottom_t");

	VKE::Texture* sonneratTexture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/trees/Texture/Sonnerat.jpg"), sonneratTexture->_image);
	VkImageViewCreateInfo sonneratTexView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, sonneratTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &sonneratTexView, nullptr, &sonneratTexture->_imageView);
	sonneratTexture->_id = VKE::Texture::sTexturesLoaded.size();
	sonneratTexture->register_texture("sonnerat");

	VKE::Texture* bark_1Texture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/trees/Texture/Bark___1.jpg"), bark_1Texture->_image);
	VkImageViewCreateInfo bark_1TexView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, bark_1Texture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &bark_1TexView, nullptr, &bark_1Texture->_imageView);
	bark_1Texture->_id = VKE::Texture::sTexturesLoaded.size();
	bark_1Texture->register_texture("bark_1");

	VKE::Texture* oak_LTexture = new VKE::Texture();
	vkutil::load_image_from_file(&std::string("../assets/vegetation/trees/Texture/Oak_Leav.jpg"), oak_LTexture->_image);
	VkImageViewCreateInfo oak_LTexView = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, oak_LTexture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(RenderEngine::_device, &oak_LTexView, nullptr, &oak_LTexture->_imageView);
	oak_LTexture->_id = VKE::Texture::sTexturesLoaded.size();
	oak_LTexture->register_texture("oak_l");


	RenderEngine::_mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(RenderEngine::_device, defaultTexture->_imageView, nullptr);
		vkDestroyImageView(RenderEngine::_device, grassTexture->_imageView, nullptr);
		vkDestroyImageView(RenderEngine::_device, mapleBarkTexture->_imageView, nullptr);
		vkDestroyImageView(RenderEngine::_device, mapleLeavesTexture->_imageView, nullptr);
		vkDestroyImageView(RenderEngine::_device, mapleLeavesOcclusionTexture->_imageView, nullptr);
		vkDestroyImageView(RenderEngine::_device, oakBarkTexture->_imageView, nullptr);
		vkDestroyImageView(RenderEngine::_device, oakLeavesTexture->_imageView, nullptr);
		vkDestroyImageView(RenderEngine::_device, oakLeavesOcclusionTexture->_imageView, nullptr);
		vkDestroyImageView(RenderEngine::_device, broadleafBarkTexture->_imageView, nullptr);
		vkDestroyImageView(RenderEngine::_device, broadleafLeavesTexture->_imageView, nullptr);
		vkDestroyImageView(RenderEngine::_device, rainforestBarkTexture->_imageView, nullptr);
		vkDestroyImageView(RenderEngine::_device, rainforestLeavesTexture->_imageView, nullptr);

		vmaDestroyImage(RenderEngine::_allocator, defaultTexture->_image._image, nullptr);
		vmaDestroyImage(RenderEngine::_allocator, grassTexture->_image._image, nullptr);
		vmaDestroyImage(RenderEngine::_allocator, mapleBarkTexture->_image._image, nullptr);
		vmaDestroyImage(RenderEngine::_allocator, mapleLeavesTexture->_image._image, nullptr);
		vmaDestroyImage(RenderEngine::_allocator, mapleLeavesOcclusionTexture->_image._image, nullptr);
		vmaDestroyImage(RenderEngine::_allocator, oakBarkTexture->_image._image, nullptr);
		vmaDestroyImage(RenderEngine::_allocator, oakLeavesTexture->_image._image, nullptr);
		vmaDestroyImage(RenderEngine::_allocator, oakLeavesOcclusionTexture->_image._image, nullptr);
		vmaDestroyImage(RenderEngine::_allocator, broadleafBarkTexture->_image._image, nullptr);
		vmaDestroyImage(RenderEngine::_allocator, broadleafLeavesTexture->_image._image, nullptr);
		vmaDestroyImage(RenderEngine::_allocator, rainforestBarkTexture->_image._image, nullptr);
		vmaDestroyImage(RenderEngine::_allocator, rainforestLeavesTexture->_image._image, nullptr);
		});
}
