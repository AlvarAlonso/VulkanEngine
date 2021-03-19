#version 460

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;
layout (location = 3) in vec2 vTexCoord;

struct ClipPositions
{
	vec4 lastFrame;
	vec4 currentFrame;
};

layout (location = 0) out vec3 outPosition;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 texCoord;
layout (location = 3) out ClipPositions clipPositions;

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

	clipPositions.lastFrame = cameraData.viewproj_lastFrame * modelMatrix * vec4(vPosition, 1.0f);
	clipPositions.currentFrame = cameraData.viewproj * modelMatrix * vec4(vPosition, 1.0f);

	gl_Position = clipPositions.currentFrame;

	outPosition = vec3(modelMatrix * vec4(vPosition, 1.0));
	outNormal = mat3(transpose(inverse(modelMatrix))) * vNormal;
	texCoord = vTexCoord;
}