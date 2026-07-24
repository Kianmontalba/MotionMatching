#include "animation_node_motion_matching.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void AnimationNodeMotionMatching::set_controller_path(const NodePath &p_path) {
	_controller_path = p_path;
}

void AnimationNodeMotionMatching::bind_controller(MotionMatchingController *p_controller) {
	_controller = p_controller;
	_controller_id = p_controller != nullptr ? p_controller->get_instance_id() : ObjectID();
}

MotionMatchingController *AnimationNodeMotionMatching::_resolve_controller() const {
	// The controller registers itself in _ready(). Re-resolving through the
	// instance id keeps the pointer safe if the node was freed.
	if (_controller != nullptr && ObjectDB::get_instance(_controller_id) != nullptr) {
		return _controller;
	}
	_controller = nullptr;
	return nullptr;
}

String AnimationNodeMotionMatching::_get_caption() const {
	return "MotionMatching";
}

bool AnimationNodeMotionMatching::_has_filter() const {
	return true;
}

// Blends the outgoing and incoming clips at absolute times supplied by the
// controller. blend_animation() taking an absolute time is exactly what makes
// a frame jump possible: the clip is entered at 4.7 seconds, not at zero.
double AnimationNodeMotionMatching::_process(double p_time, bool p_seek, bool p_is_external_seeking,
		bool p_test_only) const {
	MotionMatchingController *controller = _resolve_controller();
	if (controller == nullptr) {
		return 0.0;
	}

	const StringName current = controller->get_current_clip();
	if (current == StringName()) {
		return 0.0;
	}

	const double current_time = controller->get_current_time();
	const double previous_time = controller->get_previous_time();
	const StringName previous = controller->get_previous_clip();
	const float weight = controller->get_blend_weight();
	const bool seeked = controller->take_seek_request() || p_seek;

	// The controller owns the timeline, so the tree is always told to seek to
	// the authoritative time rather than integrating its own delta. This keeps
	// the pose and the root motion reading from the same clock.
	const double delta = p_time;

	// blend_animation() is inherited from AnimationNode and is not declared
	// const upstream, even though it only submits weighted samples through
	// the mixer rather than mutating this node's own state. The const_cast
	// is safe for that reason; it does not modify any member of this class.
	AnimationNodeMotionMatching *mutable_self = const_cast<AnimationNodeMotionMatching *>(this);

	if (previous != StringName() && weight < 1.0f) {
		mutable_self->blend_animation(previous, previous_time, delta, true, p_is_external_seeking, 1.0f - weight,
				Animation::LOOPED_FLAG_NONE);
	}

	mutable_self->blend_animation(current, current_time, delta, seeked, p_is_external_seeking, weight,
			Animation::LOOPED_FLAG_NONE);

	_last_time = current_time;
	return current_time;
}

void AnimationNodeMotionMatching::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_controller_path", "path"),
			&AnimationNodeMotionMatching::set_controller_path);
	ClassDB::bind_method(D_METHOD("get_controller_path"),
			&AnimationNodeMotionMatching::get_controller_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "controller_path"), "set_controller_path",
			"get_controller_path");

	MM_BIND_PROPERTY(AnimationNodeMotionMatching, Variant::BOOL, use_custom_timeline)
}
