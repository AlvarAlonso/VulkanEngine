#version 460
#extension GL_EXT_ray_tracing : enable

struct RayPayload {
	vec4 color_dist;
	vec4 direction;
	vec3 origin;
};

layout(location = 0) rayPayloadInEXT RayPayload rayPayload;

void main()
{
	// View-independent background gradient to simulate a basic sky background
	const vec3 gradientStart = vec3(0.40, 0.49, 1.0);
	const vec3 gradientEnd = vec3(0.15, 0.32, 1.0);
	vec3 unitDir = normalize(gl_WorldRayDirectionEXT);
	float t = 0.5 * (unitDir.y + 1.0);
	vec3 color = (1.0-t) * gradientStart + t * gradientEnd;
	rayPayload.color_dist = vec4(color, -1.0);

    //rayPayload.color_dist = vec4(0.0, 0.0, 0.2, -1.0);
}