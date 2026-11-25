#version 450 core

layout(location = 0) in vec3 in_position;
layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
	mat4 shadow_projection;
} scene;
layout(std140, set = 1, binding = 0) uniform ModelUniform {
	mat4 model;
} object;

void main() {
	gl_Position = scene.shadow_projection * object.model * vec4(in_position, 1.0);
}
