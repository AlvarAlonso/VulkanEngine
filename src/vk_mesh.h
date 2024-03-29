#pragma once

#include <vk_types.h>
#include <vector>
#include <map>

// forward declarations
struct BlasInput;

namespace VKE
{
	struct Material;
}

struct VertexInputDescription {
	std::vector<VkVertexInputBindingDescription> bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;

	VkPipelineVertexInputStateCreateFlags flags = 0;
};


struct Vertex {

	glm::vec3 position;
	glm::vec3 normal;
	glm::vec3 color;
	glm::vec2 uv;

	static VertexInputDescription get_vertex_description(bool onlyPosition = false);
	
	bool operator==(const Vertex& other) const {
		return position == other.position && normal == other.normal && uv == other.uv;
	}
};

struct rtVertex {
	glm::vec4 position;
	glm::vec4 normal;
	glm::vec4 uv;

	bool operator==(const rtVertex& other) const {
		return position == other.position && normal == other.normal && uv == other.uv;
	}
};

namespace std {
	template<> struct hash<Vertex> {
		size_t operator()(Vertex const& vertex) const {
			return ((hash<glm::vec3>()(vertex.position) ^
				(hash<glm::vec3>()(vertex.normal) << 1)) >> 1) ^
				(hash<glm::vec2>()(vertex.uv) << 1);
		}
	};
}

struct Primitive {
	uint32_t firstVertex;
	uint32_t firstIndex;
	uint32_t indexCount;
	uint32_t vertexCount;
	VKE::Material& material;
	bool hasIndices;

	Primitive(uint32_t firstVertex, uint32_t firstIndex, uint32_t indexCount, uint32_t vertexCount, VKE::Material& material) : firstIndex(firstIndex), indexCount(indexCount), vertexCount(vertexCount), material(material) {
		hasIndices = indexCount > 0;
	};

	void primitive_to_vulkan_geometry(VkDeviceOrHostAddressConstKHR& vertexBufferDeviceAddress, VkDeviceOrHostAddressConstKHR& indexBufferDeviceAddress, std::vector<BlasInput>& input);

	void draw(glm::mat4& model, VkCommandBuffer commandBuffer, VkPipelineLayout layout);
};

struct PrimitiveToShader {
	glm::vec4 firstIdx_rndIdx_matIdx_transIdx;
};

namespace VKE
{
	class Mesh
	{
	public:
		std::string _name;
		static std::map<std::string, Mesh*> sMeshesLoaded;
		Mesh();
		~Mesh();

		std::vector<Vertex> _vertices;
		std::vector<uint32_t> _indices;

		AllocatedBuffer _vertexBuffer;
		AllocatedBuffer _indexBuffer;

		std::vector<Primitive*> _primitives;

		void upload_to_gpu();

		void create_vertex_buffer();
		void create_index_buffer();

		void destroy_buffers();

		void create_quad();

		void create_cube();

		//loader
		static Mesh* get(const char* name);
		void register_mesh(const char* name);
	};
}

namespace vkutil
{
	bool load_meshes_from_obj(const std::string* filename, const std::string* customName);
}


