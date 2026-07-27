#include "debug.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

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

void MMDebugDraw::_draw_circle(const Vector3 &p_center, float p_radius, const Color &p_color) {
	constexpr int segments = 10;
	Vector3 previous = p_center + Vector3(p_radius, 0, 0);
	for (int i = 1; i <= segments; i++) {
		const float angle = (float)i / (float)segments * (float)Math_TAU;
		const Vector3 next = p_center + Vector3(Math::cos(angle) * p_radius, 0, Math::sin(angle) * p_radius);
		_draw_line(previous, next, p_color);
		previous = next;
	}
}

void MMDebugDraw::_draw_arrow(const Vector3 &p_from, const Vector3 &p_direction, float p_length,
		const Color &p_color) {
	if (p_direction.length_squared() < 0.000001f) {
		return;
	}
	const Vector3 direction = p_direction.normalized();
	const Vector3 tip = p_from + direction * p_length;
	_draw_line(p_from, tip, p_color);

	// Two short back-angled strokes for the arrowhead, same trick used to
	// draw an arrow in 2D -- just kept flat on the XZ plane here since every
	// sample direction this is used for is already horizontal.
	const Vector3 side = direction.cross(Vector3(0, 1, 0)).normalized() * (p_length * 0.28f);
	const Vector3 back = tip - direction * (p_length * 0.35f);
	_draw_line(tip, back + side, p_color);
	_draw_line(tip, back - side, p_color);
}

void MMDebugDraw::_ensure_label_pool(int p_count) {
	while (_label_pool.size() < p_count) {
		Label3D *label = memnew(Label3D);
		label->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
		label->set_no_depth_test(true);
		label->set_font_size(_label_font_size);
		label->set_modulate(_label_color);
		add_child(label);
		_label_pool.push_back(label);
	}
}

void MMDebugDraw::_hide_labels_from(int p_visible_count) {
	for (int i = p_visible_count; i < _label_pool.size(); i++) {
		_label_pool[i]->set_visible(false);
	}
}

void MMDebugDraw::_process(double p_delta) {
	_mesh->clear_surfaces();
	MotionMatchingController *controller = _resolve_controller();
	if (controller == nullptr || !_draw_trajectory) {
		_hide_labels_from(0);
		return;
	}

	const PackedVector3Array points = controller->get_debug_trajectory();
	if (points.size() < 2) {
		_hide_labels_from(0);
		return;
	}

	Ref<MMTrajectory> trajectory = controller->get_trajectory();
	// All three of these are paired 1:1 with points -- same
	// history-then-current-then-future order, same length -- so index i's
	// direction/velocity/time is always index i's own point.
	const PackedVector3Array directions = trajectory.is_valid() ? trajectory->get_debug_directions()
																  : PackedVector3Array();
	const PackedVector3Array velocities = trajectory.is_valid() ? trajectory->get_debug_velocities()
																  : PackedVector3Array();
	const PackedFloat32Array times = trajectory.is_valid() ? trajectory->get_debug_times()
															 : PackedFloat32Array();

	_mesh->surface_begin(Mesh::PRIMITIVE_LINES, _material);

	// The mesh is a child of the character, so world space points have to come
	// back into local space or the line will double up the character's motion.
	const Transform3D inverse = get_global_transform().affine_inverse();
	const Vector3 lift = Vector3(0, _floor_offset, 0);

	const int future_count = trajectory.is_valid() ? trajectory->get_sample_count() : 0;
	const int split = MAX(0, points.size() - future_count);

	for (int i = 0; i < points.size() - 1; i++) {
		const Color color = i < split - 1 ? _history_color : _future_color;
		_draw_line(inverse.xform(points[i] + lift), inverse.xform(points[i + 1] + lift), color);
	}

	if (_draw_frame_labels) {
		_ensure_label_pool(points.size());
	}

	// Every sample gets a circle, and -- per the reference spec -- every
	// circle gets its own forward arrow, not just the predicted half of the
	// line. History circles use _history_color, current and future use
	// _future_color, same split the line above already uses. The velocity
	// arrow is drawn alongside the facing arrow rather than instead of it:
	// during a straight run the two overlap and nothing is lost, but during
	// a strafe, a dodge, or a fast swing they visibly split apart, which is
	// the whole point of drawing both.
	for (int i = 0; i < points.size(); i++) {
		const Color color = i < split ? _history_color : _future_color;
		const Vector3 local_point = inverse.xform(points[i] + lift);
		_draw_circle(local_point, _marker_size, color);

		if (_draw_facing && i < directions.size()) {
			const Vector3 local_direction = inverse.basis.xform(directions[i]);
			_draw_arrow(local_point, local_direction, _forward_arrow_length, color);
		}

		if (_draw_velocity && i < velocities.size()) {
			const Vector3 local_velocity = inverse.basis.xform(velocities[i]);
			const float speed = local_velocity.length();
			const float arrow_length = MIN(speed * _velocity_arrow_scale, _velocity_arrow_max_length);
			if (arrow_length > 0.001f) {
				_draw_arrow(local_point, local_velocity, arrow_length, _velocity_color);
			}
		}

		if (_draw_frame_labels && i < _label_pool.size()) {
			Label3D *label = _label_pool[i];
			label->set_visible(true);
			label->set_position(local_point + Vector3(0, _label_height_offset, 0));
			label->set_modulate(_label_color);
			label->set_font_size(_label_font_size);
			const float time = i < times.size() ? times[i] : 0.0f;
			label->set_text(vformat("%d | %+.2fs", i, time));
		}
	}

	if (_draw_frame_labels) {
		_hide_labels_from(points.size());
	} else {
		_hide_labels_from(0);
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
	MM_BIND_PROPERTY(MMDebugDraw, Variant::BOOL, draw_velocity)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::BOOL, draw_frame_labels)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::FLOAT, marker_size)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::FLOAT, floor_offset)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::FLOAT, forward_arrow_length)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::FLOAT, velocity_arrow_scale)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::FLOAT, velocity_arrow_max_length)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::FLOAT, label_height_offset)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::INT, label_font_size)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::COLOR, history_color)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::COLOR, future_color)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::COLOR, matched_color)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::COLOR, velocity_color)
	MM_BIND_PROPERTY(MMDebugDraw, Variant::COLOR, label_color)
}
