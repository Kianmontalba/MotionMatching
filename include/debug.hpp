#ifndef MM_DEBUG_HPP
#define MM_DEBUG_HPP

#include "mm_types.hpp"
#include "motion_matching.hpp"

#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// MMDebugDraw
//
// The trajectory debug draw, in the style every AAA motion matching tool ships
// with: past line behind, predicted line ahead, a marker per sample, and the
// matched clip's own trajectory drawn on top so a mismatch is visible instead
// of merely suspected.
// ---------------------------------------------------------------------------
class MMDebugDraw : public MeshInstance3D {
	GDCLASS(MMDebugDraw, MeshInstance3D);

private:
	NodePath _controller_path;
	mutable MotionMatchingController *_controller = nullptr;
	ObjectID _controller_id;
	Ref<ImmediateMesh> _mesh;
	Ref<StandardMaterial3D> _material;

	bool _draw_trajectory = true;
	bool _draw_matched_trajectory = true;
	bool _draw_facing = true;
	float _marker_size = 0.06f;
	// How far above the raw trajectory points to draw everything. The raw
	// points sit at the character's root height, which on most rigs is at
	// or near the ground -- without this lift the whole debug draw reads as
	// buried in the floor even with depth testing disabled.
	float _floor_offset = 0.05f;
	// Length of the small forward-direction arrow drawn off every circle.
	float _forward_arrow_length = 0.18f;
	Color _history_color = Color(0.35f, 0.55f, 1.0f);
	Color _future_color = Color(0.2f, 1.0f, 0.5f);
	Color _matched_color = Color(1.0f, 0.65f, 0.15f);

	void _draw_line(const Vector3 &p_from, const Vector3 &p_to, const Color &p_color);
	// Flat ring in the XZ plane -- this is the "O" in the O----> style every
	// AAA motion matching debug view uses per sample point.
	void _draw_circle(const Vector3 &p_center, float p_radius, const Color &p_color);
	// The "---->" half: a line with a small two-stroke arrowhead, used both
	// for the per-point forward arrow and (scaled differently) anywhere else
	// a direction needs to read clearly from a top-down or side angle.
	void _draw_arrow(const Vector3 &p_from, const Vector3 &p_direction, float p_length,
			const Color &p_color);
	// The controller registers itself once at _ready(); re-checking through
	// its ObjectID on every use (same pattern as
	// AnimationNodeMotionMatching::_resolve_controller()) means a controller
	// freed later in the scene's life turns this back into a plain null
	// instead of leaving a dangling MotionMatchingController* behind.
	MotionMatchingController *_resolve_controller() const;

protected:
	static void _bind_methods();

public:
	MMDebugDraw();

	void set_controller_path(const NodePath &p_path);
	NodePath get_controller_path() const { return _controller_path; }

	MM_ACCESSORS(bool, draw_trajectory)
	MM_ACCESSORS(bool, draw_matched_trajectory)
	MM_ACCESSORS(bool, draw_facing)
	MM_ACCESSORS(float, marker_size)
	MM_ACCESSORS(float, floor_offset)
	MM_ACCESSORS(float, forward_arrow_length)
	MM_ACCESSORS(Color, history_color)
	MM_ACCESSORS(Color, future_color)
	MM_ACCESSORS(Color, matched_color)

	void _ready() override;
	void _process(double p_delta) override;
};

} // namespace godot

#endif // MM_DEBUG_HPP
