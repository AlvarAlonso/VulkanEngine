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
		Material* _material;
		glm::mat4 _model;
		glm::mat4 _global_model;

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

		Node _root;

		virtual ~Prefab();

		//Manager to cache loaded prefabs
		static std::map<std::string, Prefab*> sPrefabsLoaded;
		static Prefab* get(const char* filename);
		void register_prefab(std::string name);
	};
}


