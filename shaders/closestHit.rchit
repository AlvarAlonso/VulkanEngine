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

struct Light {
	vec4 position_maxDist;
	vec4 color_intensity;
};

layout(binding = 3, set = 0, scalar) buffer Vertices {
	Vertex v[]; 
} vertices[];

layout(binding = 4, set = 0) buffer Indices { uint i[]; } indices[];
layout(binding = 5, set = 0) buffer Transforms { mat4 t; } transforms[];
layout(binding = 6, set = 0) uniform Lights { Light l[1];} lights;

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

	//hitValue = vec3(0.9f, 0.9f, 0.9f);

	  // Computing the coordinates of the hit position
  vec3 worldPos = vec3(v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z);
  // Transforming the position to world space
  worldPos = vec3(transforms[gl_InstanceCustomIndexEXT].t * vec4(worldPos, 1.0));

// Vector toward the light
  vec3  L;
  float lightIntensity = lights.l[0].color_intensity.w;
  float lightDistance  = lights.l[0].position_maxDist.w;

  // Point light
  vec3 lDir      = lights.l[0].position_maxDist.xyz - worldPos;
  lightDistance  = length(lDir);
  lightIntensity = lightIntensity / (lightDistance * lightDistance);
  L              = normalize(lDir);

  float dotNL = max(dot(normal, L), 0.2);

  hitValue = vec3(dotNL) * lights.l[0].color_intensity.xyz;
}