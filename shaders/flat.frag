#version 460

#extension GL_EXT_nonuniform_qualifier : enable

precision highp int;

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
const uint MAX_24UINT = 0x00FFFFFF;
const uint MAX_8UINT = 0x000000FF;

const uint DEFAULT_VISIBILITY = 0x000000FF;
const uint DEFAULT_MATERIAL_OPACITY = 0x0000000F;

uint computeVisibilitySample(uint depth, uint visibility)
{
	uint V = depth;
	V = V << 8;
	V = V | visibility;
	
	return V;
}

uint computeNewVisibility(uint V, uint visibilityToAdd)
{
	uint visibility = (V & 0x000000FF) - visibilityToAdd;

	uint newV = V & 0xFFFFFF00;
	newV = V | visibility;

	return newV;
}

uvec2 computeMiddleSamples(uint a, uint b, uint c, uint d)
{
	uint firstInterval = (a & MAX_8UINT) - (b & MAX_8UINT);
	uint secondInterval = (b & MAX_8UINT) - (c & MAX_8UINT);
	uint thirdInterval = (c & MAX_8UINT) - (d & MAX_8UINT);

	//return uvec2(b, d);

	if(firstInterval > secondInterval && secondInterval > thirdInterval)
	{
		return uvec2(c, d);
	}
	else if(secondInterval > firstInterval && secondInterval > thirdInterval)
	{
		return uvec2(b, d);
	}
	else
	{
		return uvec2(b, c);
	}
}

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
	
	uint depth = uint(gl_FragCoord.z * MAX_24UINT);
	
	uvec4 currentDsmValues = imageLoad(deepShadowImage, ivec2(gl_FragCoord.xy));

	uint v = 0;

	// it is a min
	if(depth < (currentDsmValues.x >> 8))
	{
		v = computeVisibilitySample(depth, DEFAULT_VISIBILITY - DEFAULT_MATERIAL_OPACITY);

		if(currentDsmValues.x == MAX_UINT)
		{
			imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(v, currentDsmValues.y, currentDsmValues.z, currentDsmValues.w));
		}
		else if(currentDsmValues.w == 0)
		{
			currentDsmValues.w = computeNewVisibility(currentDsmValues.x, DEFAULT_MATERIAL_OPACITY);

			imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(v, currentDsmValues.y, currentDsmValues.z, currentDsmValues.w));
		}
		else if(currentDsmValues.y == 0)
		{
			currentDsmValues.w = computeNewVisibility(currentDsmValues.w, DEFAULT_MATERIAL_OPACITY);
			currentDsmValues.y = computeNewVisibility(currentDsmValues.x, DEFAULT_MATERIAL_OPACITY);

			imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(v, currentDsmValues.y, currentDsmValues.z, currentDsmValues.w));
		}
		else if(currentDsmValues.z == 0)
		{
			currentDsmValues.w = computeNewVisibility(currentDsmValues.w, DEFAULT_MATERIAL_OPACITY);
			currentDsmValues.z = computeNewVisibility(currentDsmValues.y, DEFAULT_MATERIAL_OPACITY);
			currentDsmValues.y = computeNewVisibility(currentDsmValues.x, DEFAULT_MATERIAL_OPACITY);

			imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(v, currentDsmValues.y, currentDsmValues.z, currentDsmValues.w));
		}
		else
		{
			currentDsmValues.w = computeNewVisibility(currentDsmValues.w, DEFAULT_MATERIAL_OPACITY);
			currentDsmValues.z = computeNewVisibility(currentDsmValues.z, DEFAULT_MATERIAL_OPACITY);
			currentDsmValues.y = computeNewVisibility(currentDsmValues.y, DEFAULT_MATERIAL_OPACITY);
			currentDsmValues.x = computeNewVisibility(currentDsmValues.x, DEFAULT_MATERIAL_OPACITY);

			currentDsmValues.yz = computeMiddleSamples(v, currentDsmValues.x, currentDsmValues.y, currentDsmValues.z);

			imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(v, currentDsmValues.y, currentDsmValues.z, currentDsmValues.w));
		}
	}
	// it is a max
	else if(depth > (currentDsmValues.w >> 8))
	{
		if(currentDsmValues.w == 0)
		{
			v = computeVisibilitySample(depth, (currentDsmValues.x & MAX_8UINT) - DEFAULT_MATERIAL_OPACITY);

			imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(currentDsmValues.x, currentDsmValues.y, currentDsmValues.z, v));
		}
		else if(currentDsmValues.y == 0)
		{
			v = computeVisibilitySample(depth, (currentDsmValues.w & MAX_8UINT) - DEFAULT_MATERIAL_OPACITY);

			currentDsmValues.y = currentDsmValues.w;

			imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(currentDsmValues.x, currentDsmValues.y, currentDsmValues.z, v));
		}
		else if(currentDsmValues.z == 0)
		{
			v = computeVisibilitySample(depth, (currentDsmValues.w & MAX_8UINT) - DEFAULT_MATERIAL_OPACITY);

			currentDsmValues.z = currentDsmValues.w;

			imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(currentDsmValues.x, currentDsmValues.y, currentDsmValues.z, v));
		}
		else
		{
			v = computeVisibilitySample(depth, (currentDsmValues.w & MAX_8UINT) - DEFAULT_MATERIAL_OPACITY);

			currentDsmValues.yz = computeMiddleSamples(currentDsmValues.x, currentDsmValues.y, currentDsmValues.z, currentDsmValues.w);

			imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(currentDsmValues.x, currentDsmValues.y, currentDsmValues.z, v));
		}
	}
	// it is in the middle
	else
	{
		if(currentDsmValues.y == 0)
		{
			v = computeVisibilitySample(depth, (currentDsmValues.x & MAX_8UINT) - DEFAULT_MATERIAL_OPACITY);

			currentDsmValues.w = computeNewVisibility(currentDsmValues.w, DEFAULT_MATERIAL_OPACITY);

			imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(currentDsmValues.x, v, currentDsmValues.z, currentDsmValues.w));
		}
		else if(currentDsmValues.z == 0)
		{
			if(depth < (currentDsmValues.y >> 8))
			{
				v = computeVisibilitySample(depth, (currentDsmValues.x & MAX_8UINT) - DEFAULT_MATERIAL_OPACITY);
				
				currentDsmValues.w = computeNewVisibility(currentDsmValues.w, DEFAULT_MATERIAL_OPACITY);
				currentDsmValues.z = computeNewVisibility(currentDsmValues.y, DEFAULT_MATERIAL_OPACITY);

				imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(currentDsmValues.x, v, currentDsmValues.z, currentDsmValues.w));
			}
			else
			{
				v = computeVisibilitySample(depth, (currentDsmValues.y & MAX_8UINT) - DEFAULT_MATERIAL_OPACITY);

				currentDsmValues.w = computeNewVisibility(currentDsmValues.w, DEFAULT_MATERIAL_OPACITY);

				imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(currentDsmValues.x, currentDsmValues.y, v, currentDsmValues.w));
			}
		}
		else
		{
			if(depth < (currentDsmValues.y >> 8))
			{
				v = computeVisibilitySample(depth, (currentDsmValues.x & MAX_8UINT) - DEFAULT_MATERIAL_OPACITY);

				currentDsmValues.w = computeNewVisibility(currentDsmValues.w, DEFAULT_MATERIAL_OPACITY);
				currentDsmValues.z = computeNewVisibility(currentDsmValues.z, DEFAULT_MATERIAL_OPACITY);
				currentDsmValues.y = computeNewVisibility(currentDsmValues.y, DEFAULT_MATERIAL_OPACITY);

				currentDsmValues.yz = computeMiddleSamples(currentDsmValues.x, v, currentDsmValues.y, currentDsmValues.z);

				imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(currentDsmValues.x, currentDsmValues.y, currentDsmValues.z, currentDsmValues.w));
			}
			else if(depth < (currentDsmValues.z >> 8))
			{
				v = computeVisibilitySample(depth, (currentDsmValues.y & MAX_8UINT) - DEFAULT_MATERIAL_OPACITY);
				
				currentDsmValues.w = computeNewVisibility(currentDsmValues.w, DEFAULT_MATERIAL_OPACITY);
				currentDsmValues.z = computeNewVisibility(currentDsmValues.z, DEFAULT_MATERIAL_OPACITY);

				currentDsmValues.yz = computeMiddleSamples(currentDsmValues.x, currentDsmValues.y, v, currentDsmValues.z);

				imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(currentDsmValues.x, currentDsmValues.y, currentDsmValues.z, currentDsmValues.w));
			}
			else
			{
				v = computeVisibilitySample(depth, (currentDsmValues.z & MAX_8UINT) - DEFAULT_MATERIAL_OPACITY);

				currentDsmValues.w = computeNewVisibility(currentDsmValues.w, DEFAULT_MATERIAL_OPACITY);

				currentDsmValues.yz = computeMiddleSamples(currentDsmValues.x, currentDsmValues.y, currentDsmValues.z, v);

				imageStore(deepShadowImage, ivec2(gl_FragCoord.xy), uvec4(currentDsmValues.x, currentDsmValues.y, currentDsmValues.z, currentDsmValues.w));
			}
		}
	}
}