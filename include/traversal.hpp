#ifndef MM_TRAVERSAL_HPP
#define MM_TRAVERSAL_HPP

#include "mm_types.hpp"
#include "trajectory.hpp"

#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// MMTraversal
//
// Traversal is not a separate animation system. It is a query that turns
// geometry into a tag filter plus a warp target, then hands both back to the
// same search that drives locomotion.
//
// Obstacles are probed along the predicted trajectory rather than straight
// ahead, so a character that is about to turn tests the wall it will actually
// meet.
// ---------------------------------------------------------------------------
class MMTraversal : public RefCounted {
	GDCLASS(MMTraversal, RefCounted);

public:
	enum TraversalType {
		TRAVERSAL_NONE = 0,
		TRAVERSAL_STEP,
		TRAVERSAL_LOW_VAULT,
		TRAVERSAL_HIGH_VAULT,
		TRAVERSAL_MANTLE,
		TRAVERSAL_CLIMB,
		TRAVERSAL_SLIDE_UNDER
	};

private:
	float _max_probe_distance = 2.5f;
	float _character_height = 1.8f;
	float _step_height = 0.35f;
	float _low_vault_height = 0.9f;
	float _high_vault_height = 1.4f;
	float _mantle_height = 2.2f;
	float _max_obstacle_depth = 1.2f;
	uint32_t _collision_mask = 1;

	TraversalType _detected = TRAVERSAL_NONE;
	Vector3 _entry_point;
	Vector3 _top_point;
	Vector3 _exit_point;
	float _obstacle_height = 0.0f;
	float _obstacle_depth = 0.0f;
	float _surface_angle = 0.0f;

protected:
	static void _bind_methods();

public:
	MM_ACCESSORS(float, max_probe_distance)
	MM_ACCESSORS(float, character_height)
	MM_ACCESSORS(float, step_height)
	MM_ACCESSORS(float, low_vault_height)
	MM_ACCESSORS(float, high_vault_height)
	MM_ACCESSORS(float, mantle_height)
	MM_ACCESSORS(float, max_obstacle_depth)

	void set_collision_mask(int p_mask) { _collision_mask = (uint32_t)p_mask; }
	int get_collision_mask() const { return (int)_collision_mask; }

	// Probes along the trajectory and classifies what was found.
	TraversalType probe(PhysicsDirectSpaceState3D *p_space, const Transform3D &p_character,
			const Ref<MMTrajectory> &p_trajectory);

	TraversalType get_detected_type() const { return _detected; }
	void clear();

	// Tag mask the search should require for the detected traversal type.
	int get_required_tags() const;

	Vector3 get_entry_point() const { return _entry_point; }
	Vector3 get_top_point() const { return _top_point; }
	Vector3 get_exit_point() const { return _exit_point; }
	float get_obstacle_height() const { return _obstacle_height; }
	float get_obstacle_depth() const { return _obstacle_depth; }

	Dictionary get_result() const;
};

} // namespace godot

VARIANT_ENUM_CAST(godot::MMTraversal::TraversalType);

#endif // MM_TRAVERSAL_HPP
