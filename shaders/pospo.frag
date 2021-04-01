//glsl version 4.5
#version 450

//texture input
layout(set = 0, binding = 0) uniform sampler2D inputImage;
layout(set = 1, binding = 0) uniform sampler2D deepShadowMap;

layout (location = 0) in vec2 uv;

//output write
layout (location = 0) out vec4 outFragColor;

layout( push_constant, std140 ) uniform ShaderFlags {
	int renderMode;
} shaderFlags;

void main() 
{	
	if(shaderFlags.renderMode == 0)
	{
		vec3 color = texture(inputImage, uv).xyz;
		outFragColor = vec4(color, 1.0f);
	}
	else
	{
		vec3 dsmValue = texture(deepShadowMap, uv).xyz;
		outFragColor = vec4(dsmValue, 1.0f);
	}
}