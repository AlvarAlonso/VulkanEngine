#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : require

#include "random.h"
#include "shaderCommon.h"

// Payloads
layout(location = 0) rayPayloadInEXT DistanceRayPayload prd;

void main()
{
	prd.distance = gl_HitTEXT;
}