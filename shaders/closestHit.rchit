#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

struct RayPayload {
	vec3 color;
	float distance;
	vec3 normal;
	float reflector;
};

layout(location = 0) rayPayloadInEXT RayPayload rayPayload;
layout(location = 1) rayPayloadInEXT bool isShadowed;

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;

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

struct Material {
	vec4 color;
	vec4 properties; //metalness = x, roughness = y, shininess = z, fresnel = w
};

layout(binding = 3, set = 0, scalar) buffer Vertices { Vertex v[]; } vertices[];
layout(binding = 4, set = 0) buffer Indices { uint i[]; } indices[];
layout(binding = 5, set = 0) buffer Transforms { mat4 t; } transforms[];
layout(binding = 6, set = 0) uniform Lights { Light l[3]; } lights;
layout(binding = 7, set = 0) buffer Materials { Material m[]; } materials;
layout(binding = 8, set = 0) buffer MatIdx { uint i[]; } matIndices;

float computeAttenuation( in float distanceToLight, in float maxDist )
{
	float att_factor = maxDist - distanceToLight;
	att_factor /= maxDist;
	att_factor = max(att_factor, 0.0);
	return att_factor*att_factor;
}

void main()
{
	const vec3 barycentrics = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);

	// Vertex of the triangle
	uint i0 = indices[gl_InstanceCustomIndexEXT].i[3 * gl_PrimitiveID + 0];
	uint i1 = indices[gl_InstanceCustomIndexEXT].i[3 * gl_PrimitiveID + 1];
	uint i2 = indices[gl_InstanceCustomIndexEXT].i[3 * gl_PrimitiveID + 2];

	Vertex v0 = vertices[gl_InstanceCustomIndexEXT].v[i0];
	Vertex v1 = vertices[gl_InstanceCustomIndexEXT].v[i1];
	Vertex v2 = vertices[gl_InstanceCustomIndexEXT].v[i2];

	// Computing the normal at hit position
	vec3 normal = vec3(v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z);
	// Transforming the normal to world space
	normal = normalize(vec3(transforms[gl_InstanceCustomIndexEXT].t * vec4(normal, 0.0)));

	//hitValue = vec3(0.9f, 0.9f, 0.9f);

	  // Computing the coordinates of the hit position
	  vec3 worldPos = vec3(v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z);
	  // Transforming the position to world space
	  worldPos = vec3(transforms[gl_InstanceCustomIndexEXT].t * vec4(worldPos, 1.0));

	  vec3 totalLight = vec3(0);

  for(int i = 0; i < 3; i++)
  {
	  // Vector toward the light
	  vec3  L;
	  float lightIntensity = lights.l[i].color_intensity.w;
	  float lightMaxDist  = lights.l[i].position_maxDist.w;

	  // Point light
	  vec3 lDir      = lights.l[i].position_maxDist.xyz - worldPos;
	  float lightDistance  = length(lDir);
	  //lightIntensity = lightIntensity / (lightDistance * lightDistance);
	  L              = normalize(lDir);

	  float dotNL = max(dot(normal, L), 0.0);

	  float attenuation = computeAttenuation( lightDistance,  lightMaxDist );

	  if(dotNL > 0.0) 
	  {
		  // Tracing shadow ray only if the light is visible from the surface
		  if(dot(normal, L) > 0)
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
				}
			}
		}

		totalLight += vec3(dotNL) * lights.l[i].color_intensity.xyz * lightIntensity * attenuation;
	}

	totalLight /= 3;
	
	rayPayload.color = totalLight * vec3(materials.m[matIndices.i[gl_InstanceCustomIndexEXT]].color);
	rayPayload.distance = gl_RayTmaxEXT;
	rayPayload.normal = normal;
	rayPayload.reflector = materials.m[matIndices.i[gl_InstanceCustomIndexEXT]].properties.z;
}