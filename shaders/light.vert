#version 460

vec3 positions[6] = vec3[](
    vec3(1.0, -1.0, 0.0),
	vec3(-1.0, 1.0, 0.0),
	vec3(1.0, 1.0, 0.0),
	vec3(-1.0, -1.0, 0.0),
	vec3(-1.0, 1.0, 0.0),
	vec3(1.0, -1.0, 0.0)
);

vec2 uvs[6] = vec2[](
	vec2(1.0, 0.0),
	vec2(0.0, 1.0),
	vec2(1.0, 1.0),
	vec2(0.0, 0.0),
	vec2(0.0, 1.0),
	vec2(1.0, 0.0)
);

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;
layout (location = 3) in vec2 vTexCoord;

layout(set = 0, binding = 0) uniform CameraBuffer {
	mat4 view;
	mat4 proj;
	mat4 viewproj;
} cameraData;

layout (location = 0) out vec2 v_uv;

void main() 
{	
	//v_uv = uvs[gl_VertexIndex];
	//gl_Position = vec4(positions[gl_VertexIndex], 1.0);

	v_uv = vTexCoord;
	gl_Position = vec4(vPosition, 1.0);
}