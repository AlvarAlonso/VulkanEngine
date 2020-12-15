#pragma once

#include "vk_types.h"

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

		void init_deferred_attachments();

		void render_forward();

		void render_deferred();

		void init_commands();

		void record_forward_command_buffers();

		void record_deferred_command_buffers();

		void init_sync_structures();
	};
}

