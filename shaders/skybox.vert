#version 460

layout (location = 0) in vec3 vPosition;

layout(set = 0, binding = 0) uniform CameraBuffer {
	mat4 view;
	mat4 proj;
	mat4 viewproj;
	mat4 viewproj_lastFrame;
} cameraData;

layout(location = 0) out vec3 outUVW;

void main()
{
	outUVW = vPosition;
	outUVW.xy *= -1.0;
	mat4 view = cameraData.view;
	view[3] = vec4(0.0, 0.0, 0.0, 1.0);

	gl_Position = cameraData.proj * view * vec4(vPosition, 1.0);
}