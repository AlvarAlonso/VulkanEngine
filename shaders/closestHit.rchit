#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

struct RayPayload {
	vec4 color_dist;
	vec4 direction;
	vec4 origin;
};

layout(location = 0) rayPayloadInEXT RayPayload rayPayload;
layout(location = 1) rayPayloadInEXT bool isShadowed;

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;

#define PI 3.1415926535897932384626433832795

hitAttributeEXT vec3 attribs;

struct Vertex {
	vec4 position;
	vec4 normal;
	vec4 uv;
};

struct Light {
	vec4 position_maxDist;
	vec4 color_intensity;
};

struct Primitive {
	vec4 firstIdx_rndIdx_matIdx_transIdx;
};

struct Material {
	vec4 color;
	vec4 emissive_factor;
	vec4 roughness_metallic_tilling_color_factors; // Color is the index to the color texture
	vec4 emissive_metRough_occlusion_normal_indices; // Indices to material textures
};

layout(binding = 2, set = 0) uniform CameraProperties 
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
layout(binding = 9, set = 0) uniform sampler2D textures[]; //image2D ?

//PBR
float D_GGX ( const in float NoH, const in float linearRoughness );
vec3 F_Schlick( const in float VoH, const in vec3 f0 );
float GGX(float NdotV, float k);
float G_Smith( float NdotV, float NdotL, float roughness);
vec3 specularBRDF( float roughness, vec3 f0, float NoH, float NoV, float NoL, float LoH );

float computeAttenuation( in float distanceToLight, in float maxDist )
{
	float att_factor = maxDist - distanceToLight;
	att_factor /= maxDist;
	att_factor = max(att_factor, 0.0);
	return att_factor*att_factor;
}

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
	N = normalize(vec3(transforms.t[transformIndex] * vec4(N, 0.0)));

	// Computing the coordinates of the hit position
	vec3 worldPos = vec3(v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z);
	// Transforming the position to world space
	worldPos = vec3(transforms.t[renderableIndex] * vec4(worldPos, 1.0));

	vec2 uv = vec2(v0.uv.xy * barycentrics.x + v1.uv.xy * barycentrics.y + v2.uv.xy * barycentrics.z);

	//MATERIAL INFO
	Material material = materials.m[materialIndex];

	float metal = material.roughness_metallic_tilling_color_factors.y;
	float roughness = material.roughness_metallic_tilling_color_factors.x;
	vec3 color_texture = texture(textures[int(material.roughness_metallic_tilling_color_factors.w)], uv).xyz;
	vec3 color_material = material.color.xyz;

	//calculate f0 reflection based on the color and metalness
	vec3 f0 = color_texture * metal + (vec3( 0.5 ) * ( 1.0 - metal ));

	//COMPUTE LIGHT
	vec3 totalLight = vec3(0);

  for(int i = 0; i < 5; i++)
  {
	  float lightIntensity = lights.l[i].color_intensity.w;
	  float lightMaxDist  = lights.l[i].position_maxDist.w;

	  // Point light
	  vec3 lDir      = lights.l[i].position_maxDist.xyz - worldPos;
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
		  float tMin   = 0.001;
		  float tMax   = lightDistance;
		  uint  flags = gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT;
		  isShadowed = true;
		  traceRayEXT(topLevelAS,  // acceleration structure
				  flags,       // rayFlags
				  0xFF,        // cullMask
				  0,           // sbtRecordOffset
				  0,           // sbtRecordStride
				  1,           // missIndex
				  worldPos,      // ray origin
				  tMin,        // ray min range
				  L,          // ray direction
				  tMax,        // ray max range
				  1            // payload (location = 1)
		  );

		  if(isShadowed)
		  {
			  attenuation = 0.0;
			  totalLight += 0.0;
			  continue;
		  }
	  }
		
	  //calulate the specular and diffuse
	  vec3 diffuse = ( 1.0 - metal ) * color_texture * color_material;	//the more metalness the less diffuse color
	  vec3 ks = specularBRDF( roughness, f0, NdotH, NdotV, NdotL, LdotH );
	  vec3 kd = diffuse * NdotL;
	  vec3 direct = kd + ks;

	  totalLight += direct * lights.l[i].color_intensity.xyz * lightIntensity * attenuation;
	}

	//vec3 finalColor = totalLight;
	vec3 finalColor = color_material;

	//PAYLOAD INFORMATION
	//float materialType = materials.m[matIndices.i[gl_InstanceCustomIndexEXT]].properties.w;


	rayPayload.color_dist = vec4(finalColor, gl_RayTmaxEXT);

	if(rayPayload.origin.w < 0.001)
	{
		rayPayload.direction.xyz = N;
	}

	rayPayload.direction.w = -1.0;

	rayPayload.origin.w += 1;
	rayPayload.origin.xyz = worldPos;
	
	/*
	else if(materialType == 1.0)
	{
		vec3 I = normalize(gl_WorldRayDirectionEXT);

		vec3 direction = reflect(I, N);

		rayPayload.color_dist = vec4(finalColor, gl_RayTmaxEXT);
		rayPayload.direction = vec4(direction, 1.0);

		if(rayPayload.origin.w == 1.0)
		{
			rayPayload.origin = vec4(worldPos, 2.0);
		}
		else
		{
			rayPayload.origin = vec4(worldPos, 0.0);
		}
	}
	else if(materialType == 2.0)
	{
		finalColor = vec3(0.0);
		//vec3 N = normalize(N);
		vec3 D = normalize(gl_WorldRayDirectionEXT);
		
		float NdotD = dot(N, D); 
			
		vec3 refractedN = NdotD > 0.0 ? -N : N;
		float ior = materials.m[matIndices.i[gl_InstanceCustomIndexEXT]].properties.z;
		float eta = NdotD > 0.0 ? 1.0 / ior : ior;

		vec3 direction = refract(D, refractedN, eta);

		rayPayload.color_dist = vec4(finalColor, gl_RayTmaxEXT);
		rayPayload.direction = vec4(direction.xyz,  1.0);

		if(rayPayload.origin.w == 1.0)
		{
			rayPayload.origin = vec4(worldPos, 2.0);
		}
		else
		{
			rayPayload.origin = vec4(worldPos, 0.0);
		}
	}
	*/
}

float D_GGX ( const in float NoH, const in float linearRoughness )
{
	float a2 = linearRoughness * linearRoughness;
	float f = (NoH * NoH) * (a2 - 1.0) + 1.0;
	return a2 / (PI * f * f);
}

vec3 F_Schlick( const in float VoH, const in vec3 f0 )
{
	float f = pow(1.0 - VoH, 5.0);
	return f0 + (vec3(1.0) - f0) * f;
}

float GGX(float NdotV, float k)
{
	return NdotV / (NdotV * (1.0 - k) + k);
}
	
float G_Smith( float NdotV, float NdotL, float roughness)
{
	float k = pow(roughness + 1.0, 2.0) / 8.0;
	return GGX(NdotL, k) * GGX(NdotV, k);
}

vec3 specularBRDF( float roughness, vec3 f0, float NoH, float NoV, float NoL, float LoH )
{
	float a = roughness * roughness;

	float D = D_GGX( NoH, a );
	vec3 F = F_Schlick( LoH, f0 );
	float G = G_Smith( NoV, NoL, roughness );
	
	vec3 spec = D * G * F;
	spec /= ( 4.0 * NoL * NoV + 1e-6 );

	return spec;
}