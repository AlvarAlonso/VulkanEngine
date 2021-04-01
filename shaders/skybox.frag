#version 460

layout(set = 0, binding = 1) uniform samplerCube cubeMap;

layout(location = 0) in vec3 inUVW;

layout(location = 0) out vec4 outFragColor;

void main()
{
	outFragColor = texture(cubeMap, inUVW);
}