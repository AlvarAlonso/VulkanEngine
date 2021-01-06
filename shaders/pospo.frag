//glsl version 4.5
#version 450

//texture input
layout(set = 0, binding = 0) uniform sampler2D inputImage;

layout (location = 0) in vec2 uv;

//output write
layout (location = 0) out vec4 outFragColor;

void main() 
{	
	vec3 color = texture(inputImage, uv).xyz;
	outFragColor = vec4(color, 1.0f);
}