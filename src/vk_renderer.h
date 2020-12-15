#pragma once

#include "vk_types.h"
#include"vk_mesh.h"

namespace GRAPHICS
{
	constexpr unsigned int FRAME_OVERLAP = 2;

	enum RenderMode {
		FORWARD_MODE = 0,
		DEFERRED_MODE
	};

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

		void draw_scene();

	private:

		//Queues
		VkQueue _graphicsQueue;
		uint32_t _graphicsQueueFamily;

		//Depth Buffer
		VkImageView _depthImageView;
		AllocatedImage _depthImage;
		VkFormat _depthFormat;

		//deferred attachments
		VkImageView _positionImageView;
		AllocatedImage _positionImage;
		VkFormat _positionFormat;

		VkImageView _normalImageView;
		AllocatedImage _normalImage;
		VkFormat _normalFormat;

		VkImageView _albedoImageView;
		AllocatedImage _albedoImage;
		VkFormat _albedoFormat;

		Mesh deferred_quad;

		//Render passes
		VkRenderPass _defaultRenderPass;
		VkRenderPass _deferredRenderPass;

		//Framebuffers
		std::vector<VkFramebuffer> _framebuffers;
		VkFramebuffer _offscreen_framebuffer;

		//Commands
		VkCommandPool _forwardCommandPool;
		VkCommandPool _deferredCommandPool;

		VkCommandBuffer _deferredCommandBuffer;
		FrameData _frames[FRAME_OVERLAP];
		VkSemaphore _offscreenSemaphore;

		VkSampler _defaultSampler;

		//init renderer structures and attachments
		void init_renderer();

		void create_depth_buffer();

		void create_deferred_attachments();

		void init_commands();

		void init_framebuffers();

		void init_sync_structures();

		void init_default_render_pass();

		void init_deferred_render_pass();

		void record_forward_command_buffers();

		void record_deferred_command_buffers();

		//draw functions
		void render_forward();

		void render_deferred();
	};
}

