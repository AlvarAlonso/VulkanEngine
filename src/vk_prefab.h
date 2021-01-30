#pragma once

#include "vk_types.h"
#include <map>
#include <string>
#include <cassert>

class Mesh;
class Texture;
class Material;

namespace VKE
{
	class Node
	{
	public:
		std::string _name;
		bool _visible;

		Mesh* _mesh;
		glm::mat4 _model;
		glm::mat4 _global_model;
		glm::vec3 _translation;
		glm::mat4 _rotation;
		glm::vec3 _scale;

		//info to create the tree
		Node* _parent;
		std::vector<Node*> _children;

		Node();

		virtual ~Node();

		//add node to children list
		void add_child(Node* child);

		glm::mat4 get_global_matrix(bool fast = false);
	};

	class Prefab
	{
	public:

		std::string _name;
		std::map<std::string, Node*> _nodes_by_name;

		struct Vertices {
			int count;
			AllocatedBuffer vertexBuffer;
		} _vertices;

		struct Indices {
			int count;
			AllocatedBuffer indexBuffer;
		} _indices;

		Node _root;

		Prefab();
		Prefab(Mesh& mesh);
		virtual ~Prefab();

		//Manager to cache loaded prefabs
		static std::map<std::string, Prefab*> sPrefabsLoaded;
		static Prefab* get(const char* filename);
		void register_prefab(std::string name);
	};
}


