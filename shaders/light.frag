//glsl version 4.5
#version 450

//gbuffers input
layout(set = 1, binding = 0) uniform sampler2D position;
layout(set = 1, binding = 1) uniform sampler2D normal;
layout(set = 1, binding = 2) uniform sampler2D albedo;

//output write
layout (location = 0) out vec4 outFragColor;

layout(set = 0, binding = 1) uniform  SceneData{   
    vec4 fogColor; // w is for exponent
	vec4 fogDistances; //x for min, y for max, zw unused.
	vec4 ambientColor;
	vec4 sunlightDirection; //w for sun power
	vec4 sunlightColor;
} sceneData;

layout (location = 0) in vec2 uv;

void main() 
{	
	vec3 color = texture(albedo, uv).xyz;
	outFragColor = vec4(color, 1.0f);
	//outFragColor = vec4(1.0f, 0.0f, 0.0f, 1.0f);
}