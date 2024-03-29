#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : require

#include "random.h"
#include "shaderCommon.h"

hitAttributeEXT vec3 attribs;

// Payloads
layout(location = 0) rayPayloadInEXT RayPayload prd;
layout(location = 1) rayPayloadEXT ShadowRayPayload shadowPrd;

// Descriptors
layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 0) uniform CameraProperties 
{
	mat4 viewInverse;
	mat4 projInverse;
	vec4 position;
} cam;
layout(binding = 3, set = 0, scalar) buffer Vertices { Vertex v[]; } vertices[];
layout(binding = 4, set = 0) buffer Indices { uint i[]; } indices[];
layout(binding = 5, set = 0) buffer Transforms { mat4 t[]; } transforms;
layout(binding = 6, set = 0) buffer Primitives { Primitive p[]; } primitives;
layout(binding = 7, set = 0) uniform Lights { Light l[5]; } lights;
layout(binding = 8, set = 0) buffer Materials { Material m[]; } materials;
layout(binding = 9, set = 0) uniform sampler2D textures[];

void main()
{
	//COMPUTE TRIANGLE INFO

	const vec3 barycentrics = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);

	// Primitive Information
	Primitive primitive = primitives.p[gl_InstanceCustomIndexEXT];
	uint firstIndex = uint(primitive.firstIdx_rndIdx_matIdx_transIdx.x);
	uint renderableIndex = uint(primitive.firstIdx_rndIdx_matIdx_transIdx.y);
	uint materialIndex = uint(primitive.firstIdx_rndIdx_matIdx_transIdx.z);
	uint transformIndex = uint(primitive.firstIdx_rndIdx_matIdx_transIdx.w);

	// Vertex of the triangle
	uint i0 = indices[renderableIndex].i[3 * gl_PrimitiveID + firstIndex + 0];
	uint i1 = indices[renderableIndex].i[3 * gl_PrimitiveID + firstIndex + 1];
	uint i2 = indices[renderableIndex].i[3 * gl_PrimitiveID + firstIndex + 2];

	Vertex v0 = vertices[renderableIndex].v[i0];
	Vertex v1 = vertices[renderableIndex].v[i1];
	Vertex v2 = vertices[renderableIndex].v[i2];

	// Computing the normal at hit position
	vec3 N = vec3(v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z);
	// Transforming the normal to world space
	N = normalize(vec3(transforms.t[transformIndex] * vec4(N, 0.0))); // TODO: Inverse Transpose of the transform

	// Computing the coordinates of the hit position
	vec3 worldPos = vec3(v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z);
	// Transforming the position to world space
	worldPos = vec3(transforms.t[transformIndex] * vec4(worldPos, 1.0));

	vec2 uv = vec2(v0.uv.xy * barycentrics.x + v1.uv.xy * barycentrics.y + v2.uv.xy * barycentrics.z);

	//MATERIAL INFO
	Material material = materials.m[materialIndex];

	// Calculate if the hit was in a non opaque area
	int occlusionTextureIdx = int(material.emissive_metRough_occlusion_normal_indices.z);
	vec3 occlusion_texture = texture(textures[occlusionTextureIdx], uv).xyz;
	
	if(occlusionTextureIdx >= 0 && occlusion_texture.x < 0.001 && occlusion_texture.y < 0.001 && occlusion_texture.z < 0.001)
	{
		prd.color_dist.w = gl_RayTmaxEXT;
		prd.origin.xyz = offsetPositionAlongNormal(worldPos, -N);
		prd.origin.w = 0;
		return;
	}
	// origin.w is different from 0 if this hit is not skipped
	prd.origin.w = 1;

	// Material properties
	float roughness = material.roughness_metallic_tilling_color_factors.x;
	float metal = material.roughness_metallic_tilling_color_factors.y;
	float tilling = material.roughness_metallic_tilling_color_factors.z;
	
	//apply tilling
	uv *= tilling;

	//get texture texels values
	int textureIndex = int(material.roughness_metallic_tilling_color_factors.w);
	vec3 color_texture = texture(textures[textureIndex], uv).xyz;
	vec3 color_material = material.color.xyz;

	if(textureIndex < 0.001)
	{
		color_texture *= color_material;
	}

	//calculate f0 reflection based on the color and metalness
	vec3 f0 = color_texture * metal + (vec3( 0.5 ) * ( 1.0 - metal ));

	//COMPUTE LIGHT
	vec3 totalLight = vec3(0);

  for(int i = 0; i < 1; i++)
  {
	  // light info
	  float lightIntensity = lights.l[i].color_intensity.w;
	  float lightMaxDist  = lights.l[i].position_maxDist.w;
	  vec3 lightPosition = lights.l[i].position_maxDist.xyz;
	  float radius = lights.l[i].properties_type.x; // TODO

	  // Point light
	  vec3 lDir      = lightPosition - worldPos;
	  float lightDistance  = length(lDir);

	  vec3 L = normalize(lDir);
	  vec3 V = normalize( cam.position.xyz - worldPos );
	  vec3 H = normalize( L + V );
	  float NdotL = clamp( dot( N, L ), 0.0, 1.0 );
	  float NdotV = clamp( dot( N, V ), 0.0, 1.0 );
	  float NdotH = clamp( dot( N, H ), 0.0, 1.0 );
      float LdotH = clamp( dot( L, H ), 0.0, 1.0 );

	  float attenuation = computeAttenuation( lightDistance,  lightMaxDist );
	  
	  // Tracing shadow ray only if the light is visible from the surface
	  if(NdotL > 0.0)
	  {
		  
		  // Calculates the angle of a cone that starts at position worldPosition and perfectly
		  // encapsulates a sphere at position light.position with radius light.radius
		  vec3 perpL = cross(L, vec3(0.0, 1.0, 0.0));
		  // Handle case where L = up -> perpL should then be (1, 0, 0)
		  if(perpL == vec3(0))
		  {
			perpL.x = 1.0;
		  }
		  // Use perpL to get a vector from worldPosition to the edge of the light sphere
		  vec3 toLightEdge = normalize((lightPosition + perpL * radius) - worldPos); // radius
		  // Angle between L and toLightEdge. Used as the cone angle when sampling shadow rays
		  float coneAngle = acos(dot(L, toLightEdge)) * 2.0f;

		  vec3 sampledDirection = normalize(getConeSample(prd.seed, L, coneAngle));

		  float tMin   = 0.001;
		  float tMax   = length(lightDistance + 100);
		  uint  flags = gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT;
		  shadowPrd.alpha = 0.0f;
		  shadowPrd.hardShadowed = true;

			// trace to the first opaque surface
			traceRayEXT(topLevelAS,
			flags,
			0xFE,
			0,
			0,
			1,
			worldPos,
			tMin,
			-sampledDirection,
			tMax,
			1);

			if(shadowPrd.hardShadowed == true)
			{
				continue;
			}

			flags = gl_RayFlagsSkipClosestHitShaderEXT;

			traceRayEXT(topLevelAS,  // acceleration structure
			flags,       // rayFlags
			0xFD,        // cullMask
			1,           // sbtRecordOffset
			0,           // sbtRecordStride
			1,           // missIndex
			worldPos,      // ray origin
			tMin,        // ray min range
			-sampledDirection,          // ray direction
			tMax,        // ray max range
			1            // payload (location = 1)
			);

		clamp(shadowPrd.alpha, 0.0, 1.0);
		attenuation = 1.0 - shadowPrd.alpha;
		if(attenuation < 0.0001) continue;
	  }
		
	  //calulate the specular and diffuse
	  vec3 diffuse = ( 1.0 - metal ) * color_texture * color_material;	//the more metalness the less diffuse color
	  vec3 ks = specularBRDF( roughness, f0, NdotH, NdotV, NdotL, LdotH );
	  vec3 kd = diffuse * NdotL;
	  vec3 direct = kd + ks;

	  totalLight += direct * lights.l[i].color_intensity.xyz * lightIntensity * attenuation;
	}

	vec3 finalColor = totalLight;

	//PAYLOAD INFORMATION

	prd.color_dist = vec4(finalColor, gl_RayTmaxEXT);

	if(prd.origin.w < 0.001)
	{
		prd.direction.xyz = N;
	}

	prd.direction.w = -1.0;

	prd.origin.w += 1;
	prd.origin.xyz = worldPos;
}