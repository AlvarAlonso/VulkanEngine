//glsl version 4.5
#version 450

//texture input
layout(set = 0, binding = 0) uniform sampler2D inputImage;
layout(set = 1, binding = 0, rgba32ui) uniform uimage2D deepShadowMap;

layout (location = 0) in vec2 uv;

//output write
layout (location = 0) out vec4 outFragColor;

layout( push_constant, std140 ) uniform ShaderFlags {
	bool showDeepShadowMap;
	int shadowMapLayer;
} shaderFlags;

//float near = 0.1;
//float far = 3000.0;

float near = -500.0;
float far = 500.0;

const uint MAX_UINT = 0xFFFFFFFF;

float linearizeDepth(float depth)
{
	float z = depth * 2.0 - 1.0;
	return (2.0 * near * far) / (far + near - z * (far - near));
}

double linearizeDepth(double depth)
{
	double z = depth * 2.0 - 1.0;
	return (2.0 * near * far) / (far + near - z * (far - near));
}

void main() 
{	
	if(!shaderFlags.showDeepShadowMap)
	{
		vec3 color = texture(inputImage, uv).xyz;
		//color = vec3(linearizeDepth(color.x) / far);
		outFragColor = vec4(color, 1.0);
	}
	else
	{
		if(shaderFlags.shadowMapLayer == 0)
		{
			uint dsmiValue = imageLoad(deepShadowMap, ivec2(gl_FragCoord.xy)).x;
			double dsmValue = double(dsmiValue) / double(MAX_UINT);
			outFragColor = vec4(vec3(float(dsmValue)), 1.0);
		}
		else
		{
			vec3 dsmValue = vec3(imageLoad(deepShadowMap, ivec2(gl_FragCoord.xy)).x);
			float depth = dsmValue.x / 16777216.0;

			outFragColor = vec4(vec3(dsmValue), 1.0);
		}
	}

	imageStore(deepShadowMap, ivec2(gl_FragCoord.xy), uvec4(MAX_UINT, 0, 1, 1));
}