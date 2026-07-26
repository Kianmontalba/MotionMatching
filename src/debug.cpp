#include "debug.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

MMDebugDraw::MMDebugDraw() {
	_mesh.instantiate();
	_material.instantiate();
	_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	_material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
	_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	set_mesh(_mesh);
	set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
}

void MMDebugDraw::set_controller_path(const NodePath &p_path) {
	_controller_path = p_path;
	if (is_inside_tree()) {
		_controller = Object::cast_to<MotionMatchingController>(get_node_or_null(p_path));
		_controller_id = _controller != nullptr ? _controller->get_instance_id() : ObjectID();
	}
}

void MMDebugDraw::_ready() {
	if (!_controller_path.is_empty()) {
		_controller = Object::cast_to<MotionMatchingController>(get_node_or_null(_controller_path));
		_controller_id = _controller != nullptr ? _controller->get_instance_id() : ObjectID();
	}
	set_process(!Engine::get_singleton()->is_editor_hint());
}

MotionMatchingController *MMDebugDraw::_resolve_controller() const {
	if (_controller != nullptr && ObjectDB::get_instance(_controller_id) != nullptr) {
		return _controller;
	}
	_controller = nullptr;
	return nullptr;
}

void MMDebugDraw::_draw_line(const Vector3 &p_from, const Vector3 &p_to, const Color &p_color) {
	_mesh->surface_set_color(p_color);
	_mesh->surface_add_vertex(p_from);
	_mesh->surface_set_color(p_color);
	_mesh->surface_add_vertex(p_to);
}

void MMDebugDraw::_draw_marker(const Vector3 &p_position, float p_size, const Color &p_color) {
	_draw_line(p_position - Vector3(p_size, 0, 0), p_position + Vector3(p_size, 0, 0), p_color);
	_draw_line(p_position - Vector3(0, 0, p_size), p_position + Vector3(0, 0, p_size), p_color);
	_draw_line(p_position, p_position + Vector3(0, p_size * 2.0f, 0), p_color);
}

void MMDebugDraw::_process(double p_delta) {
	_mesh->clear_surfaces();
	MotionMatchingController *controller = _resolve_controller();
	if (controller == nullptr || !_draw_trajectory) {
		return;
	}

	const PackedVector3Array points = controller->get_debug_trajectory();
	if (points.size() < 2) {
		return;
	}

	_mesh->surface_begin(Mesh::PRIMITIVE_LINES, _material);

	// The mesh is a child of the character, so world space points have to come
	// back into local space or the line will double up the character's motion.
	const Transform3D inverse = get_global_transform().affine_inverse();

	Ref<MMTrajectory> trajectory = controller->get_trajectory();
	const int future_count = trajectory.is_valid() ? trajectory->get_sample_count() : 0;
	const int split = MAX(0, points.size() - future_count);

	for (int i = 0; i < points.size() - 1; i++) {
		const Color color = i < split - 1 ? _history_color : _future_color;
		_draw_line(inverse.xform(points[i]), inverse.xform(points[i + 1]), color);
	}

	for (int i = split; i < points.size(); i++) {
		_draw_marker(inverse.xform(points[i]), _marker_size, _future_color);
	}

	if (_draw_facing && trajectory.is_valid()) {
		for (int i = 0; i < future_count; i++) {
			const Vector3 origin = inverse.xform(trajectory->get_sample_position(i));
			const Vector3 direction = inverse.basis.xform(trajectory->get_sample_direction(i));
			_draw_line(origin, origin + direction * 0.25f, _matched_color);
		}
	}

	_mesh->surface_end();
}

void MMDebugDraw::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_controller_path", "path"), &MMDebugDraw::set_controller_path);
	ClassDB::bind_method(D_METHOD("get_controller_path"), &MMDebugDraw::get_controller_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "controller_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES,
						  "MotionMatchingController"),
			"set_controller_path", "get_controller_path");

	MM_BIND_PROPERTY(MMDebugDraw, Variant::BOOL, draw_trajectory)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::BOOL, draw_matched_trajectory)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::BOOL, draw_facing)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::FLOAT, marker_size)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::COLOR, history_color)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::COLOR, future_color)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::COLOR, matched_color)
}
