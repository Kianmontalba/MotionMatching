#ifndef MM_DEBUG_HPP
#define MM_DEBUG_HPP

#include "mm_types.hpp"
#include "motion_matching.hpp"

#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/label3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/templates/vector.hpp>

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
	// Facing shows which way the character is oriented; velocity shows
	// which way it is actually travelling. During a strafe, a dodge, or a
	// fast swing the two point in different directions, and drawing only
	// facing was hiding exactly the mismatch a debug view exists to show.
	bool _draw_velocity = true;
	// Numbers floating above each point: the sample index and the time (in
	// seconds, negative for history) that sample contributed to the
	// feature vector -- i.e. which "frame" of the query that point is.
	bool _draw_frame_labels = true;
	// Colors each point's circle by confidence tier (green/yellow/red)
	// instead of the plain history/future split. The line and arrows keep
	// their usual colors either way -- only the circle changes, since that
	// is the one element that reads cleanly as a single "state" per point.
	bool _draw_confidence_tier = false;
	float _confidence_green_threshold = 0.8f;
	float _confidence_yellow_threshold = 0.5f;
	// Draws the current traversal probe's entry/top/exit points (if any is
	// assigned to the controller and currently detects something) as a
	// separate marker, plus its type name on the frame label of the nearest
	// trajectory point.
	bool _draw_traversal_preview = true;
	float _marker_size = 0.06f;
	// How far above the raw trajectory points to draw everything. The raw
	// points sit at the character's root height, which on most rigs is at
	// or near the ground -- without this lift the whole debug draw reads as
	// buried in the floor even with depth testing disabled.
	float _floor_offset = 0.05f;
	// Length of the small forward-direction arrow drawn off every circle.
	float _forward_arrow_length = 0.18f;
	// Velocity arrows scale with speed instead of using a fixed length, so
	// a fast swing visibly reads as a longer arrow than a slow one. This is
	// the multiplier from m/s to arrow length, clamped by the max below.
	float _velocity_arrow_scale = 0.1f;
	float _velocity_arrow_max_length = 0.6f;
	// How far above each marker the floating frame label sits.
	float _label_height_offset = 0.22f;
	int _label_font_size = 14;
	Color _history_color = Color(0.35f, 0.55f, 1.0f);
	Color _future_color = Color(0.2f, 1.0f, 0.5f);
	Color _matched_color = Color(1.0f, 0.65f, 0.15f);
	Color _velocity_color = Color(1.0f, 0.85f, 0.1f);
	Color _label_color = Color(1.0f, 1.0f, 1.0f);
	Color _confidence_green = Color(0.25f, 1.0f, 0.3f);
	Color _confidence_yellow = Color(1.0f, 0.9f, 0.2f);
	Color _confidence_red = Color(1.0f, 0.25f, 0.25f);
	Color _traversal_color = Color(1.0f, 0.15f, 0.85f);

	// Reused every frame instead of freed and recreated, so a long play
	// session doesn't churn nodes just because the trajectory is redrawn
	// sixty times a second. Grows on demand, never shrinks.
	Vector<Label3D *> _label_pool;

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

	// Grows the pool to at least p_count labels, creating new Label3D
	// children as needed. Existing labels are never destroyed here.
	void _ensure_label_pool(int p_count);
	// Hides every pooled label beyond p_visible_count, so a shrinking
	// trajectory (fewer history samples right after _ready, for example)
	// doesn't leave stale numbers hanging in the air.
	void _hide_labels_from(int p_visible_count);

protected:
	static void _bind_methods();

public:
	MMDebugDraw();

	void set_controller_path(const NodePath &p_path);
	NodePath get_controller_path() const { return _controller_path; }

	MM_ACCESSORS(bool, draw_trajectory)
	MM_ACCESSORS(bool, draw_matched_trajectory)
	MM_ACCESSORS(bool, draw_facing)
	MM_ACCESSORS(bool, draw_velocity)
	MM_ACCESSORS(bool, draw_frame_labels)
	MM_ACCESSORS(bool, draw_confidence_tier)
	MM_ACCESSORS(float, confidence_green_threshold)
	MM_ACCESSORS(float, confidence_yellow_threshold)
	MM_ACCESSORS(bool, draw_traversal_preview)
	MM_ACCESSORS(float, marker_size)
	MM_ACCESSORS(float, floor_offset)
	MM_ACCESSORS(float, forward_arrow_length)
	MM_ACCESSORS(float, velocity_arrow_scale)
	MM_ACCESSORS(float, velocity_arrow_max_length)
	MM_ACCESSORS(float, label_height_offset)
	MM_ACCESSORS(int, label_font_size)
	MM_ACCESSORS(Color, history_color)
	MM_ACCESSORS(Color, future_color)
	MM_ACCESSORS(Color, matched_color)
	MM_ACCESSORS(Color, velocity_color)
	MM_ACCESSORS(Color, label_color)
	MM_ACCESSORS(Color, confidence_green)
	MM_ACCESSORS(Color, confidence_yellow)
	MM_ACCESSORS(Color, confidence_red)
	MM_ACCESSORS(Color, traversal_color)

	void _ready() override;
	void _process(double p_delta) override;
};

} // namespace godot

#endif // MM_DEBUG_HPP
