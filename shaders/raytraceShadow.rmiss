#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#include "random.h"
#include "shaderCommon.h"

layout(location = 1) rayPayloadInEXT ShadowRayPayload prd;

void main()
{
	prd.hardShadowed = false;
}
