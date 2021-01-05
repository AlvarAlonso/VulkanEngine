#include "vk_mesh.h"
#include "vk_utils.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <iostream>
#include "vk_render_engine.h"

VertexInputDescription Vertex::get_vertex_description()
{
    VertexInputDescription description;

    VkVertexInputBindingDescription mainBinding = {};
    mainBinding.binding = 0;
    mainBinding.stride = sizeof(Vertex);
    mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    description.bindings.push_back(mainBinding);

    VkVertexInputAttributeDescription positionAttribute = {};
    positionAttribute.binding = 0;
    positionAttribute.location = 0;
    positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    positionAttribute.offset = offsetof(Vertex, position);

    VkVertexInputAttributeDescription normalAttribute = {};
    normalAttribute.binding = 0;
    normalAttribute.location = 1;
    normalAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    normalAttribute.offset = offsetof(Vertex, normal);

    
    VkVertexInputAttributeDescription colorAttribute = {};
    colorAttribute.binding = 0;
    colorAttribute.location = 2;
    colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    colorAttribute.offset = offsetof(Vertex, color);
    
    VkVertexInputAttributeDescription uvAttribute = {};
    uvAttribute.binding = 0;
    uvAttribute.location = 3;
    uvAttribute.format = VK_FORMAT_R32G32_SFLOAT;
    uvAttribute.offset = offsetof(Vertex, uv);

    description.attributes.push_back(positionAttribute);
    description.attributes.push_back(normalAttribute);
    description.attributes.push_back(colorAttribute);
    description.attributes.push_back(uvAttribute);

    return description;
}

Mesh::Mesh()
{
}

Mesh::Mesh(const char* filename)
{
    load_from_obj(filename);
    upload_to_gpu();
}

void Mesh::upload_to_gpu()
{
    if(_vertices.size() > 0 && _indices.size() > 0)
    {
        create_vertex_buffer();
        create_index_buffer();
    }
    else
    {
        std::cout << "Failed tu upload mesh to the gpu!" << std::endl;
    }
}

bool Mesh::load_from_obj(const char* filename)
{
    tinyobj::attrib_t attrib;

    std::vector<tinyobj::shape_t> shapes;

    std::vector<tinyobj::material_t> materials;

    std::string warn;
    std::string err;

    tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename, nullptr);
    
    if(!warn.empty())
    {
        std::cout << "WARN: " << warn << std::endl;
    }

    if (!err.empty())
    {
        std::cerr << err << std::endl;
        return false;
    }

    std::unordered_map<Vertex, uint32_t> uniqueVertices{};

    for(const auto& shape : shapes)
    {
        for(const auto& index : shape.mesh.indices)
        {
            Vertex vertex{};

            vertex.position = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };

            vertex.normal = {
                attrib.normals[3 * index.normal_index + 0],
                attrib.normals[3 * index.normal_index + 1],
                attrib.normals[3 * index.normal_index + 2]
            };

            vertex.uv = {
                attrib.texcoords[2 * index.texcoord_index + 0],
                1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
            };

            vertex.color = { 1.0f, 1.0f, 1.0f };

            if(uniqueVertices.count(vertex) == 0) 
            {
                uniqueVertices[vertex] = static_cast<uint32_t>(_vertices.size());
                _vertices.push_back(vertex);
            }

            _indices.push_back(uniqueVertices[vertex]);
        }
    }

    return true;
}

void Mesh::create_vertex_buffer()
{
    const size_t bufferSize = _vertices.size() * sizeof(Vertex);

    VkBufferCreateInfo stagingBufferInfo = {};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.pNext = nullptr;
    stagingBufferInfo.size = bufferSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    AllocatedBuffer stagingBuffer;

    VK_CHECK(vmaCreateBuffer(RenderEngine::_allocator, &stagingBufferInfo, &vmaAllocInfo,
        &stagingBuffer._buffer,
        &stagingBuffer._allocation,
        nullptr));

    void* data;
    vmaMapMemory(RenderEngine::_allocator, stagingBuffer._allocation, &data);
    memcpy(data, _vertices.data(), _vertices.size() * sizeof(Vertex));
    vmaUnmapMemory(RenderEngine::_allocator, stagingBuffer._allocation);

    VkBufferCreateInfo vertexBufferInfo = {};
    vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertexBufferInfo.pNext = nullptr;
    vertexBufferInfo.size = bufferSize;
    vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VK_CHECK(vmaCreateBuffer(RenderEngine::_allocator, &vertexBufferInfo, &vmaAllocInfo,
        &_vertexBuffer._buffer,
        &_vertexBuffer._allocation,
        nullptr));

    vkupload::immediate_submit([=](VkCommandBuffer cmd)
        {
            VkBufferCopy copy;
            copy.dstOffset = 0;
            copy.srcOffset = 0;
            copy.size = bufferSize;

            vkCmdCopyBuffer(cmd, stagingBuffer._buffer, _vertexBuffer._buffer, 1, &copy);
        });
    
    RenderEngine::_mainDeletionQueue.push_function([=]() {
        vmaDestroyBuffer(RenderEngine::_allocator, _vertexBuffer._buffer, _vertexBuffer._allocation);
        });
        
    vmaDestroyBuffer(RenderEngine::_allocator, stagingBuffer._buffer, stagingBuffer._allocation);
}

void Mesh::create_index_buffer()
{
    const size_t bufferSize = _indices.size() * sizeof(uint32_t);

    VkBufferCreateInfo stagingBufferInfo = {};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.pNext = nullptr;
    stagingBufferInfo.size = bufferSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    AllocatedBuffer stagingBuffer;

    VK_CHECK(vmaCreateBuffer(RenderEngine::_allocator, &stagingBufferInfo, &vmaAllocInfo,
        &stagingBuffer._buffer,
        &stagingBuffer._allocation,
        nullptr));

    void* data;
    vmaMapMemory(RenderEngine::_allocator, stagingBuffer._allocation, &data);
    memcpy(data, _indices.data(), _indices.size() * sizeof(uint32_t));
    vmaUnmapMemory(RenderEngine::_allocator, stagingBuffer._allocation);

    VkBufferCreateInfo indexBufferInfo = {};
    indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indexBufferInfo.pNext = nullptr;
    indexBufferInfo.size = bufferSize;
    indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VK_CHECK(vmaCreateBuffer(RenderEngine::_allocator, &indexBufferInfo, &vmaAllocInfo,
        &_indexBuffer._buffer,
        &_indexBuffer._allocation,
        nullptr));

    vkupload::immediate_submit([=](VkCommandBuffer cmd)
        {
            VkBufferCopy copy;
            copy.dstOffset = 0;
            copy.srcOffset = 0;
            copy.size = bufferSize;

            vkCmdCopyBuffer(cmd, stagingBuffer._buffer, _indexBuffer._buffer, 1, &copy);
        });

    RenderEngine::_mainDeletionQueue.push_function([=]() {
            vmaDestroyBuffer(RenderEngine::_allocator, _indexBuffer._buffer, _indexBuffer._allocation);
        });

    vmaDestroyBuffer(RenderEngine::_allocator, stagingBuffer._buffer, stagingBuffer._allocation);
}

void Mesh::destroy_buffers()
{
    vmaDestroyBuffer(RenderEngine::_allocator, _indexBuffer._buffer, _indexBuffer._allocation);
    vmaDestroyBuffer(RenderEngine::_allocator, _vertexBuffer._buffer, _indexBuffer._allocation);
}

void Mesh::create_quad(int size)
{
    _vertices.clear();
    _indices.clear();
    destroy_buffers();

    //Quad vertices
    _vertices.resize(4);

    _vertices[0].position = { -size, -size, 0.0f };
    _vertices[1].position = { size, -size, 0.0f };
    _vertices[2].position = { size, size, 0.0f };
    _vertices[3].position = { -size, size, 0.0f };
    
    _vertices[0].normal = { 0.0f, 0.0f, 1.0f };
    _vertices[1].normal = { 0.0f, 0.0f, 1.0f };
    _vertices[2].normal = { 0.0f, 0.0f, 1.0f };
    _vertices[3].normal = { 0.0f, 0.0f, 1.0f };

    _vertices[0].color = { 1.0f, 1.0f, 1.0f };
    _vertices[1].color = { 1.0f, 1.0f, 1.0f };
    _vertices[2].color = { 1.0f, 1.0f, 1.0f };
    _vertices[3].color = { 1.0f, 1.0f, 1.0f };

    _vertices[0].uv = { 1.0f, 0.0f };
    _vertices[1].uv = { 0.0f, 0.0f };
    _vertices[2].uv = { 0.0f, 1.0f };
    _vertices[3].uv = { 1.0f, 1.0f };

    //Quad indices
    _indices = {0, 1, 2, 2, 3, 0};

    upload_to_gpu();
}
