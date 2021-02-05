#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define STBI_MSC_SECURE_CRT
#include "tiny_gltf.h"
#include "vk_prefab.h"
#include "vk_material.h"
#include "vk_mesh.h"
#include "vk_types.h"
#include "vk_utils.h"
#include "vk_render_engine.h"
#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_textures.h"
#include <cassert>
#include "vk_gltf_loader.h"
#include "vk_prefab.h"

struct sgltfData 
{
    std::vector<VKE::Node*> nodes;
    std::vector<VKE::Texture*> textures;
    std::vector<VKE::Material*> materials;
} loadedData;

void update_texture_id(int* id)
{
    if (*id > VKE::Texture::sTexturesLoaded.size()) { return; }

    *id = VKE::Texture::sTexturesLoaded.size();
}

void update_material_id(int* id)
{
    if (*id > VKE::Material::sMaterials.size()) { return; }

    *id = VKE::Material::sMaterials.size();
}

void texture_from_glTF_image(tinygltf::Image& gltfimage)
{
    VKE::Texture* texture = new VKE::Texture();

	unsigned char* buffer = nullptr;
	VkDeviceSize bufferSize = 0;
	bool deleteBuffer = false;

	// Convert to rgba if it is rgb
	if (gltfimage.component == 3)
	{
		bufferSize = gltfimage.width * gltfimage.height * 4;
		buffer = new unsigned char[bufferSize];
		unsigned char* rgba = buffer;
		unsigned char* rgb = &gltfimage.image[0];
		for (int32_t i = 0; i < gltfimage.width * gltfimage.height; ++i)
		{
			for (int32_t j = 0; j < 3; ++j)
			{
				rgba[j] = rgb[j];
			}
			rgba += 4;
			rgb += 3;
		}
		deleteBuffer = true;
	}
	else
	{
		buffer = &gltfimage.image[0];
		bufferSize = gltfimage.image.size();
	}

	VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

	AllocatedBuffer stagingBuffer = vkutil::create_buffer(RenderEngine::_allocator, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
	
	void* data;
	vmaMapMemory(RenderEngine::_allocator, stagingBuffer._allocation, &data);
	memcpy(data, buffer, bufferSize);
	vmaUnmapMemory(RenderEngine::_allocator, stagingBuffer._allocation);

	VkExtent3D imageExtent;
	imageExtent.width = gltfimage.width;
	imageExtent.height = gltfimage.height;
	imageExtent.depth = 1;

	VkImageCreateInfo image_create_info = vkinit::image_create_info(format, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, imageExtent);

	VmaAllocationCreateInfo image_alloc_info = {};
	image_alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	vmaCreateImage(RenderEngine::_allocator, &image_create_info, &image_alloc_info, &texture->_image._image, &texture->_image._allocation, nullptr);

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
        imageBarrier_toTransfer.image = texture->_image._image;
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

        vkCmdCopyBufferToImage(cmd, stagingBuffer._buffer, texture->_image._image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);


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

    VkImageViewCreateInfo image_view_info = vkinit::imageview_create_info(format, texture->_image._image, VK_IMAGE_ASPECT_COLOR_BIT);
    vkCreateImageView(RenderEngine::_device, &image_view_info, nullptr, &texture->_imageView);

    texture->_name = gltfimage.name;
    loadedData.textures.push_back(texture);

    RenderEngine::_mainDeletionQueue.push_function([=]() {
        vkDestroyImageView(RenderEngine::_device, texture->_imageView, nullptr);
        vmaDestroyImage(RenderEngine::_allocator, texture->_image._image, texture->_image._allocation);
        });

    vmaDestroyBuffer(RenderEngine::_allocator, stagingBuffer._buffer, stagingBuffer._allocation);
}

void load_node(VKE::Node *parent, const tinygltf::Node &node, uint32_t nodeIndex, const tinygltf::Model &model, 
    std::vector<uint32_t>& indexBuffer, std::vector<Vertex>& vertexBuffer, float globalScale)
{
    VKE::Node* newNode = new VKE::Node();
    newNode->_parent = parent;
    newNode->_name = node.name;
    newNode->_model = glm::mat4(1.0f);

    // Generate local node matrix
    glm::vec3 translation = glm::vec3(0.0f);
    if(node.translation.size() == 3) {
        translation = glm::make_vec3(node.translation.data());
        newNode->_translation;
    }
    glm::mat4 rotation = glm::mat4(1.0f);
    if(node.rotation.size() == 4) {
        glm::quat q = glm::make_quat(node.rotation.data());
        newNode->_rotation = glm::mat4(q);
    }
    glm::vec3 scale = glm::vec3(1.9f);
    if(node.scale.size() == 3) {
        scale = glm::make_vec3(node.scale.data());
        newNode->_scale = scale;
    }
    if(node.matrix.size() == 16) {
        newNode->_model = glm::make_mat4x4(node.matrix.data());
    }

    // Node with children
    if(node.children.size() > 0) {
        for(size_t i = 0; i < node.children.size(); i++) {
            load_node(newNode, model.nodes[node.children[i]], node.children[i], model, indexBuffer, vertexBuffer, globalScale);
        }
    }

    // Node contains mesh data
    if (node.mesh > -1) {
        const tinygltf::Mesh mesh = model.meshes[node.mesh];
        Mesh* newMesh = new Mesh();
        for(size_t j = 0; j < mesh.primitives.size(); j++) 
        {
            const tinygltf::Primitive& primitive = mesh.primitives[j];
            uint32_t indexStart = static_cast<uint32_t>(indexBuffer.size());
            uint32_t vertexStart = static_cast<uint32_t>(vertexBuffer.size());
            uint32_t indexCount = 0;
            uint32_t vertexCount = 0;
            glm::vec3 posMin{};
            glm::vec3 posMax{};
            bool hasIndices = primitive.indices > -1;
            // Vertices
            {
                const float* bufferPos = nullptr;
                const float* bufferNormals = nullptr;
                const float* bufferTexCoordSet0 = nullptr;

                int posByteStride;
                int normByteStride;
                int uv0ByteStride;

                // Position attribute is required
                assert(primitive.attributes.find("POSITION") != primitive.attributes.end());
                
                const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
                const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
                bufferPos = reinterpret_cast<const float*>(&model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]);
                posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2]);
                posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2]);
                vertexCount = static_cast<uint32_t>(posAccessor.count);
                posByteStride = posAccessor.ByteStride(posView) ? (posAccessor.ByteStride(posView) / sizeof(float)) : TINYGLTF_TYPE_VEC3 * 4;
            
                if(primitive.attributes.find("NORMAL") != primitive.attributes.end())
                {
                    const tinygltf::Accessor& normAccessor = model.accessors[primitive.attributes.find("NORMAL")->second];
                    const tinygltf::BufferView& normView = model.bufferViews[normAccessor.bufferView];
                    bufferNormals = reinterpret_cast<const float*>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
                    normByteStride = normAccessor.ByteStride(normView) ? (normAccessor.ByteStride(normView) / sizeof(float)) : TINYGLTF_TYPE_VEC3 * 4;
                }

                if(primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end())
                {
                    const tinygltf::Accessor& uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
                    const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
                    bufferTexCoordSet0 = reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
                    uv0ByteStride = uvAccessor.ByteStride(uvView) ? (uvAccessor.ByteStride(uvView) / sizeof(float)) : TINYGLTF_TYPE_VEC2 * 4;
                }

                for(size_t v = 0; v < posAccessor.count; v++)
                {
                    Vertex vert{};
                    vert.position = glm::vec4(glm::make_vec3(&bufferPos[v * posByteStride]), 1.0f);
                    vert.normal = glm::normalize(glm::vec3(bufferNormals ? glm::make_vec3(&bufferNormals[v * normByteStride]) : glm::vec3(0.0f)));
                    vert.uv = bufferTexCoordSet0 ? glm::make_vec2(&bufferTexCoordSet0[v * uv0ByteStride]) : glm::vec3(0.0f);

                    vertexBuffer.push_back(vert);
                }
            }

            // Indices
            if(hasIndices)
            {
                const tinygltf::Accessor& accessor = model.accessors[primitive.indices > -1 ? primitive.indices : 0];
                const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

                indexCount = static_cast<uint32_t>(accessor.count);
                const void* dataPtr = &(buffer.data[accessor.byteOffset + bufferView.byteOffset]);

                switch(accessor.componentType)
                {
                case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
                    const uint32_t* buf = static_cast<const uint32_t*>(dataPtr);
                    for(size_t index = 0; index < accessor.count; index++) {
                        indexBuffer.push_back(buf[index] + vertexStart);
                    }
                    break;
                }
                case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
                    const uint16_t* buf = static_cast<const uint16_t*>(dataPtr);
                    for(size_t index = 0; index < accessor.count; index++) {
                        indexBuffer.push_back(buf[index] + vertexStart);
                    }
                    break;
                }
                case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
                    const uint8_t* buf = static_cast<const uint8_t*>(dataPtr);
                    for(size_t index = 0; index < accessor.count; index++) {
                        indexBuffer.push_back(buf[index] + vertexStart);
                    }
                    break;
                }
                default:
                    std::cerr << "Index component type" << accessor.componentType << " not suported!" << std::endl;
                    return;
                }
            }

            Primitive* newPrimitive = new Primitive(indexStart, indexCount, vertexCount, primitive.material > -1 ? *loadedData.materials[primitive.material] : *loadedData.materials.back());
            newMesh->_primitives.push_back(newPrimitive);
        }

        newNode->_mesh = newMesh;
    }
    
    if(parent)
    {
        parent->_children.push_back(newNode);
    }
    else
    {
        loadedData.nodes.push_back(newNode);
    }
}

void load_textures(tinygltf::Model &gltfModel)
{
    for(tinygltf::Texture &tex : gltfModel.textures) 
    {
        tinygltf::Image image = gltfModel.images[tex.source];
        
        texture_from_glTF_image(image);
    }
}

void load_materials(tinygltf::Model &gltfModel)
{
    for(tinygltf::Material &mat : gltfModel.materials)
    {
        VKE::Material* material = new VKE::Material();
        if(mat.values.find("baseColorTexture") != mat.values.end()) {
            material->_color_texture = loadedData.textures[mat.values["baseColorTexture"].TextureIndex()];
        }
        if(mat.values.find("metallicRoughnessTexture") != mat.values.end()) {
            material->_metallic_roughness_texture = loadedData.textures[mat.values["metallicRoughnessTexture"].TextureIndex()];
        }
        if(mat.values.find("roughnessFactor") != mat.values.end()) {
            material->_roughness_factor = static_cast<float>(mat.values["roughnessFactor"].Factor());
        }
        if(mat.values.find("metallicFactor") != mat.values.end()) {
            material->_metallic_factor = static_cast<float>(mat.values["metallicFactor"].Factor());
        }
        if(mat.values.find("baseColorFactor") != mat.values.end()) {
            material->_color = glm::make_vec4(mat.values["baseColorFactor"].ColorFactor().data());
        }
        if(mat.additionalValues.find("normalTexture") != mat.additionalValues.end()) {
            material->_normal_texture = loadedData.textures[mat.additionalValues["normalTexture"].TextureIndex()];
        }
        if(mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end()) {
            material->_occlusion_texture = loadedData.textures[mat.additionalValues["occlusionTexture"].TextureIndex()];
        }
        if(mat.additionalValues.find("emissiveFactor") != mat.additionalValues.end()) {
            material->_emissive_factor = glm::vec4(glm::make_vec3(mat.additionalValues["emissiveFactor"].ColorFactor().data()), 1.0f);
            material->_emissive_factor = glm::vec4(0.0f);
        }

        material->_name = mat.name.c_str();
        loadedData.materials.push_back(material);
    }
    // TODO: Handle default material in the Engine
    //Push default material if no material is assigned
}

VKE::Prefab* load_glTF(std::string filename, float scale = 1.0f)
{
    VKE::Prefab* prefab = new VKE::Prefab();

    tinygltf::Model gltfModel;
    tinygltf::TinyGLTF gltfContext;
    std::string error;
    std::string warning;

    bool binary = false;
    size_t extpos = filename.rfind('.', filename.length());
    if(extpos != std::string::npos) {
        binary = (filename.substr(extpos + 1, filename.length() - extpos) == "glb");
    }

    bool fileLoaded = binary ? gltfContext.LoadBinaryFromFile(&gltfModel, &error, &warning, filename.c_str()) : gltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, filename.c_str());
    
    std::vector<uint32_t> indexBuffer;
    std::vector<Vertex> vertexBuffer;

    if(fileLoaded)
    {
        load_textures(gltfModel);
        load_materials(gltfModel);

        const tinygltf::Scene& scene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];
        for(size_t i = 0; i < scene.nodes.size(); i++) 
        {
            const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
            load_node(nullptr, node, scene.nodes[i], gltfModel, indexBuffer, vertexBuffer, scale);
        }

        // Update textures and materials IDs to match the ones they should have in the engine list
        for (VKE::Texture* texture : loadedData.textures)
        {
            update_texture_id(&texture->_id);
            texture->register_texture(texture->_name.c_str());
        }
        for(VKE::Material* material : loadedData.materials)
        {
            update_material_id(&material->_id);
            material->register_material(material->_name.c_str());
        }
        
        if(loadedData.nodes.size() == 0)
        {
            std::cout << "[ERROR] No nodes were found!" << std::endl;
            return nullptr;
        }
        else if(loadedData.nodes.size() > 1)
        {
            std::cout << "[ERROR] glTFs files with more than one root node are not supported!" << std::endl;
            return nullptr;
        }
            
        prefab->_root = *loadedData.nodes[0]; //TODO: Handle more than one root node

        // The pointers to the data are no longer needed
        loadedData.textures.clear();
        loadedData.materials.clear();
        loadedData.nodes.clear();
        
        size_t vertexBufferSize = vertexBuffer.size() * sizeof(Vertex);
        size_t indexBufferSize = indexBuffer.size() * sizeof(uint32_t);
        prefab->_vertices.count = static_cast<uint32_t>(vertexBuffer.size());
        prefab->_indices.count = static_cast<uint32_t>(indexBuffer.size());

        assert(vertexBufferSize > 0);

        AllocatedBuffer vertexStaging;
        AllocatedBuffer indexStaging;

        // Create staging buffers
        // Vertex data
        vertexStaging = vkutil::create_buffer(RenderEngine::_allocator, vertexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_ONLY);

        void* vertexData;
        vmaMapMemory(RenderEngine::_allocator, vertexStaging._allocation, &vertexData);
        memcpy(vertexData, vertexBuffer.data(), vertexBufferSize);
        vmaUnmapMemory(RenderEngine::_allocator, vertexStaging._allocation);

        // Create device local buffers
        // Vertex buffer
        prefab->_vertices.vertexBuffer = vkutil::create_buffer(RenderEngine::_allocator, vertexBufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

        if(indexBufferSize > 0)
        {
            // Index data
            indexStaging = vkutil::create_buffer(RenderEngine::_allocator, indexBufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_MEMORY_USAGE_CPU_ONLY);

            void* indexData;
            vmaMapMemory(RenderEngine::_allocator, indexStaging._allocation, &indexData);
            memcpy(indexData, indexBuffer.data(), indexBufferSize);
            vmaUnmapMemory(RenderEngine::_allocator, indexStaging._allocation);

            // Create device local buffers
            // Index buffer
            prefab->_indices.indexBuffer = vkutil::create_buffer(RenderEngine::_allocator, indexBufferSize,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        }

        vkupload::immediate_submit([=](VkCommandBuffer cmd)
            {
                VkBufferCopy copyRegion;
                copyRegion.dstOffset = 0;
                copyRegion.srcOffset = 0;
                copyRegion.size = vertexBufferSize;

                vkCmdCopyBuffer(cmd, vertexStaging._buffer, prefab->_vertices.vertexBuffer._buffer, 1, &copyRegion);
            
                if(indexBufferSize > 0)
                {
                    copyRegion.size = indexBufferSize;
                    vkCmdCopyBuffer(cmd, indexStaging._buffer, prefab->_indices.indexBuffer._buffer, 1, &copyRegion);
                }
            });

        RenderEngine::_mainDeletionQueue.push_function([=]() {
            vmaDestroyBuffer(RenderEngine::_allocator, prefab->_vertices.vertexBuffer._buffer, prefab->_vertices.vertexBuffer._allocation);
            if(indexBufferSize > 0)
            {
                vmaDestroyBuffer(RenderEngine::_allocator, prefab->_indices.indexBuffer._buffer, prefab->_indices.indexBuffer._allocation);
            }
            });

        vmaDestroyBuffer(RenderEngine::_allocator, vertexStaging._buffer, vertexStaging._allocation);
        if(indexBufferSize > 0)
        {
            vmaDestroyBuffer(RenderEngine::_allocator, indexStaging._buffer, indexStaging._allocation);
        }
  
        return prefab;
    }

    return nullptr;
}