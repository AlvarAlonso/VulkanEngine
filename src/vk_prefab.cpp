#include "vk_prefab.h"
#include "vk_entity.h"
#include "vk_mesh.h"
#include "vk_material.h"
#include "vk_gltf_loader.h"
#include "vk_render_engine.h"
#include <glm/gtx/transform.hpp>
#include "vk_utils.h"
#include <string>

using namespace VKE;

std::map<std::string, Prefab*> Prefab::sPrefabsLoaded;

Node::Node() :_opaque(true), _parent(nullptr), _mesh(nullptr), _visible(true)
{

}

Node::~Node()
{
    assert(_parent == nullptr);

    //delete children
    for(int i = 0; i < _children.size(); i++)
    {
        _children[i]->_parent = nullptr;
        delete _children[i];
    }
}

void VKE::Node::draw(glm::mat4& model, VkCommandBuffer commandBuffer, VkPipelineLayout layout)
{
    if(_mesh != nullptr && _mesh->_primitives.size() > 0)
    {
        VKE::Material* lastMaterial = nullptr;
        GPUObjectData objectData{};
        for(const auto& primitive : _mesh->_primitives)
        {
            objectData.modelMatrix = model * get_global_matrix();

            if(&primitive->material != lastMaterial)
            {
                objectData.matIndex.x = primitive->material._id;
                lastMaterial = &primitive->material;
            }

            vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GPUObjectData), &objectData);
                
            if(primitive->hasIndices)
            {
                vkCmdDrawIndexed(commandBuffer, primitive->indexCount, 1, primitive->firstIndex, 0, 0);
            }
            else
            {
                vkCmdDraw(commandBuffer, primitive->vertexCount, 1, 0, 0);
            }
        }
    }

    for(const auto& child : _children)
    {
        child->draw(model, commandBuffer, layout);
    }
}

void Node::add_child(Node* child)
{
    assert(child->_parent == nullptr);
    
    _children.push_back(child);
    child->_parent = this;
}

glm::mat4 Node::get_global_matrix(bool fast)
{
    if (_parent)
        _global_model = _model * (fast ? _parent->_global_model : _parent->get_global_matrix());
    else
        _global_model = _model;
    return _global_model;
}

void VKE::Node::node_to_vulkan_geometry(VkDeviceOrHostAddressConstKHR& vertexBufferDeviceAddress, VkDeviceOrHostAddressConstKHR& indexBufferDeviceAddress, std::vector<BlasInput>& inputVector)
{
    if(_children.size() > 0)
    {
        for(const auto& child : _children)
        {
            child->node_to_vulkan_geometry(vertexBufferDeviceAddress, indexBufferDeviceAddress, inputVector);
        }
    }

    if(_mesh != nullptr && _mesh->_primitives.size() > 0)
    {
        for(const auto& primitive : _mesh->_primitives)
        {
            primitive->primitive_to_vulkan_geometry(vertexBufferDeviceAddress, indexBufferDeviceAddress, inputVector);
        }
    }
}

void VKE::Node::node_to_TLAS_instance(const glm::mat4& prefabModel, std::vector<AccelerationStructure>& bottomLevelAS, std::vector<VkAccelerationStructureInstanceKHR>& instances)
{
    if (_children.size() > 0)
    {
        for (const auto& child : _children)
        {
            child->node_to_TLAS_instance(prefabModel, bottomLevelAS, instances);
        }
    }

    if (_mesh != nullptr && _mesh->_primitives.size() > 0)
    {
        glm::mat4 model = prefabModel * get_global_matrix();
        model = glm::transpose(model);

        VkTransformMatrixKHR transformMatrix = {
            model[0].x, model[0].y, model[0].z, model[0].w,
            model[1].x, model[1].y, model[1].z, model[1].w,
            model[2].x, model[2].y, model[2].z, model[2].w,
        };

        for (int i = 0; i < _mesh->_primitives.size(); i++)
        {
            VkAccelerationStructureInstanceKHR instance{};
            instance.transform = transformMatrix;
            instance.instanceCustomIndex = instances.size();
            instance.mask = _opaque ? 0x02 : 0x01; //FD, FE masks
            instance.instanceShaderBindingTableRecordOffset = 0;
            instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            instance.accelerationStructureReference = bottomLevelAS[instances.size()]._deviceAddress;

            instances.push_back(instance);
        }
    }
}

void VKE::Node::get_primitive_to_shader_info(const glm::mat4& model, std::vector<PrimitiveToShader>& primitivesInfo, std::vector<glm::mat4>& transforms, const int renderableIndex)
{
    if (_children.size() > 0)
    {
        for (const auto& child : _children)
        {
            child->get_primitive_to_shader_info(model, primitivesInfo, transforms, renderableIndex);
        }
    }

    if (_mesh != nullptr && _mesh->_primitives.size() > 0)
    {
        glm::mat4 global_matrix = model * get_global_matrix();

        for (const auto& primitive : _mesh->_primitives)
        {
            PrimitiveToShader primitiveInfo{};
            primitiveInfo.firstIdx_rndIdx_matIdx_transIdx.x = primitive->firstIndex;
            primitiveInfo.firstIdx_rndIdx_matIdx_transIdx.y = renderableIndex;
            primitiveInfo.firstIdx_rndIdx_matIdx_transIdx.z = primitive->material._id;
            primitiveInfo.firstIdx_rndIdx_matIdx_transIdx.w = transforms.size();

            primitivesInfo.push_back(primitiveInfo);
        }

        transforms.emplace_back(global_matrix);
    }
}

void VKE::Node::get_nodes_transforms(const glm::mat4& model, std::vector<glm::mat4>& transforms)
{
    if (_children.size() > 0)
    {
        for (const auto& child : _children)
        {
            child->get_nodes_transforms(model, transforms);
        }
    }

    if (_mesh != nullptr)
    {
        if (_mesh->_primitives.size() > 0)
        {
            glm::mat4 globalMatrix = model * get_global_matrix();
            transforms.emplace_back(globalMatrix);
        }
    }
}

Prefab::Prefab()
{
}

Prefab::Prefab(Mesh& mesh, const std::string& materialName)
{
    _vertices.count = mesh._vertices.size();
    _vertices.vertexBuffer = mesh._vertexBuffer;
    _indices.count = mesh._indices.size();
    _indices.indexBuffer = mesh._indexBuffer;

    std::vector<rtVertex> rtVertices;
    rtVertices.reserve(_vertices.count);

    for(const auto& vertex : mesh._vertices)
    {
        rtVertex rtv;
        rtv.position = glm::vec4(vertex.position, 1.0f);
        rtv.normal = glm::vec4(vertex.normal, 1.0f);
        rtv.uv = glm::vec4(vertex.uv, 1.0f, 1.0f);

        rtVertices.push_back(rtv);
    }

    size_t rtVertexBufferSize = sizeof(rtVertex) * rtVertices.size();

    // RTVertex data
    AllocatedBuffer rtvStaging = vkutil::create_buffer(RenderEngine::_allocator, rtVertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY);

    void* rtVertexData;
    vmaMapMemory(RenderEngine::_allocator, rtvStaging._allocation, &rtVertexData);
    memcpy(rtVertexData, rtVertices.data(), rtVertexBufferSize);
    vmaUnmapMemory(RenderEngine::_allocator, rtvStaging._allocation);

    // RTVertex buffer
    _vertices.rtvBuffer = vkutil::create_buffer(RenderEngine::_allocator, rtVertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    vkupload::immediate_submit([=](VkCommandBuffer cmd)
        {
            VkBufferCopy copyRegion;
            copyRegion.dstOffset = 0;
            copyRegion.srcOffset = 0;
            copyRegion.size = rtVertexBufferSize;

            vkCmdCopyBuffer(cmd, rtvStaging._buffer, _vertices.rtvBuffer._buffer, 1, &copyRegion);
        });

    Primitive* primitive = new Primitive(0, 0, _indices.count, _vertices.count, *VKE::Material::sMaterials[materialName]);

    Node* node = new Node();
    node->_opaque = primitive->material._type == DIFFUSE ? true : false;
    node->_mesh = &mesh;
    node->_mesh->_primitives.push_back(primitive);
    node->_model = glm::translate(glm::vec3{ 0, 0, 0 });
    _roots.push_back(node);
}

Prefab::~Prefab()
{
    if(_name.size())
    {
        auto it = sPrefabsLoaded.find(_name);
        if (it != sPrefabsLoaded.end());
        sPrefabsLoaded.erase(it);
    }
}

void VKE::Prefab::draw(glm::mat4& model, VkCommandBuffer commandBuffer, VkPipelineLayout layout)
{
    for(const auto& node : _roots)
    {
        node->draw(model, commandBuffer, layout);
    }
}

Prefab* Prefab::get(const char* filename)
{
    assert(filename);
    std::map<std::string, Prefab*>::iterator it = sPrefabsLoaded.find(filename);
    if (it != sPrefabsLoaded.end())
        return it->second;
    
    Prefab* prefab = load_glTF(std::string(filename), 1.0f);
    if(!prefab)
    {
        std::cout << "[ERROR]: Prefab not found" << std::endl;
        return nullptr;
    }

    const char* name = filename;
    prefab->register_prefab(name);
    return prefab;
}

void Prefab::register_prefab(const char* name)
{
    _name = name;
    sPrefabsLoaded[name] = this;
}