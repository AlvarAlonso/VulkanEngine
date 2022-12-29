#include "vk_textures.h"
#include <iostream>

#include "vk_initializers.h"
#include "vk_utils.h"

#include <stb_image.h>
#include "vk_render_engine.h"
#include <cassert>

using namespace VKE;

int VKE::Texture::textureCount = 0;
std::map<std::string, VKE::Texture*> VKE::Texture::sTexturesLoaded;

bool vkutil::load_image_from_file(const std::string* file, AllocatedImage& outImage)
{
    int texWidth, texHeight, texChannels;

    stbi_uc* pixels = stbi_load(file->c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    if(!pixels) {
        std::cout << "Failed to load texture file" << file << std::endl;
        return false;
    }

    void* pixel_ptr = pixels;
    VkDeviceSize imageSize = texWidth * texHeight * 4;

    VkFormat image_format = VK_FORMAT_R8G8B8A8_UNORM;

    AllocatedBuffer stagingBuffer = vkutil::create_buffer(RenderEngine::_allocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);


    void* data;
    vmaMapMemory(RenderEngine::_allocator, stagingBuffer._allocation, &data);
    memcpy(data, pixel_ptr, static_cast<size_t>(imageSize));
    vmaUnmapMemory(RenderEngine::_allocator, stagingBuffer._allocation);

    stbi_image_free(pixels);

    VkExtent3D imageExtent;
    imageExtent.width = static_cast<uint32_t>(texWidth);
    imageExtent.height = static_cast<uint32_t>(texHeight);
    imageExtent.depth = 1;

    VkImageCreateInfo dimg_info = vkinit::image_create_info(image_format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, imageExtent);

    AllocatedImage newImage;

    VmaAllocationCreateInfo dimg_allocinfo = {};
    dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vmaCreateImage(RenderEngine::_allocator, &dimg_info, &dimg_allocinfo, &newImage._image, &newImage._allocation, nullptr);

    vkupload::immediate_submit([&](VkCommandBuffer cmd) {
        VkImageSubresourceRange range;
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        VkImageMemoryBarrier imageBarrier_toTransfer = {};
        imageBarrier_toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageBarrier_toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarrier_toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageBarrier_toTransfer.image = newImage._image;
        imageBarrier_toTransfer.subresourceRange = range;
        imageBarrier_toTransfer.srcAccessMask = 0;
        imageBarrier_toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &imageBarrier_toTransfer);

        VkBufferImageCopy copyRegion = {};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = imageExtent;

        vkCmdCopyBufferToImage(cmd, stagingBuffer._buffer, newImage._image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    
        
        VkImageMemoryBarrier imageBarrier_toReadable = imageBarrier_toTransfer;
        imageBarrier_toReadable.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageBarrier_toReadable.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageBarrier_toReadable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageBarrier_toReadable.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &imageBarrier_toReadable);
    });

    RenderEngine::_mainDeletionQueue.push_function([=]() {
        vmaDestroyImage(RenderEngine::_allocator, newImage._image, newImage._allocation);
    });

    vmaDestroyBuffer(RenderEngine::_allocator, stagingBuffer._buffer, stagingBuffer._allocation);

    outImage = newImage;

    return true;
}

bool vkutil::load_image_from_file(const std::string* file, int& width, int& height, void** data)
{
    int texChannels;

    stbi_uc* pixels = stbi_load(file->c_str(), &width, &height, &texChannels, STBI_rgb_alpha);

    if (!pixels) {
        std::cout << "Failed to load texture file" << file << std::endl;
        return false;
    }

    void* pixel_ptr = (void*)pixels;
    *data = pixel_ptr;

    return true;
}

bool vkutil::load_cubemap(const std::string* filename, VkFormat format, AllocatedImage& outImage, VkImageView& outImageView)
{
    void* textureData[6];

    int width{ 0 };
    int height{ 0 };

    const std::string baseName = *filename;

    // Load 6 faces separately and copy them into a staging buffer
    load_image_from_file(&std::string(baseName + std::string("_ft.jpg")), width, height, &textureData[0]);
    load_image_from_file(&std::string(baseName + std::string("_bk.jpg")), width, height, &textureData[1]);
    load_image_from_file(&std::string(baseName + std::string("_up.jpg")), width, height, &textureData[2]);
    load_image_from_file(&std::string(baseName + std::string("_dn.jpg")), width, height, &textureData[3]);
    load_image_from_file(&std::string(baseName + std::string("_rt.jpg")), width, height, &textureData[4]);
    load_image_from_file(&std::string(baseName + std::string("_lf.jpg")), width, height, &textureData[5]);

    const VkDeviceSize imageSize = width * height * 4 * 6.0f;
    const VkDeviceSize layerSize = imageSize / 6.0f;

    AllocatedBuffer stagingBuffer = vkutil::create_buffer(RenderEngine::_allocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    
    void* data;
    vmaMapMemory(RenderEngine::_allocator, stagingBuffer._allocation, &data);

    for(int i = 0; i < 6; i++)
    {
        memcpy((void*)((char*)data + layerSize * i), textureData[i], static_cast<size_t>(layerSize));
    }

    vmaUnmapMemory(RenderEngine::_allocator, stagingBuffer._allocation);

    for(int i = 0; i < 6; i++)
    {
        stbi_image_free(textureData[i]);
    }
    
    VkExtent3D imageExtent
    {
        width,
        height,
        1
    };

    // Create the cube image
    VkImageCreateInfo cube_img_info = vkinit::image_create_info(format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, imageExtent );
    cube_img_info.arrayLayers = 6;
    cube_img_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    AllocatedImage newImage;

    VmaAllocationCreateInfo cube_img_allocinfo = {};
    cube_img_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vmaCreateImage(RenderEngine::_allocator, &cube_img_info, &cube_img_allocinfo, &newImage._image, &newImage._allocation, nullptr);

    VkImageView imageView;

    // Create cube image view
    VkImageViewCreateInfo view = vkinit::imageview_create_info(format, newImage._image, VK_IMAGE_ASPECT_COLOR_BIT);
    view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    view.subresourceRange.layerCount = 6;

    VK_CHECK(vkCreateImageView(RenderEngine::_device, &view, nullptr, &imageView));

    vkupload::immediate_submit([&](VkCommandBuffer cmd) {

        std::vector<VkBufferImageCopy> bufferCopyRegions;

        for(uint32_t face = 0; face < 6; face++)
        {
            // Calculate offset for current face
            VkBufferImageCopy bufferCopyRegion = {};
            bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            bufferCopyRegion.imageSubresource.mipLevel = 0;
            bufferCopyRegion.imageSubresource.baseArrayLayer = face;
            bufferCopyRegion.imageSubresource.layerCount = 1;
            bufferCopyRegion.imageExtent = imageExtent;
            bufferCopyRegion.bufferOffset = layerSize * face;
            bufferCopyRegions.push_back(bufferCopyRegion);
        }

        // Image barrier
        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.layerCount = 6;

        VkImageMemoryBarrier imageBarrier_toTransfer = {};
        imageBarrier_toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageBarrier_toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarrier_toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageBarrier_toTransfer.image = newImage._image;
        imageBarrier_toTransfer.subresourceRange = subresourceRange;
        imageBarrier_toTransfer.srcAccessMask = 0;
        imageBarrier_toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &imageBarrier_toTransfer);

        vkCmdCopyBufferToImage(cmd, stagingBuffer._buffer, newImage._image, 
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
            static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());

        VkImageMemoryBarrier imageBarrier_toReadable = imageBarrier_toTransfer;
        imageBarrier_toReadable.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageBarrier_toReadable.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageBarrier_toReadable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageBarrier_toReadable.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &imageBarrier_toReadable);

    });

    RenderEngine::_mainDeletionQueue.push_function([=]() {
        vkDestroyImageView(RenderEngine::_device, imageView, nullptr);
        vmaDestroyImage(RenderEngine::_allocator, newImage._image, newImage._allocation);
    });

    vmaDestroyBuffer(RenderEngine::_allocator, stagingBuffer._buffer, stagingBuffer._allocation);

    outImage = newImage;
    outImageView = imageView;

    return true;
}

VKE::Texture* VKE::Texture::get(const char* name)
{
    assert(name);
    std::map<std::string, VKE::Texture*>::iterator it = sTexturesLoaded.find(name);
    if (it != sTexturesLoaded.end())
        return it->second;
    return nullptr;
}

void VKE::Texture::register_texture(const char* name)
{
    _name = name;
    sTexturesLoaded[name] = this;
}
