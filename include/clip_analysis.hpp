#ifndef MM_CLIP_ANALYSIS_HPP
#define MM_CLIP_ANALYSIS_HPP

#include "mm_types.hpp"
#include "skeleton_profile.hpp"

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// MMClipStats
//
// What a clip actually does, measured from its motion. Every quantity that has
// a unit is normalized by the character's own hip height, so the same
// thresholds classify a 30 cm creature, a 1.8 m human and a 6 m giant without
// a single number being retuned.
// ---------------------------------------------------------------------------
struct MMClipStats {
	float duration = 0.0f;
	float hip_height = 1.0f; // Reference scale for this rig, in metres.

	float average_speed = 0.0f; // Hip heights per second.
	float peak_speed = 0.0f;
	float start_speed = 0.0f;
	float end_speed = 0.0f;

	float net_displacement = 0.0f; // Hip heights.
	float vertical_range = 0.0f;
	float net_height_gain = 0.0f;

	float total_turn = 0.0f; // Radians.
	float lateral_ratio = 0.0f; // Sideways travel over total travel.
	float backward_ratio = 0.0f;

	float contact_ratio = 1.0f; // Fraction of frames with a foot down.
	float longest_airborne = 0.0f; // Seconds with no foot down.
	float crouch_ratio = 0.0f; // Fraction of frames with a lowered pelvis.
};

// ---------------------------------------------------------------------------
// MMClipAnalyzer
//
// Derives tags and a category from measured motion rather than from a file
// name. This is what makes the framework animation pack agnostic: an untitled
// mocap take, a Mixamo clip called "mixamo.com" and a studio clip called
// "LOC_RUN_FWD_01" all classify identically because they move identically.
//
// A name rule table exists, but it ships empty. It is an override for teams
// with a strict convention, never a requirement.
// ---------------------------------------------------------------------------
class MMClipAnalyzer : public Resource {
	GDCLASS(MMClipAnalyzer, Resource);

private:
	// Speed bands, in hip heights per second.
	float _idle_speed = 0.10f;
	float _walk_speed = 1.50f;
	float _jog_speed = 2.60f;
	float _run_speed = 3.80f;

	// Motion shape thresholds.
	float _turn_threshold = 1.05f; // Radians of net yaw to count as a turn.
	float _strafe_ratio = 0.55f;
	float _backward_ratio = 0.55f;
	float _crouch_drop = 0.18f; // Pelvis drop, in hip heights.
	float _airborne_seconds = 0.18f;
	float _traversal_height = 0.35f; // Vertical range, in hip heights.
	float _start_stop_delta = 0.8f; // Speed change across the clip.

	Dictionary _name_rules; // Optional. Empty by default, on purpose.
	bool _use_name_rules = false;
	bool _use_motion_analysis = true;

protected:
	static void _bind_methods();

public:
	MM_ACCESSORS(float, idle_speed)
	MM_ACCESSORS(float, walk_speed)
	MM_ACCESSORS(float, jog_speed)
	MM_ACCESSORS(float, run_speed)
	MM_ACCESSORS(float, turn_threshold)
	MM_ACCESSORS(float, strafe_ratio)
	MM_ACCESSORS(float, backward_ratio)
	MM_ACCESSORS(float, crouch_drop)
	MM_ACCESSORS(float, airborne_seconds)
	MM_ACCESSORS(float, traversal_height)
	MM_ACCESSORS(float, start_stop_delta)
	MM_ACCESSORS(Dictionary, name_rules)
	MM_ACCESSORS(bool, use_name_rules)
	MM_ACCESSORS(bool, use_motion_analysis)

	// Derives the speed bands from the distribution of clips actually present
	// in a library, instead of leaving the defaults to fit or not fit an
	// unfamiliar pack. Quartiles of the moving clips (idle ones excluded) give
	// four bands that follow the library's own sense of walking and running,
	// whether it is a human rig, a quadruped, or something stylized.
	void calibrate_speed_bands(const PackedFloat32Array &p_clip_average_speeds);

	// Core classification. p_name is only consulted when name rules are
	// explicitly enabled and populated.
	int classify(const MMClipStats &p_stats, const String &p_name) const;
	static int category_for_tags(int p_tags);

	// Scriptable entry point so a tool script can classify a stats dictionary.
	int classify_dictionary(const Dictionary &p_stats, const String &p_name) const;
	static Dictionary stats_to_dictionary(const MMClipStats &p_stats);
};

} // namespace godot

#endif // MM_CLIP_ANALYSIS_HPP
