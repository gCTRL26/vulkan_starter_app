#version 450 core

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;

layout(location = 0) out vec4 out_color;

struct PointLight {
	vec3 position;
	vec3 color;
};

struct SpotLight {
	vec3 position;
	vec3 color;
	vec3 direction;
	float inner_cutoff;
	float outer_cutoff;
};

layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
	vec3 camera_position;
	vec3 directional_light_direction;
	vec3 directional_light_color;
	vec3 ambient_light;
	uint point_light_count;
	uint spot_light_count;
} scene_uniforms;

layout(std430, set = 0, binding = 1) readonly buffer PointLights {
	PointLight point_lights[];
};

layout(std430, set = 0, binding = 2) readonly buffer SpotLights {
	SpotLight spot_lights[];
};

layout(std140, set = 1, binding = 0) uniform ModelUniform {
	mat4 model;
	vec3 albedo;
	vec3 specular;
	float alpha;
	float use_vertex_colors;
} model_uniforms;

vec3 blinnPhong(vec3 n, vec3 v, vec3 l, vec3 light, vec3 albedo) {
	vec3 h = normalize(l + v);
	float n_dot_l = max(dot(n, l), 0.0);
	float n_dot_h = max(dot(n, h), 0.0);

	return (albedo * n_dot_l + model_uniforms.specular * pow(n_dot_h, model_uniforms.alpha)) * n_dot_l * light;
}

void main() {
	vec3 n = normalize(in_normal);
	vec3 v = normalize(scene_uniforms.camera_position - in_position);
	vec3 albedo = model_uniforms.albedo * in_color;

	vec3 color = scene_uniforms.ambient_light * albedo;

	color += blinnPhong(n, v, normalize(-scene_uniforms.directional_light_direction),
	                    scene_uniforms.directional_light_color, albedo);

	for (uint i = 0; i < scene_uniforms.point_light_count; ++i) {
		vec3 to_light = point_lights[i].position - in_position;
		float distance_squared = dot(to_light, to_light);
		vec3 l = to_light / sqrt(distance_squared);

		color += blinnPhong(n, v, l, point_lights[i].color / distance_squared, albedo);
	}

	for (uint i = 0; i < scene_uniforms.spot_light_count; ++i) {
		vec3 to_light = spot_lights[i].position - in_position;
		float distance_squared = dot(to_light, to_light);
		vec3 l = to_light / sqrt(distance_squared);

		float cos_angle = dot(-l, normalize(spot_lights[i].direction));
		float cone = clamp((cos_angle - spot_lights[i].outer_cutoff) /
		                   max(spot_lights[i].inner_cutoff - spot_lights[i].outer_cutoff, 0.0001), 0.0, 1.0);

		color += blinnPhong(n, v, l, spot_lights[i].color * cone / distance_squared, albedo);
	}

	out_color = vec4(color, 1.0);
}
