#version 450 core

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 out_color;

layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
} scene_uniforms;

layout(std140, set = 1, binding = 0) uniform ModelUniform {
	mat4 model;
	vec3 albedo;
	vec3 specular;
	float alpha;
	float use_vertex_colors;
} model_uniforms;

layout(push_constant) uniform PushConstants {
	vec4 override_color;
} push_constants;

void main() {
	gl_Position = scene_uniforms.view_projection * model_uniforms.model * vec4(in_position, 1.0);

	vec3 color = model_uniforms.albedo * mix(vec3(1.0), in_color, model_uniforms.use_vertex_colors);
	out_color = mix(color, push_constants.override_color.rgb, push_constants.override_color.a);
}
