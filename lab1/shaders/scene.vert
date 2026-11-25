#version 450 core

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec3 out_position;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;
layout(location = 3) out vec4 out_shadow_position;

layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
	mat4 shadow_projection;
	vec3 camera_position;
	vec3 directional_light_direction;
	vec3 directional_light_color;
	vec3 ambient_light;
	uint point_light_count;
	uint spot_light_count;
} scene_uniforms;

layout(std140, set = 1, binding = 0) uniform ModelUniform {
	mat4 model;
	vec4 line_color;
	vec3 specular;
	float alpha;
} model_uniforms;

void main() {
	vec4 position = model_uniforms.model * vec4(in_position, 1.0);
	gl_Position = scene_uniforms.view_projection * position;

	out_position = position.xyz;
	out_normal = transpose(inverse(mat3(model_uniforms.model))) * in_normal;
	out_uv = in_uv;
	out_shadow_position = scene_uniforms.shadow_projection * position;
}
