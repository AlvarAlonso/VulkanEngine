#version 460

#extension GL_EXT_nonuniform_qualifier : enable

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
layout(set = 2, binding = 0, rgba32ui) uniform uimage2D deepShadowImage;

float near = -500.0;
float far = 0.1;

const uint MAX_UINT = 0xFFFFFFFF;

float linearizeDepth(float depth)
{
	float z = depth * 2.0 - 1.0;
	return (2.0 * near * far) / (far + near - z * (far - near));
}

void main()
{
	int matIndex = int(objectPushConstant.matIndex.x);
	Material material = materials.m[matIndex];

	int occlusionTextureIdx = int(material.emissive_metRough_occlusion_normal_indices.z);
	vec3 occlusionTexture = texture(textures[occlusionTextureIdx], texCoord).xyz;
	
	if(occlusionTextureIdx >= 0 && occlusionTexture.x < 0.001 && occlusionTexture.y < 0.001 && occlusionTexture.z < 0.001)
	{
		discard;
	}
	
	//float depth = gl_FragCoord.z;
	uint depth = uint(gl_FragCoord.z * MAX_UINT);

	uint currentMinDepth = imageLoad(deepShadowImage, ivec2(gl_FragCoord.xy)).x;
	uint currentMaxDepth = imageLoad(deepShadowImage, ivec2(gl_FragCoord.xy)).y;

	if(depth < currentMinDepth)
	{
		imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(depth, 1, 1, 1));
	}
	
	/*
	uint depth = 50;

	imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(depth, 1, 1, 1));
	*/
	/*
	if(depth < currentMinDepth)
	{
		imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(depth, currentMaxDepth, 1, 1));
	}

	if(depth < currentMinDepth && depth > currentMaxDepth)
	{
		imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(depth, depth, 1, 1));
		return;
	}
	else if(depth > currentMaxDepth)
	{
		imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(currentMinDepth, depth, 1, 1));
	}
	*/
}