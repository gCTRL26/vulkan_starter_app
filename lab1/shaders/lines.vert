#version 450 core

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 out_color;

layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
} scene_uniforms;

layout(std140, set = 1, binding = 0) uniform ModelUniform {
	mat4 model;
	vec4 line_color;
	vec3 specular;
	float alpha;
} model_uniforms;

void main() {
	gl_Position = scene_uniforms.view_projection * model_uniforms.model * vec4(in_position, 1.0);

	out_color = mix(in_color, model_uniforms.line_color.rgb, model_uniforms.line_color.a);
}
