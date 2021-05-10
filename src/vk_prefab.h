#pragma once

#include "vk_types.h"
#include <map>
#include <string>
#include <cassert>

struct rtVertex;
struct Vertex;
struct BlasInput;
struct PrimitiveToShader;

namespace VKE
{
	class Mesh;

	class Node
	{
	public:
		std::string _name;
		bool _visible;
		bool _opaque; // changes the mask of the tlas instance

		VKE::Mesh* _mesh;
		glm::mat4 _model;
		glm::mat4 _global_model;

		//info to create the tree
		Node* _parent;
		std::vector<Node*> _children;

		Node();

		virtual ~Node();

		void draw(glm::mat4& model, VkCommandBuffer commandBuffer, VkPipelineLayout layout);

		//add node to children list
		void add_child(Node* child);
		glm::mat4 get_global_matrix(bool fast = false);

		void node_to_vulkan_geometry(VkDeviceOrHostAddressConstKHR& vertexBufferDeviceAddress, 
			VkDeviceOrHostAddressConstKHR& indexBufferDeviceAddress, std::vector<BlasInput>& inputVector);
		void node_to_TLAS_instance(const glm::mat4& prefabModel, std::vector<AccelerationStructure>& bottomLevelAS, std::vector<VkAccelerationStructureInstanceKHR>& instances);
		void get_primitive_to_shader_info(const glm::mat4& model,
			std::vector<PrimitiveToShader>& primitivesInfo, std::vector<glm::mat4>& transforms, const int renderableIndex);
		void get_nodes_transforms(const glm::mat4& model, std::vector<glm::mat4>& transforms);
	};

	class Prefab
	{
	public:

		std::string _name;
		std::map<std::string, Node*> _nodes_by_name;

		std::vector<rtVertex> _rtvs;

		struct Vertices {
			int count;
			AllocatedBuffer vertexBuffer;
			AllocatedBuffer rtvBuffer;
			std::vector<Vertex> vertices;
			std::vector<rtVertex> rtVertices;
		} _vertices;

		struct Indices {
			int count;
			AllocatedBuffer indexBuffer;
		} _indices;

		std::vector<Node*> _roots;

		Prefab();
		Prefab(Mesh& mesh, const std::string& materialName = "default");
		virtual ~Prefab();

		void draw(glm::mat4& model, VkCommandBuffer commandBuffer, VkPipelineLayout layout);

		//Manager to cache loaded prefabs
		static std::map<std::string, Prefab*> sPrefabsLoaded;
		static Prefab* get(const char* filename);
		void register_prefab(const char* name);
	};
}


