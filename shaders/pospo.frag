//glsl version 4.5
#version 450

//texture input
layout(set = 0, binding = 0) uniform sampler2D inputImage;
layout(set = 1, binding = 0, rgba32f) uniform image2D deepShadowMap;

layout (location = 0) in vec2 uv;

//output write
layout (location = 0) out vec4 outFragColor;

layout( push_constant, std140 ) uniform ShaderFlags {
	bool showDeepShadowMap;
	int shadowMapLayer;
} shaderFlags;

void main() 
{	
	if(!shaderFlags.showDeepShadowMap)
	{
		vec3 color = texture(inputImage, uv).xyz;
		outFragColor = vec4(color, 1.0f);
	}
	else
	{
		if(shaderFlags.shadowMapLayer == 0)
		{
			vec3 dsmValue = vec3(imageLoad(deepShadowMap, ivec2(gl_FragCoord.xy)).x);
			outFragColor = vec4(dsmValue, 1.0f);
		}
		else
		{
			vec3 dsmValue = vec3(imageLoad(deepShadowMap, ivec2(gl_FragCoord.xy)).y);
			outFragColor = vec4(dsmValue, 1.0f);
		}

		imageStore(deepShadowMap, ivec2(gl_FragCoord.xy), vec4(1.0, 0.0, 1.0, 1.0));
	}
}