#pragma once

#include "vk_types.h"
#include"vk_mesh.h"

struct RenderObject;

namespace GRAPHICS
{
	constexpr unsigned int FRAME_OVERLAP = 2;

	enum RenderMode {
		RENDER_MODE_FORWARD = 0,
		RENDER_MODE_DEFERRED
	};

	struct sFrameData {
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

		void init_renderer();

		void draw_scene();

		//Render passes
		VkRenderPass _defaultRenderPass;
		VkRenderPass _deferredRenderPass;

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

		//Framebuffers
		std::vector<VkFramebuffer> _framebuffers;
		VkFramebuffer _offscreen_framebuffer;

		//Commands
		VkCommandPool _forwardCommandPool;
		VkCommandPool _deferredCommandPool;

		VkCommandBuffer _deferredCommandBuffer;
		sFrameData _frames[FRAME_OVERLAP];
		VkSemaphore _offscreenSemaphore;

		VkSampler _defaultSampler;

	private:

		int _frameNumber{ 0 };

		bool isDeferredCommandInit = false;

		//Queues
		VkQueue _graphicsQueue;
		uint32_t _graphicsQueueFamily;

		//init renderer structures and attachments

		void create_depth_buffer();

		void create_deferred_attachments();

		void init_commands();

		void init_framebuffers();

		void init_sync_structures();

		void init_default_render_pass();

		void init_deferred_render_pass();

		void record_forward_command_buffers();

		void record_deferred_command_buffers(RenderObject* first, int count);

		//support functions
		int get_current_frame_index();
		
		//draw functions
		void render_forward();

		void render_deferred();

		void draw_forward(VkCommandBuffer cmd, RenderObject* first, int count);

		void draw_deferred(VkCommandBuffer cmd, int imageIndex);		
	};
}

