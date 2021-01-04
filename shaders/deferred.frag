//glsl version 4.5
#version 450

//shader input
layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 texCoord;
layout (location = 3) in vec3 randomColor;

//output write
layout (location = 0) out vec4 outPosition;
layout (location = 1) out vec4 outNormal;
layout (location = 2) out vec4 outFragColor;

layout(set = 0, binding = 1) uniform  SceneData{   
    vec4 fogColor; // w is for exponent
	vec4 fogDistances; //x for min, y for max, zw unused.
	vec4 ambientColor;
	vec4 sunlightDirection; //w for sun power
	vec4 sunlightColor;
} sceneData;

layout(set = 2, binding = 0) uniform sampler2D tex1;

void main() 
{	
	vec3 color = texture(tex1, texCoord).xyz;
	outPosition = vec4(inPosition, 1.0f);
	outNormal = vec4(inNormal, 1.0f);
	outFragColor = vec4(color, 1.0f);
	//outFragColor = vec4(randomColor, 1.0f);
}