//glsl version 4.5
#version 450
#extension GL_EXT_nonuniform_qualifier : enable

struct ClipPositions
{
	vec4 lastFrame;
	vec4 currentFrame;
};

//shader input
layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 texCoord;
layout (location = 3) in ClipPositions clipPositions;

//output write
layout (location = 0) out vec4 outPosition;
layout (location = 1) out vec4 outNormal;
layout (location = 2) out vec4 outFragColor;
layout (location = 3) out vec4 outMotionVector;

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

float near = 0.1;
float far = 100.0;

float linearizeDepth(float depth)
{
	float z = depth * 2.0 - 1.0;
	return (2.0 * near * far) / (far + near - z * (far - near));
}

void main() 
{	
	int matIndex = int(objectPushConstant.matIndex.x);
	Material material = materials.m[matIndex];

	int textureIndex = int(materials.m[matIndex].roughness_metallic_tilling_color_factors.w);

	int occlusionTextureIdx = int(material.emissive_metRough_occlusion_normal_indices.z);
	vec3 occlusionTexture = texture(textures[occlusionTextureIdx], texCoord).xyz;
	
	if(occlusionTextureIdx >= 0 && occlusionTexture.x < 0.001 && occlusionTexture.y < 0.001 && occlusionTexture.z < 0.001)
	{
		discard;
	}
	
	float tilling = material.roughness_metallic_tilling_color_factors.z;
	vec2 uv = texCoord * tilling;

	vec3 color;

	if(textureIndex < 0.001)
	{
		color = materials.m[matIndex].color.xyz;
	}
	else
	{
		color = texture(textures[textureIndex], uv).xyz;
	}

	vec2 position = clipPositions.currentFrame.xy / clipPositions.currentFrame.w * 0.5f + 0.5f;
	vec2 lastFramePosition = clipPositions.lastFrame.xy / clipPositions.lastFrame.w * 0.5f + 0.5f;

	outPosition = vec4(inPosition, 1.0);
	outNormal = vec4(inNormal, 1.0) * 0.5 + vec4(0.5);

	//float depth = linearizeDepth(gl_FragCoord.z) / far;
	//outFragColor = vec4(vec3(depth), 1.0);

	outFragColor = vec4(color, float(matIndex) / 100.0);

	outMotionVector = vec4((position - lastFramePosition) * 0.5f + 0.5f, 0.0, 1.0);
}