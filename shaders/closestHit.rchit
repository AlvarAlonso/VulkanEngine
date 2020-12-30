#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec3 attribs;

struct Vertex {
	vec4 position;
	vec4 normal;
	vec4 uv;
};

layout(binding = 3, set = 0, scalar) buffer Vertices {
	Vertex v[]; 
} vertices[];

layout(binding = 4, set = 0) buffer Indices { uint i[]; } indices[];
layout(binding = 5, set = 0) buffer Transforms { mat4 t; } transforms[];

void main()
{
	//indices[gl_InstanceCustomIndexEXT].i[gl_PrimitiveID];

	const vec3 barycentrics = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);

	// Vertex of the triangle
	uint i0 = indices[gl_InstanceCustomIndexEXT].i[3 * gl_PrimitiveID + 0];
	uint i1 = indices[gl_InstanceCustomIndexEXT].i[3 * gl_PrimitiveID + 1];
	uint i2 = indices[gl_InstanceCustomIndexEXT].i[3 * gl_PrimitiveID + 2];

	Vertex v0 = vertices[gl_InstanceCustomIndexEXT].v[i0];
	Vertex v1 = vertices[gl_InstanceCustomIndexEXT].v[i1];
	Vertex v2 = vertices[gl_InstanceCustomIndexEXT].v[i2];

	// Computing the normal at hit position
	vec3 normal = vec3(v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z);
	// Transforming the normal to world space
	normal = normalize(vec3(transforms[gl_InstanceCustomIndexEXT].t * vec4(normal, 0.0)));

	hitValue = normal;
}