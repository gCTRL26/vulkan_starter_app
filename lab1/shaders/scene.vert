#version 450 core

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 out_color;

layout(std140, set = 0, binding = 0) uniform GlobalUniforms {
	mat4 view;
	mat4 projection;
} global_uniforms;

layout(std140, set = 1, binding = 0) uniform ObjectUniforms {
	mat4 model;
	vec4 color;
	float use_vertex_colors;
} object_uniforms;

layout(push_constant) uniform PushConstants {
	vec4 override_color;
} push_constants;

void main() {
	gl_Position = global_uniforms.projection * global_uniforms.view *
	              object_uniforms.model * vec4(in_position, 1.0);

	vec3 vertex_color = mix(vec3(1.0), in_color, object_uniforms.use_vertex_colors);
	vec3 color = object_uniforms.color.rgb * vertex_color;

	out_color = mix(color, push_constants.override_color.rgb, push_constants.override_color.a);
}
