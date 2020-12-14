#version 450

//shader input
layout (location = 0) in vec3 normal;

//output write
layout (location = 0) out vec4 outFragColor;

void main() 
{	
	outFragColor = vec4(normal, 1.0f);
}