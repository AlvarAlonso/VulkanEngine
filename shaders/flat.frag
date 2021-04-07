#version 460

layout (location = 0) in vec2 texCoord;

struct Material {
	vec4 color;
	vec4 emissive_factor;
	vec4 roughness_metallic_tilling_color_factors; // Color is the index to the color texture
	vec4 emissive_metRough_occlusion_normal_indices; // Indices to material textures
};

layout( push_constant, std140 ) uniform ObjectIndices {
	mat4 modelMatrix;
	vec4 matIndex; // currently using only x component to pass the primitive material index
} objectPushConstant;

layout(set = 1, binding = 0) buffer Materials { Material m[]; } materials;
layout(set = 1, binding = 1) uniform sampler2D textures[];
layout(set = 2, binding = 0, rgba32f) uniform image2D deepShadowImage;

float near = -1000.0;
float far = 500.0;

float linearizeDepth(float depth)
{
	float z = depth * 2.0 - 1.0;
	return (2.0 * near * far) / (far + near - z * (far - near));
}

void main()
{
	//float depth = linearizeDepth(gl_FragCoord.z) / far;
	float depth = gl_FragCoord.z;

	float currentMinDepth = imageLoad(deepShadowImage, ivec2(gl_FragCoord.xy)).x;
	float currentMaxDepth = imageLoad(deepShadowImage, ivec2(gl_FragCoord.xy)).y;
	
	if(depth < currentMinDepth && depth > currentMaxDepth)
	{
		imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), vec4(depth, depth, 1.0, 1.0));
		return;
	}

	if(depth < currentMinDepth)
	{
		imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), vec4(depth, currentMaxDepth, 1.0, 1.0));
	}
	else if(depth > currentMaxDepth)
	{
		imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), vec4(currentMinDepth, depth, 1.0, 1.0));
	}
}