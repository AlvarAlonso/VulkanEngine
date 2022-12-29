#version 460

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;
layout (location = 3) in vec2 vTexCoord;

layout (location = 0) out vec2 texCoord;

layout(set = 0, binding = 0) uniform CameraBuffer {
	mat4 view;
	mat4 proj;
	mat4 viewproj;
	mat4 viewproj_lastFrame;
} cameraData;

layout( push_constant, std140 ) uniform ModelMatrix {
	mat4 modelMatrix;
  	vec4 matIndex; // currently using only x component to pass the primitive material index
} objectPushConstant;

void main()
{
	mat4 modelMatrix = objectPushConstant.modelMatrix;

	gl_Position = cameraData.viewproj * modelMatrix * vec4(vPosition, 1.0f);
	texCoord = vTexCoord;
}