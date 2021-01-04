#version 460

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;
layout (location = 3) in vec2 vTexCoord;

layout (location = 0) out vec3 outPosition;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 texCoord;
layout (location = 3) out vec3 randomColor;

layout(set = 0, binding = 0) uniform CameraBuffer {
	mat4 view;
	mat4 proj;
	mat4 viewproj;
} cameraData;

struct ObjectData {
	mat4 model;
};

layout(std140, set = 1, binding = 0) readonly buffer ObjectBuffer {
	ObjectData objects[];
} objectBuffer;

vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

void main() 
{	
	
	gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);

	mat4 mo = mat4(1);
	/*
	mat4 zeroes = mat4(0.0, 0.0, 0.0, 0.0,
                  0.0, 0.0, 0.0, 0.0,
                  0.0, 0.0, 0.0, 0.0, 
                  0.0, 0.0, 0.0, 0.0);
	
	if(cameraData.viewproj == zeroes)
	{
		randomColor = vec3(0.5, 0.5, 0.5);
	}
	else
	{
	    randomColor = vPosition;
	}
	*/

	randomColor = vPosition;
	mat4 modelMatrix = mo; //objectBuffer.objects[gl_BaseInstance].model;
	mat4 transformMatrix = (cameraData.viewproj * modelMatrix);

	gl_Position = transformMatrix * vec4(vPosition, 1.0f);
	
	randomColor = randomColor;
	outPosition = vPosition;
	outNormal = vNormal;
	texCoord = vTexCoord;
}