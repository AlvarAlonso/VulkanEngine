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
    rayPayload.color_dist = vec4(0.0, 0.0, 0.2, -1.0);
}