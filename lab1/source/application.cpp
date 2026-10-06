#include "application.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

#include <imgui.h>

#include "graphics.hpp"
#include "hexahedron.hpp"
#include "math.hpp"

namespace application {

namespace {

using graphics::internal::context;

struct GlobalUniforms {
	math::Matrix4 view;
	math::Matrix4 projection;
};

struct ObjectUniforms {
	math::Matrix4 model;
	float color[4];
	float use_vertex_colors;
	float padding[3];
};

struct PushConstants {
	float override_color[4];
};

static_assert(sizeof(GlobalUniforms) == 128);
static_assert(sizeof(ObjectUniforms) == 96);

enum class ProjectionType : int {
	Perspective,
	Orthographic,
};

enum class Trajectory : int {
	None,
	Circle,
	Ellipse,
	Wave,
	Lissajous,
	Trefoil,
	Lemniscate,
	Rose,
	Count,
};

enum TrajectoryParameter : unsigned {
	UsesA = 1 << 0,
	UsesB = 1 << 1,
	UsesH = 1 << 2,
	UsesK = 1 << 3,
	UsesPhase = 1 << 4,
};

struct TrajectoryInfo {
	const char* name;
	const char* formula;
	unsigned parameters;
};

const TrajectoryInfo trajectory_infos[] = {
	{ "Нет (вращение на месте)", "p(t) = 0", 0 },
	{ "Окружность", "p(t) = (A cos t,  0,  A sin t)", UsesA },
	{ "Эллипс", "p(t) = (A cos t,  0,  B sin t)", UsesA | UsesB },
	{ "Волна вдоль окружности", "p(t) = (A cos t,  H sin(kt + φ),  A sin t)",
	  UsesA | UsesH | UsesK | UsesPhase },
	{ "Фигура Лиссажу", "p(t) = (A sin(t + φ),  H sin((k + 1)t),  B sin kt)",
	  UsesA | UsesB | UsesH | UsesK | UsesPhase },
	{ "Узел-трилистник", "p(t) = (A(sin t + 2 sin 2t) / 3,  −H sin 3t,  A(cos t − 2 cos 2t) / 3)",
	  UsesA | UsesH },
	{ "Лемниската Бернулли", "p(t) = (A cos t,  0,  B sin t cos t) / (1 + sin²t)", UsesA | UsesB },
	{ "Роза", "p(t) = (A cos kt cos t,  0,  A cos kt sin t)", UsesA | UsesK },
};

static_assert(sizeof(trajectory_infos) / sizeof(trajectory_infos[0]) == size_t(Trajectory::Count));

struct SceneObject {
	char name[64];

	math::Vector3 position;
	math::Vector3 rotation;
	math::Vector3 scale;
	bool uniform_scale;

	Trajectory trajectory;
	bool animate;
	float speed;
	float radius_a;
	float radius_b;
	float height;
	int frequency;
	float phase;
	float tilt;
	math::Vector3 spin_speed;
	bool orient_along_path;
	bool show_path;

	float color[3];
	bool use_vertex_colors;

	float t;
	math::Vector3 spin;

	ObjectUniforms uniforms;

	graphics::Buffer uniform_buffer;
	VkDescriptorSet descriptor_set;
};

struct Camera {
	math::Vector3 target = { 0.0f, 0.0f, 0.0f };
	float yaw = 30.0f;
	float pitch = 25.0f;
	float distance = 15.0f;

	ProjectionType projection = ProjectionType::Perspective;
	float fov = 60.0f;
	float ortho_height = 10.0f;
	bool ortho_follow_distance = true;
	float z_near = 0.1f;
	float z_far = 100.0f;
};

struct Settings {
	bool playing = true;
	float time_scale = 1.0f;

	hexahedron::ColorScheme color_scheme = hexahedron::ColorScheme::PositionRGB;
	bool mesh_dirty = false;

	bool show_edges = true;
	float edge_color[3] = { 0.02f, 0.02f, 0.02f };
	bool show_grid = true;
	bool show_axes = true;
	float grid_height = -3.0f;
	float background[3] = { 0.015f, 0.017f, 0.024f };
};

struct PendingRelease {
	graphics::Buffer buffer;
	VkDescriptorSet descriptor_set;
};

constexpr uint32_t max_objects = 16;
constexpr uint32_t path_segments = 256;
constexpr int grid_half_size = 10;
constexpr uint32_t max_line_vertices = 2048 + max_objects * path_segments * 2;

constexpr uint32_t max_descriptor_sets = 2 + max_objects;

const float object_palette[][3] = {
	{ 1.00f, 1.00f, 1.00f },
	{ 1.00f, 0.55f, 0.25f },
	{ 0.35f, 0.80f, 1.00f },
	{ 0.55f, 1.00f, 0.45f },
	{ 1.00f, 0.45f, 0.75f },
	{ 1.00f, 0.90f, 0.35f },
	{ 0.70f, 0.55f, 1.00f },
};

VkDescriptorSetLayout vk_global_set_layout;
VkDescriptorSetLayout vk_object_set_layout;
VkPipelineLayout vk_pipeline_layout;
VkPipeline vk_pipeline_triangles;
VkPipeline vk_pipeline_lines;
VkDescriptorPool vk_descriptor_pool;

graphics::Buffer global_uniform_buffer;
VkDescriptorSet vk_global_descriptor_set;

graphics::Buffer world_uniform_buffer;
VkDescriptorSet vk_world_descriptor_set;

graphics::Buffer vertex_buffer;
graphics::Buffer triangle_index_buffer;
graphics::Buffer edge_index_buffer;
graphics::Buffer line_vertex_buffer;

hexahedron::Mesh mesh;

std::vector<SceneObject> objects;
std::vector<PendingRelease> pending_releases;
int selected_object = 0;
int created_objects = 0;

Camera camera;
Settings settings;

GlobalUniforms global_uniforms;
std::vector<Vertex> line_vertices;
double last_time = -1.0;

VkDescriptorSet allocateUniformSet(VkDescriptorSetLayout layout, const graphics::Buffer& buffer,
                                   VkDeviceSize range) {
	const VkDescriptorSetAllocateInfo descriptor_set = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = vk_descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &layout,
	};

	VkDescriptorSet result;

	if (vkAllocateDescriptorSets(context.device, &descriptor_set, &result) != VK_SUCCESS) {
		std::cerr << "Failed to allocate descriptor set\n";
		return VK_NULL_HANDLE;
	}

	const VkDescriptorBufferInfo uniform_buffer_descriptor = {
		.buffer = buffer.buffer,
		.offset = 0,
		.range = range,
	};

	const VkWriteDescriptorSet descriptor_writes[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = result,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &uniform_buffer_descriptor,
		},
	};

	vkUpdateDescriptorSets(context.device, sizeof(descriptor_writes) / sizeof(descriptor_writes[0]),
	                       descriptor_writes, 0, nullptr);

	return result;
}

SceneObject makeObject() {
	const int index = created_objects++;
	const float* color = object_palette[index % (sizeof(object_palette) / sizeof(object_palette[0]))];

	SceneObject object = {
		.position = { 0.0f, 0.0f, 0.0f },
		.rotation = { 0.0f, 0.0f, 0.0f },
		.scale = { 1.0f, 1.0f, 1.0f },
		.uniform_scale = true,
		.trajectory = Trajectory(1 + index % (int(Trajectory::Count) - 1)),
		.animate = true,
		.speed = 0.8f,
		.radius_a = 4.0f,
		.radius_b = 2.5f,
		.height = 1.0f,
		.frequency = 3,
		.phase = 0.0f,
		.tilt = 0.0f,
		.spin_speed = { 0.0f, 60.0f, 0.0f },
		.orient_along_path = false,
		.show_path = true,
		.color = { color[0], color[1], color[2] },
		.use_vertex_colors = true,
		.t = 0.0f,
		.spin = { 0.0f, 0.0f, 0.0f },
	};

	std::snprintf(object.name, sizeof(object.name), "Гексаэдр %d", index + 1);
	return object;
}

bool addObject(SceneObject object) {
	if (objects.size() >= max_objects) {
		return false;
	}

	if (!graphics::createBuffer(sizeof(ObjectUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                            object.uniform_buffer)) {
		return false;
	}

	object.descriptor_set = allocateUniformSet(vk_object_set_layout, object.uniform_buffer,
	                                           sizeof(ObjectUniforms));
	if (object.descriptor_set == VK_NULL_HANDLE) {
		graphics::destroyBuffer(object.uniform_buffer);
		return false;
	}

	objects.push_back(object);
	selected_object = int(objects.size()) - 1;

	return true;
}

void removeObject(int index) {
	SceneObject& object = objects[size_t(index)];

	pending_releases.push_back({ object.uniform_buffer, object.descriptor_set });

	objects.erase(objects.begin() + index);
	selected_object = std::clamp(selected_object, 0, std::max(0, int(objects.size()) - 1));
}

void releasePending() {
	for (PendingRelease& release : pending_releases) {
		vkFreeDescriptorSets(context.device, vk_descriptor_pool, 1, &release.descriptor_set);
		graphics::destroyBuffer(release.buffer);
	}

	pending_releases.clear();
}

void createDefaultScene() {
	SceneObject center = makeObject();
	center.scale = { 1.6f, 1.6f, 1.6f };
	center.trajectory = Trajectory::None;
	center.spin_speed = { 12.0f, 30.0f, 0.0f };
	addObject(center);

	SceneObject knot = makeObject();
	knot.trajectory = Trajectory::Trefoil;
	knot.radius_a = 4.5f;
	knot.height = 1.2f;
	knot.speed = 0.5f;
	knot.scale = { 0.6f, 0.6f, 0.6f };
	knot.spin_speed = { 0.0f, 0.0f, 0.0f };
	knot.orient_along_path = true;
	knot.color[0] = 1.0f; knot.color[1] = 1.0f; knot.color[2] = 1.0f;
	addObject(knot);

	SceneObject lissajous = makeObject();
	lissajous.trajectory = Trajectory::Lissajous;
	lissajous.radius_a = 6.5f;
	lissajous.radius_b = 6.5f;
	lissajous.height = 1.5f;
	lissajous.frequency = 2;
	lissajous.phase = 90.0f;
	lissajous.speed = 0.3f;
	lissajous.scale = { 0.5f, 0.5f, 0.9f };
	lissajous.uniform_scale = false;
	lissajous.spin_speed = { 0.0f, 90.0f, 45.0f };
	lissajous.color[0] = 1.0f; lissajous.color[1] = 0.55f; lissajous.color[2] = 0.25f;
	lissajous.use_vertex_colors = false;
	addObject(lissajous);

	selected_object = 0;
}

void setupFont() {
	const char* const fonts[] = {
		"C:/Windows/Fonts/segoeui.ttf",
		"C:/Windows/Fonts/arial.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/TTF/DejaVuSans.ttf",
		"/usr/share/fonts/dejavu/DejaVuSans.ttf",
		"/System/Library/Fonts/Supplemental/Arial.ttf",
	};

	ImGuiIO& io = ImGui::GetIO();

	for (const char* font : fonts) {
		if (std::ifstream(font).good() && io.Fonts->AddFontFromFileTTF(font, 18.0f) != nullptr) {
			return;
		}
	}

	std::cerr << "No font with Cyrillic glyphs found, using ImGui default font\n";
}

math::Vector3 trajectoryPoint(const SceneObject& object, float t) {
	const float a = object.radius_a;
	const float b = object.radius_b;
	const float h = object.height;
	const float k = float(object.frequency);
	const float phi = math::radians(object.phase);

	math::Vector3 p = { 0.0f, 0.0f, 0.0f };

	switch (object.trajectory) {
	case Trajectory::Circle:
		p = { a * std::cos(t), 0.0f, a * std::sin(t) };
		break;

	case Trajectory::Ellipse:
		p = { a * std::cos(t), 0.0f, b * std::sin(t) };
		break;

	case Trajectory::Wave:
		p = { a * std::cos(t), h * std::sin(k * t + phi), a * std::sin(t) };
		break;

	case Trajectory::Lissajous:
		p = { a * std::sin(t + phi), h * std::sin((k + 1.0f) * t), b * std::sin(k * t) };
		break;

	case Trajectory::Trefoil:
		p = {
			a * (std::sin(t) + 2.0f * std::sin(2.0f * t)) / 3.0f,
			-h * std::sin(3.0f * t),
			a * (std::cos(t) - 2.0f * std::cos(2.0f * t)) / 3.0f,
		};
		break;

	case Trajectory::Lemniscate: {
		const float s = std::sin(t), c = std::cos(t);
		const float d = 1.0f + s * s;
		p = { a * c / d, 0.0f, b * s * c / d };
		break;
	}

	case Trajectory::Rose: {
		const float r = a * std::cos(k * t);
		p = { r * std::cos(t), 0.0f, r * std::sin(t) };
		break;
	}

	default:
		break;
	}

	return math::Matrix4::rotationX(math::radians(object.tilt)).transformPoint(p);
}

math::Matrix4 pathOrientation(const SceneObject& object) {
	const float dt = 1e-3f;
	math::Vector3 direction = trajectoryPoint(object, object.t + dt) -
	                          trajectoryPoint(object, object.t - dt);
	if (object.speed < 0.0f) {
		direction = -direction;
	}

	if (math::length(direction) < 1e-6f) {
		return math::Matrix4::identity();
	}

	const math::Vector3 forward = math::normalize(direction);

	math::Vector3 up = { 0.0f, 1.0f, 0.0f };
	if (math::length(math::cross(up, forward)) < 1e-3f) {
		up = { 1.0f, 0.0f, 0.0f };
	}

	const math::Vector3 right = math::normalize(math::cross(up, forward));
	const math::Vector3 new_up = math::cross(forward, right);

	return math::Matrix4::basis(right, new_up, forward);
}

math::Matrix4 modelMatrix(const SceneObject& object) {
	math::Matrix4 orientation = math::Matrix4::identity();
	if (object.orient_along_path && object.trajectory != Trajectory::None) {
		orientation = pathOrientation(object);
	}

	return math::Matrix4::translation(object.position + trajectoryPoint(object, object.t)) *
	       orientation *
	       math::Matrix4::rotationEuler(object.rotation) *
	       math::Matrix4::rotationEuler(object.spin) *
	       math::Matrix4::scale(object.scale);
}

math::Vector3 cameraPosition() {
	const float yaw = math::radians(camera.yaw);
	const float pitch = math::radians(camera.pitch);

	return camera.target + math::Vector3{
		std::cos(pitch) * std::sin(yaw),
		std::sin(pitch),
		std::cos(pitch) * std::cos(yaw),
	} * camera.distance;
}

math::Matrix4 projectionMatrix() {
	const VkExtent2D extent = context.swapchain_extent;
	const float aspect = extent.height > 0 ? float(extent.width) / float(extent.height) : 1.0f;

	if (camera.projection == ProjectionType::Perspective) {
		return math::Matrix4::perspective(math::radians(camera.fov), aspect,
		                                  camera.z_near, camera.z_far);
	}

	const float half_height = camera.ortho_height * 0.5f;
	const float half_width = half_height * aspect;

	return math::Matrix4::orthographic(-half_width, half_width, -half_height, half_height,
	                                   camera.z_near, camera.z_far);
}

void resetCamera() {
	const Camera defaults;
	camera.target = defaults.target;
	camera.yaw = defaults.yaw;
	camera.pitch = defaults.pitch;
	camera.distance = defaults.distance;
}

void toggleProjection() {
	camera.projection = camera.projection == ProjectionType::Perspective
	                  ? ProjectionType::Orthographic
	                  : ProjectionType::Perspective;
}

void addLine(const math::Vector3& a, const math::Vector3& b, const math::Vector3& color) {
	line_vertices.push_back({ a, color });
	line_vertices.push_back({ b, color });
}

void buildLines() {
	line_vertices.clear();

	if (settings.show_grid) {
		const float y = settings.grid_height;
		const float n = float(grid_half_size);

		for (int i = -grid_half_size; i <= grid_half_size; ++i) {
			const float x = float(i);
			const math::Vector3 color = i == 0 ? math::Vector3{ 0.16f, 0.16f, 0.18f }
			                                   : math::Vector3{ 0.06f, 0.06f, 0.07f };
			addLine({ x, y, -n }, { x, y, n }, color);
			addLine({ -n, y, x }, { n, y, x }, color);
		}
	}

	if (settings.show_axes) {
		addLine({ 0.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 0.0f }, { 1.0f, 0.1f, 0.1f });
		addLine({ 0.0f, 0.0f, 0.0f }, { 0.0f, 3.0f, 0.0f }, { 0.1f, 1.0f, 0.1f });
		addLine({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 3.0f }, { 0.1f, 0.3f, 1.0f });
	}

	for (const SceneObject& object : objects) {
		if (!object.show_path || object.trajectory == Trajectory::None) {
			continue;
		}

		const math::Vector3 color = {
			0.25f + 0.45f * object.color[0],
			0.25f + 0.45f * object.color[1],
			0.25f + 0.45f * object.color[2],
		};

		math::Vector3 previous = object.position + trajectoryPoint(object, 0.0f);

		for (uint32_t i = 1; i <= path_segments; ++i) {
			const float t = 2.0f * math::pi * float(i) / float(path_segments);
			const math::Vector3 current = object.position + trajectoryPoint(object, t);
			addLine(previous, current, color);
			previous = current;
		}
	}

	if (line_vertices.size() > max_line_vertices) {
		line_vertices.resize(max_line_vertices);
	}
}

void handleInput() {
	ImGuiIO& io = ImGui::GetIO();

	if (!io.WantCaptureMouse) {
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			camera.yaw -= io.MouseDelta.x * 0.3f;
			camera.pitch = std::clamp(camera.pitch + io.MouseDelta.y * 0.3f, -89.0f, 89.0f);

			if (camera.yaw > 180.0f) camera.yaw -= 360.0f;
			if (camera.yaw < -180.0f) camera.yaw += 360.0f;
		}

		if (io.MouseWheel != 0.0f) {
			camera.distance = std::clamp(camera.distance * std::pow(0.9f, io.MouseWheel), 1.5f, 80.0f);
		}
	}

	if (!io.WantCaptureKeyboard) {
		if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
			settings.playing = !settings.playing;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_P, false)) {
			toggleProjection();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
			resetCamera();
		}
	}
}

void matrixTable(const char* id, const math::Matrix4& m) {
	if (ImGui::BeginTable(id, 4, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame)) {
		for (int r = 0; r < 4; ++r) {
			ImGui::TableNextRow();
			for (int c = 0; c < 4; ++c) {
				ImGui::TableNextColumn();
				ImGui::Text("%8.3f", double(m(r, c)));
			}
		}
		ImGui::EndTable();
	}
}

void cameraUI() {
	if (!ImGui::CollapsingHeader("Проекция и камера", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	int projection = int(camera.projection);
	ImGui::RadioButton("Перспективная", &projection, int(ProjectionType::Perspective));
	ImGui::SameLine();
	ImGui::RadioButton("Ортографическая", &projection, int(ProjectionType::Orthographic));
	camera.projection = ProjectionType(projection);

	if (camera.projection == ProjectionType::Perspective) {
		ImGui::SliderFloat("Угол обзора, °", &camera.fov, 10.0f, 120.0f, "%.0f");
	} else {
		ImGui::Checkbox("Масштаб как у перспективы", &camera.ortho_follow_distance);
		ImGui::BeginDisabled(camera.ortho_follow_distance);
		ImGui::SliderFloat("Высота объёма", &camera.ortho_height, 0.5f, 60.0f, "%.2f");
		ImGui::EndDisabled();
	}

	ImGui::SliderFloat("Ближняя плоскость", &camera.z_near, 0.01f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
	ImGui::SliderFloat("Дальняя плоскость", &camera.z_far, 10.0f, 500.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
	camera.z_far = std::max(camera.z_far, camera.z_near + 0.1f);

	ImGui::SeparatorText("Камера");
	ImGui::SliderFloat("Азимут, °", &camera.yaw, -180.0f, 180.0f, "%.0f");
	ImGui::SliderFloat("Высота, °", &camera.pitch, -89.0f, 89.0f, "%.0f");
	ImGui::SliderFloat("Расстояние", &camera.distance, 1.5f, 80.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
	ImGui::DragFloat3("Точка взгляда", &camera.target.x, 0.05f);
	if (ImGui::Button("Сбросить камеру (R)")) {
		resetCamera();
	}

	ImGui::TextDisabled("Мышь: ЛКМ/ПКМ — вращать, колесо — приблизить");
	ImGui::TextDisabled("Клавиши: P — проекция, Пробел — пауза, R — камера");

	if (ImGui::TreeNode("Матрица проекции P")) {
		matrixTable("##projection", global_uniforms.projection);
		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Видовая матрица V")) {
		matrixTable("##view", global_uniforms.view);
		ImGui::TreePop();
	}
}

void animationUI() {
	if (!ImGui::CollapsingHeader("Анимация", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	if (ImGui::Button(settings.playing ? "Пауза (Пробел)" : "Воспроизвести (Пробел)", ImVec2(200.0f, 0.0f))) {
		settings.playing = !settings.playing;
	}

	ImGui::SameLine();
	if (ImGui::Button("В начало")) {
		for (SceneObject& object : objects) {
			object.t = 0.0f;
			object.spin = { 0.0f, 0.0f, 0.0f };
		}
	}

	ImGui::SliderFloat("Общая скорость", &settings.time_scale, 0.0f, 5.0f, "x%.2f");
}

void objectEditorUI(SceneObject& object) {
	ImGui::PushID(&object);

	ImGui::InputText("Имя", object.name, sizeof(object.name));

	ImGui::SeparatorText("Позиция, поворот, растяжение");
	ImGui::DragFloat3("Позиция", &object.position.x, 0.05f, -20.0f, 20.0f, "%.2f");
	ImGui::SliderFloat3("Поворот, °", &object.rotation.x, -180.0f, 180.0f, "%.0f");

	if (object.uniform_scale) {
		if (ImGui::DragFloat("Растяжение", &object.scale.x, 0.01f, 0.1f, 5.0f, "%.2f")) {
			object.scale.y = object.scale.z = object.scale.x;
		}
	} else {
		ImGui::DragFloat3("Растяжение", &object.scale.x, 0.01f, 0.1f, 5.0f, "%.2f");
	}

	if (ImGui::Checkbox("Равномерное растяжение", &object.uniform_scale) && object.uniform_scale) {
		object.scale.y = object.scale.z = object.scale.x;
	}

	ImGui::SameLine();
	if (ImGui::Button("Сбросить")) {
		object.position = { 0.0f, 0.0f, 0.0f };
		object.rotation = { 0.0f, 0.0f, 0.0f };
		object.scale = { 1.0f, 1.0f, 1.0f };
		object.uniform_scale = true;
	}

	ImGui::SeparatorText("Движение по траектории");

	int trajectory = int(object.trajectory);
	if (ImGui::BeginCombo("Траектория", trajectory_infos[trajectory].name)) {
		for (int i = 0; i < int(Trajectory::Count); ++i) {
			if (ImGui::Selectable(trajectory_infos[i].name, i == trajectory)) {
				object.trajectory = Trajectory(i);
			}
		}
		ImGui::EndCombo();
	}

	const TrajectoryInfo& info = trajectory_infos[int(object.trajectory)];
	ImGui::TextDisabled("%s", info.formula);

	ImGui::Checkbox("Воспроизводить", &object.animate);
	ImGui::SameLine();
	if (ImGui::Button("t = 0")) {
		object.t = 0.0f;
	}
	ImGui::SameLine();
	ImGui::Text("t = %.2f", double(object.t));

	ImGui::SliderFloat("Скорость, рад/с", &object.speed, -3.0f, 3.0f, "%.2f");

	if (info.parameters & UsesA) {
		ImGui::SliderFloat("A (радиус, по X)", &object.radius_a, 0.0f, 10.0f, "%.2f");
	}
	if (info.parameters & UsesB) {
		ImGui::SliderFloat("B (по Z)", &object.radius_b, 0.0f, 10.0f, "%.2f");
	}
	if (info.parameters & UsesH) {
		ImGui::SliderFloat("H (высота, по Y)", &object.height, 0.0f, 5.0f, "%.2f");
	}
	if (info.parameters & UsesK) {
		ImGui::SliderInt("k (частота)", &object.frequency, 1, 8);
	}
	if (info.parameters & UsesPhase) {
		ImGui::SliderFloat("φ (фаза), °", &object.phase, -180.0f, 180.0f, "%.0f");
	}
	if (object.trajectory != Trajectory::None) {
		ImGui::SliderFloat("Наклон плоскости, °", &object.tilt, -90.0f, 90.0f, "%.0f");
	}

	ImGui::SliderFloat3("Вращение, °/с", &object.spin_speed.x, -360.0f, 360.0f, "%.0f");

	ImGui::BeginDisabled(object.trajectory == Trajectory::None);
	ImGui::Checkbox("Поворачивать по касательной", &object.orient_along_path);
	ImGui::SameLine();
	ImGui::Checkbox("Показывать путь", &object.show_path);
	ImGui::EndDisabled();

	ImGui::SeparatorText("Цвет");
	ImGui::ColorEdit3("Цвет фигуры", object.color);
	ImGui::Checkbox("Умножать на цвета вершин", &object.use_vertex_colors);

	if (ImGui::TreeNode("Матрица модели M")) {
		matrixTable("##model", object.uniforms.model);
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void objectsUI() {
	if (!ImGui::CollapsingHeader("Объекты сцены", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	const float list_height = 5.2f * ImGui::GetTextLineHeightWithSpacing();
	if (ImGui::BeginListBox("##objects", ImVec2(-FLT_MIN, list_height))) {
		for (int i = 0; i < int(objects.size()); ++i) {
			ImGui::PushID(i);
			if (ImGui::Selectable(objects[size_t(i)].name, i == selected_object)) {
				selected_object = i;
			}
			ImGui::PopID();
		}
		ImGui::EndListBox();
	}

	const bool full = objects.size() >= max_objects;

	ImGui::BeginDisabled(full);
	if (ImGui::Button("Добавить")) {
		addObject(makeObject());
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(objects.empty());
	if (ImGui::Button("Копировать") && !objects.empty()) {
		SceneObject copy = objects[size_t(selected_object)];
		created_objects++;
		std::snprintf(copy.name, sizeof(copy.name), "Гексаэдр %d", created_objects);
		copy.t += math::pi * 0.5f;
		addObject(copy);
	}
	ImGui::EndDisabled();
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(objects.empty());
	if (ImGui::Button("Удалить") && !objects.empty()) {
		removeObject(selected_object);
	}
	ImGui::EndDisabled();

	ImGui::TextDisabled("Объектов: %d / %u, наборов дескрипторов: %d",
	                    int(objects.size()), max_objects, int(objects.size()) + 2);

	if (!objects.empty()) {
		objectEditorUI(objects[size_t(selected_object)]);
	}
}

void displayUI() {
	if (!ImGui::CollapsingHeader("Отображение")) {
		return;
	}

	int scheme = int(settings.color_scheme);
	if (ImGui::Combo("Цвета вершин", &scheme, hexahedron::color_scheme_names,
	                 int(hexahedron::ColorScheme::Count))) {
		settings.color_scheme = hexahedron::ColorScheme(scheme);
		settings.mesh_dirty = true;
	}

	ImGui::Checkbox("Рёбра", &settings.show_edges);
	ImGui::SameLine();
	ImGui::ColorEdit3("Цвет рёбер", settings.edge_color, ImGuiColorEditFlags_NoInputs);

	ImGui::Checkbox("Сетка", &settings.show_grid);
	ImGui::SameLine();
	ImGui::Checkbox("Оси координат", &settings.show_axes);
	ImGui::SliderFloat("Высота сетки", &settings.grid_height, -10.0f, 10.0f, "%.1f");
	ImGui::ColorEdit3("Фон", settings.background, ImGuiColorEditFlags_NoInputs);
}

void figureUI() {
	if (!ImGui::CollapsingHeader("О фигуре")) {
		return;
	}

	const int v = int(mesh.vertices.size());
	const int e = int(mesh.edges.size() / 2);
	const int f = int(mesh.face_count);

	ImGui::TextWrapped("Правильный гексаэдр (куб) — правильный многогранник {4, 3}: "
	                   "грани — квадраты, в каждой вершине сходятся 3 грани.");
	ImGui::Text("Вершин V = %d, рёбер E = %d, граней F = %d", v, e, f);
	ImGui::Text("Эйлерова характеристика V − E + F = %d", v - e + f);
	ImGui::Text("Треугольников: %d, индексов: %d", int(mesh.triangles.size() / 3),
	            int(mesh.triangles.size()));

	const double a = double(mesh.edge_length);
	ImGui::Text("Ребро a = %.3f", a);
	ImGui::Text("Диагональ грани a√2 = %.3f", a * std::sqrt(2.0));
	ImGui::Text("Диагональ куба a√3 = %.3f", a * std::sqrt(3.0));
	ImGui::Text("Радиус описанной сферы a√3/2 = %.3f", a * std::sqrt(3.0) / 2.0);
	ImGui::Text("Радиус вписанной сферы a/2 = %.3f", a / 2.0);
	ImGui::Text("Двугранный угол = 90°");
}

void statusOverlay() {
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 10.0f,
	                               viewport->WorkPos.y + 10.0f),
	                        ImGuiCond_Always, ImVec2(1.0f, 0.0f));
	ImGui::SetNextWindowBgAlpha(0.45f);

	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
	                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
	                               ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

	if (ImGui::Begin("##status", nullptr, flags)) {
		ImGui::Text("%s проекция", camera.projection == ProjectionType::Perspective
		                           ? "Перспективная" : "Ортографическая");
		ImGui::Text("%s", settings.playing ? "Анимация идёт" : "Пауза");
		ImGui::Text("%.0f FPS", double(ImGui::GetIO().Framerate));
	}
	ImGui::End();
}

void drawUI() {
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 10.0f, viewport->WorkPos.y + 10.0f),
	                        ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(420.0f, viewport->WorkSize.y - 20.0f), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Лабораторная работа №1")) {
		ImGui::PushItemWidth(-ImGui::GetFontSize() * 9.5f);

		ImGui::Text("Вариант 12: правильный гексаэдр");
		ImGui::Separator();

		cameraUI();
		animationUI();
		objectsUI();
		displayUI();
		figureUI();

		ImGui::PopItemWidth();
	}
	ImGui::End();

	statusOverlay();
}

bool createResources() {
	const VkDescriptorSetLayoutBinding descriptor_set_bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		},
	};

	const VkDescriptorSetLayoutCreateInfo descriptor_set_layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = sizeof(descriptor_set_bindings) / sizeof(descriptor_set_bindings[0]),
		.pBindings = descriptor_set_bindings,
	};

	if (vkCreateDescriptorSetLayout(context.device, &descriptor_set_layout, nullptr,
	                                &vk_global_set_layout) != VK_SUCCESS ||
	    vkCreateDescriptorSetLayout(context.device, &descriptor_set_layout, nullptr,
	                                &vk_object_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor set layout\n";
		return false;
	}

	const VkDescriptorSetLayout set_layouts[] = {
		vk_global_set_layout,
		vk_object_set_layout,
	};

	const VkPushConstantRange push_constant_range = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.offset = 0,
		.size = sizeof(PushConstants),
	};

	const VkPipelineLayoutCreateInfo pipeline_layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = sizeof(set_layouts) / sizeof(set_layouts[0]),
		.pSetLayouts = set_layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push_constant_range,
	};

	if (vkCreatePipelineLayout(context.device, &pipeline_layout, nullptr,
	                           &vk_pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create pipeline layout\n";
		return false;
	}

	VkShaderModule vertex_shader = graphics::loadShaderModule("scene.vert.spv");
	VkShaderModule fragment_shader = graphics::loadShaderModule("scene.frag.spv");

	if (vertex_shader != VK_NULL_HANDLE && fragment_shader != VK_NULL_HANDLE) {
		const VkVertexInputBindingDescription vertex_bindings[] = {
			{
				.binding = 0,
				.stride = sizeof(Vertex),
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
			},
		};

		const VkVertexInputAttributeDescription vertex_attributes[] = {
			{
				.location = 0,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, position),
			},
			{
				.location = 1,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, color),
			},
		};

		graphics::PipelineDescription pipeline = {
			.vertex_shader = vertex_shader,
			.fragment_shader = fragment_shader,
			.layout = vk_pipeline_layout,
			.vertex_bindings = vertex_bindings,
			.vertex_binding_count = sizeof(vertex_bindings) / sizeof(vertex_bindings[0]),
			.vertex_attributes = vertex_attributes,
			.vertex_attribute_count = sizeof(vertex_attributes) / sizeof(vertex_attributes[0]),
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			.cull_mode = VK_CULL_MODE_BACK_BIT,
			.depth_bias = true,
		};

		vk_pipeline_triangles = graphics::createGraphicsPipeline(pipeline);

		pipeline.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		pipeline.cull_mode = VK_CULL_MODE_NONE;
		pipeline.depth_bias = false;
		vk_pipeline_lines = graphics::createGraphicsPipeline(pipeline);
	}

	vkDestroyShaderModule(context.device, vertex_shader, nullptr);
	vkDestroyShaderModule(context.device, fragment_shader, nullptr);

	if (vk_pipeline_triangles == VK_NULL_HANDLE || vk_pipeline_lines == VK_NULL_HANDLE) {
		return false;
	}

	const VkDescriptorPoolSize descriptor_pool_sizes[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = max_descriptor_sets,
		},
	};

	const VkDescriptorPoolCreateInfo descriptor_pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = max_descriptor_sets,
		.poolSizeCount = sizeof(descriptor_pool_sizes) / sizeof(descriptor_pool_sizes[0]),
		.pPoolSizes = descriptor_pool_sizes,
	};

	if (vkCreateDescriptorPool(context.device, &descriptor_pool, nullptr,
	                           &vk_descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor pool\n";
		return false;
	}

	if (!graphics::createBuffer(sizeof(GlobalUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                            global_uniform_buffer)) {
		return false;
	}

	vk_global_descriptor_set = allocateUniformSet(vk_global_set_layout, global_uniform_buffer,
	                                              sizeof(GlobalUniforms));
	if (vk_global_descriptor_set == VK_NULL_HANDLE) {
		return false;
	}

	if (!graphics::createBuffer(sizeof(ObjectUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                            world_uniform_buffer)) {
		return false;
	}

	vk_world_descriptor_set = allocateUniformSet(vk_object_set_layout, world_uniform_buffer,
	                                             sizeof(ObjectUniforms));
	if (vk_world_descriptor_set == VK_NULL_HANDLE) {
		return false;
	}

	const ObjectUniforms world_uniforms = {
		.model = math::Matrix4::identity(),
		.color = { 1.0f, 1.0f, 1.0f, 1.0f },
		.use_vertex_colors = 1.0f,
	};
	graphics::writeBuffer(world_uniform_buffer, &world_uniforms, sizeof(world_uniforms));

	mesh = hexahedron::build(1.0f);
	hexahedron::paint(mesh, settings.color_scheme);

	const size_t vertices_size = mesh.vertices.size() * sizeof(Vertex);
	const size_t triangles_size = mesh.triangles.size() * sizeof(uint32_t);
	const size_t edges_size = mesh.edges.size() * sizeof(uint32_t);

	if (!graphics::createBuffer(vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer) ||
	    !graphics::createBuffer(triangles_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, triangle_index_buffer) ||
	    !graphics::createBuffer(edges_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, edge_index_buffer) ||
	    !graphics::createBuffer(max_line_vertices * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	                            line_vertex_buffer)) {
		return false;
	}

	graphics::writeBuffer(vertex_buffer, mesh.vertices.data(), vertices_size);
	graphics::writeBuffer(triangle_index_buffer, mesh.triangles.data(), triangles_size);
	graphics::writeBuffer(edge_index_buffer, mesh.edges.data(), edges_size);

	return true;
}

} // namespace

bool initialize() {
	setupFont();

	if (!createResources()) {
		shutdown();
		return false;
	}

	createDefaultScene();

	return true;
}

void shutdown() {
	vkQueueWaitIdle(context.graphics_queue);

	for (PendingRelease& release : pending_releases) {
		graphics::destroyBuffer(release.buffer);
	}
	pending_releases.clear();

	for (SceneObject& object : objects) {
		graphics::destroyBuffer(object.uniform_buffer);
	}
	objects.clear();

	graphics::destroyBuffer(line_vertex_buffer);
	graphics::destroyBuffer(edge_index_buffer);
	graphics::destroyBuffer(triangle_index_buffer);
	graphics::destroyBuffer(vertex_buffer);
	graphics::destroyBuffer(world_uniform_buffer);
	graphics::destroyBuffer(global_uniform_buffer);

	vkDestroyDescriptorPool(context.device, vk_descriptor_pool, nullptr);
	vkDestroyPipeline(context.device, vk_pipeline_lines, nullptr);
	vkDestroyPipeline(context.device, vk_pipeline_triangles, nullptr);
	vkDestroyPipelineLayout(context.device, vk_pipeline_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_object_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_global_set_layout, nullptr);

	vk_descriptor_pool = VK_NULL_HANDLE;
	vk_pipeline_lines = VK_NULL_HANDLE;
	vk_pipeline_triangles = VK_NULL_HANDLE;
	vk_pipeline_layout = VK_NULL_HANDLE;
	vk_object_set_layout = VK_NULL_HANDLE;
	vk_global_set_layout = VK_NULL_HANDLE;
}

void update(double time) {
	const float dt = last_time < 0.0 ? 0.0f : float(std::min(time - last_time, 0.1));
	last_time = time;

	handleInput();
	drawUI();

	if (settings.playing) {
		const float step = dt * settings.time_scale;

		for (SceneObject& object : objects) {
			if (!object.animate) {
				continue;
			}

			object.t = std::fmod(object.t + object.speed * step, 2.0f * math::pi);
			if (object.t < 0.0f) {
				object.t += 2.0f * math::pi;
			}

			object.spin += object.spin_speed * step;
			object.spin = {
				std::fmod(object.spin.x, 360.0f),
				std::fmod(object.spin.y, 360.0f),
				std::fmod(object.spin.z, 360.0f),
			};
		}
	}

	if (camera.ortho_follow_distance) {
		camera.ortho_height = 2.0f * camera.distance * std::tan(math::radians(camera.fov) * 0.5f);
	}

	global_uniforms.view = math::Matrix4::lookAt(cameraPosition(), camera.target, { 0.0f, 1.0f, 0.0f });
	global_uniforms.projection = projectionMatrix();

	for (SceneObject& object : objects) {
		object.uniforms = {
			.model = modelMatrix(object),
			.color = { object.color[0], object.color[1], object.color[2], 1.0f },
			.use_vertex_colors = object.use_vertex_colors ? 1.0f : 0.0f,
		};
	}

	buildLines();
}

void render(const graphics::internal::FrameData& fd) {
	if (fd.command_buffer == VK_NULL_HANDLE) {
		return;
	}

	releasePending();

	if (settings.mesh_dirty) {
		hexahedron::paint(mesh, settings.color_scheme);
		graphics::writeBuffer(vertex_buffer, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));
		settings.mesh_dirty = false;
	}

	graphics::writeBuffer(global_uniform_buffer, &global_uniforms, sizeof(global_uniforms));

	for (const SceneObject& object : objects) {
		graphics::writeBuffer(object.uniform_buffer, &object.uniforms, sizeof(object.uniforms));
	}

	const uint32_t line_vertex_count = uint32_t(line_vertices.size());
	if (line_vertex_count > 0) {
		graphics::writeBuffer(line_vertex_buffer, line_vertices.data(), line_vertex_count * sizeof(Vertex));
	}

	VkCommandBuffer cmd = fd.command_buffer;

	vkResetCommandBuffer(cmd, 0);

	const VkCommandBufferBeginInfo command_buffer_begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkBeginCommandBuffer(cmd, &command_buffer_begin);

	const VkClearValue clear_values[] = {
		{ .color = { .float32 = { settings.background[0], settings.background[1],
		                          settings.background[2], 1.0f } } },
		{ .depthStencil = { 1.0f, 0 } },
	};

	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = { .extent = context.swapchain_extent },
		.clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
		.pClearValues = clear_values,
	};

	vkCmdBeginRenderPass(cmd, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

	const VkViewport viewport = {
		.x = 0, .y = 0,
		.width = float(context.swapchain_extent.width),
		.height = float(context.swapchain_extent.height),
		.minDepth = 0, .maxDepth = 1,
	};

	const VkRect2D scissor = { .extent = context.swapchain_extent };

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout,
	                        0, 1, &vk_global_descriptor_set, 0, nullptr);

	const PushConstants no_override = {};
	vkCmdPushConstants(cmd, vk_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
	                   0, sizeof(no_override), &no_override);

	const VkDeviceSize vertex_buffer_offset = 0;

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_triangles);
	vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer.buffer, &vertex_buffer_offset);
	vkCmdBindIndexBuffer(cmd, triangle_index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	for (const SceneObject& object : objects) {
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout,
		                        1, 1, &object.descriptor_set, 0, nullptr);
		vkCmdDrawIndexed(cmd, uint32_t(mesh.triangles.size()), 1, 0, 0, 0);
	}

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_lines);

	if (settings.show_edges) {
		const PushConstants edge_override = {
			.override_color = { settings.edge_color[0], settings.edge_color[1],
			                    settings.edge_color[2], 1.0f },
		};
		vkCmdPushConstants(cmd, vk_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
		                   0, sizeof(edge_override), &edge_override);
		vkCmdBindIndexBuffer(cmd, edge_index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		for (const SceneObject& object : objects) {
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout,
			                        1, 1, &object.descriptor_set, 0, nullptr);
			vkCmdDrawIndexed(cmd, uint32_t(mesh.edges.size()), 1, 0, 0, 0);
		}

		vkCmdPushConstants(cmd, vk_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
		                   0, sizeof(no_override), &no_override);
	}

	if (line_vertex_count > 0) {
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout,
		                        1, 1, &vk_world_descriptor_set, 0, nullptr);
		vkCmdBindVertexBuffers(cmd, 0, 1, &line_vertex_buffer.buffer, &vertex_buffer_offset);
		vkCmdDraw(cmd, line_vertex_count, 1, 0, 0);
	}

	vkCmdEndRenderPass(cmd);

	vkEndCommandBuffer(cmd);
}

} // namespace application
