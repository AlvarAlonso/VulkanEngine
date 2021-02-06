#include "vk_prefab.h"
#include "vk_entity.h"
#include "vk_mesh.h"
#include "vk_material.h"
#include "vk_gltf_loader.h"
#include <glm/gtx/transform.hpp>

using namespace VKE;

std::map<std::string, Prefab*> Prefab::sPrefabsLoaded;

Node::Node() : _parent(nullptr), _mesh(nullptr), _visible(true)
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

Prefab::Prefab()
{
}

Prefab::Prefab(Mesh& mesh)
{
    _vertices.count = mesh._vertices.size();
    _vertices.vertexBuffer = mesh._vertexBuffer;
    _indices.count = mesh._indices.size();
    _indices.indexBuffer = mesh._indexBuffer;

    Primitive* primitive = new Primitive(0, _indices.count, _vertices.count, *VKE::Material::sMaterials["default"]);

    Node* node = new Node();
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

    std::string name = filename;
    prefab->register_prefab(name);
    return prefab;
}

void Prefab::register_prefab(std::string name)
{
    _name = name;
    sPrefabsLoaded[name] = this;
}