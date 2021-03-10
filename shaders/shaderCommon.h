// Common GLSL file shared across ray tracing shaders.
#ifndef VK_ENGINE_SHADER_COMMON_H
#define VK_ENGINE_SHADER_COMMON_H

//#include "random.h";

#define PI 3.1415926535897932384626433832795

// vec4 origin = 0 means it has ignored the hit
struct RayPayload {
	vec4 color_dist;
	vec4 direction;
	vec4 origin;
	uint seed;
};

struct ShadowRayPayload
{
	float alpha;
	bool hardShadowed;
};

struct Vertex {
	vec4 position;
	vec4 normal;
	vec4 uv;
};

struct Light {
	vec4 position_maxDist;
	vec4 color_intensity;
    float radius;
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

float D_GGX(const in float NoH, const in float linearRoughness)
{
	float a2 = linearRoughness * linearRoughness;
	float f = (NoH * NoH) * (a2 - 1.0) + 1.0;
	return a2 / (PI * f * f);
}

vec3 F_Schlick(const in float VoH, const in vec3 f0)
{
	float f = pow(1.0 - VoH, 5.0);
	return f0 + (vec3(1.0) - f0) * f;
}

float GGX(float NdotV, float k)
{
	return NdotV / (NdotV * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness)
{
	float k = pow(roughness + 1.0, 2.0) / 8.0;
	return GGX(NdotL, k) * GGX(NdotV, k);
}

vec3 specularBRDF(float roughness, vec3 f0, float NoH, float NoV, float NoL, float LoH)
{
	float a = roughness * roughness;

	float D = D_GGX(NoH, a);
	vec3 F = F_Schlick(LoH, f0);
	float G = G_Smith(NoV, NoL, roughness);

	vec3 spec = D * G * F;
	spec /= (4.0 * NoL * NoV + 1e-6);

	return spec;
}

float computeAttenuation(in float distanceToLight, in float maxDist)
{
	float att_factor = maxDist - distanceToLight;
	att_factor /= maxDist;
	att_factor = max(att_factor, 0.0);
	return att_factor * att_factor;
}

mat3 angleAxis3x3(float angle, vec3 axis)
{
    axis = normalize(axis);
    float s = sin(angle);
    float c = cos(angle);
    float oc = 1.0 - c;

    return mat3(oc * axis.x * axis.x + c,           oc * axis.x * axis.y - axis.z * s,  oc * axis.z * axis.x + axis.y * s,
                oc * axis.x * axis.y + axis.z * s,  oc * axis.y * axis.y + c,           oc * axis.y * axis.z - axis.x * s,
                oc * axis.z * axis.x - axis.y * s,  oc * axis.y * axis.z + axis.x * s,  oc * axis.z * axis.z + c);
}

// The normal can be negated if one wants the ray to pass through
// the surface instead.
vec3 offsetPositionAlongNormal(vec3 worldPosition, vec3 normal)
{
    // Convert the normal to an integer offset.
    const float int_scale = 256.0f;
    const ivec3 of_i = ivec3(int_scale * normal);

    // Offset each component of worldPosition using its binary representation.
    // Handle the sign bits correctly.
    const vec3 p_i = vec3(  //
        intBitsToFloat(floatBitsToInt(worldPosition.x) + ((worldPosition.x < 0) ? -of_i.x : of_i.x)),
        intBitsToFloat(floatBitsToInt(worldPosition.y) + ((worldPosition.y < 0) ? -of_i.y : of_i.y)),
        intBitsToFloat(floatBitsToInt(worldPosition.z) + ((worldPosition.z < 0) ? -of_i.z : of_i.z)));

    // Use a floating-point offset instead for points near (0,0,0), the origin.
    const float origin = 1.0f / 32.0f;
    const float floatScale = 1.0f / 65536.0f;
    return vec3(  //
        abs(worldPosition.x) < origin ? worldPosition.x + floatScale * normal.x : p_i.x,
        abs(worldPosition.y) < origin ? worldPosition.y + floatScale * normal.y : p_i.y,
        abs(worldPosition.z) < origin ? worldPosition.z + floatScale * normal.z : p_i.z);
}

// Sampler for a sphere
vec3 getSphereSample(inout uint rngState, const vec3 center, const float radius)
{
	const float theta = 2 * PI * rnd(rngState);
	const float phi = acos(1 - 2 * rnd(rngState));

	const float x = sin(phi) * cos(theta);
	const float y = sin(phi) * sin(theta);
	const float z = cos(phi);

	return center + vec3(x, y, z) * radius;
}

// Returns a random direction vector inside a cone
// Angle defined in radians
// Example: direction=(0,1,0) and angle=pi returns ([-1,1],[0,1],[-1,1])
vec3 getConeSample(inout uint rngState, vec3 direction, float coneAngle) {
    float cosAngle = cos(coneAngle);

    // Generate points on the spherical cap around the north pole [1].
    float z = rnd(rngState) * (1.0f - cosAngle) + cosAngle;
    float phi = rnd(rngState) * 2.0f * PI;

    float x = sqrt(1.0f - z * z) * cos(phi);
    float y = sqrt(1.0f - z * z) * sin(phi);
    vec3 north = vec3(0.f, 0.f, 1.f);

    // Find the rotation axis `u` and rotation angle `rot` [1]
    vec3 axis = normalize(cross(north, normalize(direction)));
    float angle = acos(dot(normalize(direction), north));

    // Convert rotation axis and angle to 3x3 rotation matrix [2]
    mat3 R = angleAxis3x3(angle, axis);

    return R * vec3(x, y, z);
}

#endif