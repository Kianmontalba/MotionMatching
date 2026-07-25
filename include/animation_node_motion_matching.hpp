#ifndef MM_ANIMATION_NODE_HPP
#define MM_ANIMATION_NODE_HPP

#include "motion_matching.hpp"

#include <godot_cpp/classes/animation_root_node.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// AnimationNodeMotionMatching
//
// Drop this in as the root of an AnimationTree, or as a node inside a
// BlendTree, and the tree plays whatever the controller decided this tick.
//
// Two clips are blended: the one being left and the one being entered. Because
// blend_animation() takes an absolute time, a match can start at 4.7 seconds
// into a clip instead of at zero, which is the whole point of motion matching
// and the thing a state machine cannot do.
// ---------------------------------------------------------------------------
class AnimationNodeMotionMatching : public AnimationRootNode {
	GDCLASS(AnimationNodeMotionMatching, AnimationRootNode);

private:
	NodePath _controller_path;
	ObjectID _controller_id;
	mutable MotionMatchingController *_controller = nullptr;
	bool _use_custom_timeline = true;
	mutable double _last_time = 0.0;

	MotionMatchingController *_resolve_controller() const;

protected:
	static void _bind_methods();

public:
	void set_controller_path(const NodePath &p_path);
	NodePath get_controller_path() const { return _controller_path; }

	// Called by the controller in _ready() so the node never has to search the
	// scene tree during animation processing.
	void bind_controller(MotionMatchingController *p_controller);

	MM_ACCESSORS(bool, use_custom_timeline)

	String _get_caption() const override;
	bool _has_filter() const override;

	double _process(double p_time, bool p_seek, bool p_is_external_seeking, bool p_test_only) const override;
};

} // namespace godot

#endif // MM_ANIMATION_NODE_HPP
