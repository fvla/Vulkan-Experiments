#version 450
#pragma shader_stage(vertex)

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

layout(push_constant) uniform constants
{
	mat4 renderMatrix;
} pushConstants;

void main() {
    gl_Position = pushConstants.renderMatrix * vec4(inPosition, 1.0);
    fragColor = inColor;
}
