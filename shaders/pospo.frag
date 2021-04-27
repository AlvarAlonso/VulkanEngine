//glsl version 4.5
#version 450

precision highp int;

//texture input
layout(set = 0, binding = 0) uniform sampler2D inputImage;
layout(set = 1, binding = 0, rgba32ui) uniform uimage2D deepShadowMap;

layout (location = 0) in vec2 uv;

//output write
layout (location = 0) out vec4 outFragColor;

layout( push_constant, std140 ) uniform ShaderFlags {
	ivec4 flags;
} shaderFlags;

//float near = 0.1;
//float far = 3000.0;

float near = -500.0;
float far = 500.0;

const uint MAX_UINT = 0xFFFFFFFF;
const uint MAX_24UINT = 0x00FFFFFF;
const uint MAX_8UINT = 0x000000FF;

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
	if(shaderFlags.flags.x == 0)
	{
		vec3 color = texture(inputImage, uv).xyz;
		//color = vec3(linearizeDepth(color.x) / far);
		outFragColor = vec4(color, 1.0);
	}
	else
	{
		uint dsmiValue = 0;

		if(shaderFlags.flags.z == 0)
		{
			dsmiValue = imageLoad(deepShadowMap, ivec2(gl_FragCoord.xy)).x;
		}
		else if(shaderFlags.flags.z == 1)
		{
			dsmiValue = imageLoad(deepShadowMap, ivec2(gl_FragCoord.xy)).y;
		}
		else if(shaderFlags.flags.z == 2)
		{
			dsmiValue = imageLoad(deepShadowMap, ivec2(gl_FragCoord.xy)).z;
		}
		else
		{
			dsmiValue = imageLoad(deepShadowMap, ivec2(gl_FragCoord.xy)).w;
		}

		if(shaderFlags.flags.y == 1)
		{
			dsmiValue = dsmiValue >> 8;
			double dsmValue = double(dsmiValue) / double(MAX_24UINT);
			outFragColor = vec4(vec3(float(dsmValue)), 1.0);
		}
		else
		{
			uint iopacity = dsmiValue & 0x000000FF;
			float opacity = float(iopacity) / float(MAX_8UINT);
			outFragColor = vec4(vec3(float(opacity)), 1.0);
		}
	}
	
	imageStore(deepShadowMap, ivec2(gl_FragCoord.xy), uvec4(MAX_UINT, 0, 0, 0));
}