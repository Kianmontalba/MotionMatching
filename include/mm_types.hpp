#ifndef MM_TYPES_HPP
#define MM_TYPES_HPP

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <cstdint>
#include <limits>

namespace godot {

// Shared numeric helpers.
static const float MM_INFINITY = std::numeric_limits<float>::infinity();

// Bumped whenever MotionMatchingDatabase's stored layout changes in a way
// that makes an older saved .res/.tres unsafe to use as-is (a new packed
// array, a changed meaning for an existing one, etc.). MotionMatchingDatabase
// stamps this into format_version at finalize() time; a mismatch on load
// means the database was built by a different addon version than the one
// currently running.
static const int MM_DATABASE_FORMAT_VERSION = 1;

// ---------------------------------------------------------------------------
// Motion categories. Every database frame belongs to exactly one category.
// Categories are the cheapest filter stage: an integer compare per frame.
// ---------------------------------------------------------------------------
enum MMCategory {
	MM_CATEGORY_LOCOMOTION = 0,
	MM_CATEGORY_AIRBORNE,
	MM_CATEGORY_TRAVERSAL,
	MM_CATEGORY_COMBAT,
	MM_CATEGORY_INTERACTION,
	MM_CATEGORY_CUSTOM,
	MM_CATEGORY_MAX
};

// ---------------------------------------------------------------------------
// Animation tags. Stored as a 32 bit mask per frame so the search can reject
// whole groups of frames with a single bitwise operation.
// ---------------------------------------------------------------------------
enum MMTag {
	MM_TAG_NONE = 0,
	MM_TAG_IDLE = 1 << 0,
	MM_TAG_WALK = 1 << 1,
	MM_TAG_JOG = 1 << 2,
	MM_TAG_RUN = 1 << 3,
	MM_TAG_SPRINT = 1 << 4,
	MM_TAG_CROUCH = 1 << 5,
	MM_TAG_PRONE = 1 << 6,
	MM_TAG_STRAFE = 1 << 7,
	MM_TAG_TURN = 1 << 8,
	MM_TAG_PIVOT = 1 << 9,
	MM_TAG_START = 1 << 10,
	MM_TAG_STOP = 1 << 11,
	MM_TAG_JUMP = 1 << 12,
	MM_TAG_FALL = 1 << 13,
	MM_TAG_LAND = 1 << 14,
	MM_TAG_VAULT = 1 << 15,
	MM_TAG_MANTLE = 1 << 16,
	MM_TAG_CLIMB = 1 << 17,
	MM_TAG_SLIDE = 1 << 18,
	MM_TAG_ROLL = 1 << 19,
	MM_TAG_ATTACK = 1 << 20,
	MM_TAG_RELOAD = 1 << 21,
	MM_TAG_HIT = 1 << 22,
	MM_TAG_AIM = 1 << 23,
	MM_TAG_UNARMED = 1 << 24,
	MM_TAG_RIFLE = 1 << 25,
	MM_TAG_PISTOL = 1 << 26,
	MM_TAG_MIRRORED = 1 << 27,
	MM_TAG_USER_0 = 1 << 28,
	MM_TAG_USER_1 = 1 << 29,
	MM_TAG_USER_2 = 1 << 30
};

// Convenience masks used by the default gameplay filters.
static const uint32_t MM_TAG_MASK_GROUNDED =
		MM_TAG_IDLE | MM_TAG_WALK | MM_TAG_JOG | MM_TAG_RUN | MM_TAG_SPRINT |
		MM_TAG_CROUCH | MM_TAG_PRONE | MM_TAG_STRAFE | MM_TAG_TURN |
		MM_TAG_PIVOT | MM_TAG_START | MM_TAG_STOP;

static const uint32_t MM_TAG_MASK_AIRBORNE = MM_TAG_JUMP | MM_TAG_FALL | MM_TAG_LAND;

static const uint32_t MM_TAG_MASK_TRAVERSAL =
		MM_TAG_VAULT | MM_TAG_MANTLE | MM_TAG_CLIMB | MM_TAG_SLIDE | MM_TAG_ROLL;

// A clip carrying any of these tags represents a single discrete motion with
// a clear beginning and end (a start-up, a stop, a turn-in-place, a jump
// lifecycle, a traversal move, an attack, ...) rather than a cycle meant to
// repeat. Used to decide whether a baked clip should loop at runtime, purely
// from its tags -- so the rule holds regardless of which animation library,
// naming convention, or rig the tags were assigned to.
static const uint32_t MM_TAG_MASK_ONE_SHOT =
		MM_TAG_START | MM_TAG_STOP | MM_TAG_TURN | MM_TAG_PIVOT |
		MM_TAG_MASK_AIRBORNE | MM_TAG_MASK_TRAVERSAL |
		MM_TAG_ATTACK | MM_TAG_RELOAD | MM_TAG_HIT;

// ---------------------------------------------------------------------------
// Feature groups. Each dimension of a feature vector maps to one group, and
// each group carries its own normalization scale and cost weight.
// ---------------------------------------------------------------------------
enum MMFeatureGroup {
	MM_GROUP_TRAJECTORY_POSITION = 0,
	MM_GROUP_TRAJECTORY_DIRECTION,
	MM_GROUP_POSE_POSITION,
	MM_GROUP_POSE_VELOCITY,
	MM_GROUP_ROOT_VELOCITY,
	MM_GROUP_EXTRA,
	MM_GROUP_MAX
};

// Quality level, used for LOD on mobile and for crowd characters.
enum MMQuality {
	MM_QUALITY_ULTRA = 0,
	MM_QUALITY_HIGH,
	MM_QUALITY_MEDIUM,
	MM_QUALITY_LOW,
	MM_QUALITY_MAX
};

// How the controller pushes its result to the animation system.
enum MMPlaybackMode {
	MM_PLAYBACK_ANIMATION_TREE = 0, // AnimationNodeMotionMatching reads the controller.
	MM_PLAYBACK_SIGNAL_ONLY // The controller only emits signals; the game applies them.
};

// ---------------------------------------------------------------------------
// A single predicted or recorded trajectory point, expressed in character
// space (origin at the character, -Z forward, matching Godot conventions).
// ---------------------------------------------------------------------------
struct MMTrajectorySample {
	Vector3 position;
	Vector3 direction;
	Vector3 velocity;
	float time = 0.0f;

	MMTrajectorySample() :
			position(0, 0, 0), direction(0, 0, -1), velocity(0, 0, 0) {}
};

// ---------------------------------------------------------------------------
// Result of a database search.
// ---------------------------------------------------------------------------
struct MMMatchResult {
	int frame_index = -1;
	int animation_id = -1;
	float animation_time = 0.0f;
	float normalized_time = 0.0f;
	float cost = MM_INFINITY;

	bool is_valid() const { return frame_index >= 0; }
};

// ---------------------------------------------------------------------------
// Candidate filter evaluated before any expensive pose math runs.
// ---------------------------------------------------------------------------
struct MMSearchFilter {
	uint32_t required_tags = 0; // Every bit must be present.
	uint32_t any_tags = 0; // At least one bit must be present (0 = ignore).
	uint32_t blocked_tags = 0; // No bit may be present.
	uint32_t category_mask = 0xFFFFFFFFu; // Bit n allows MMCategory n.
	float min_speed = -1.0f; // Negative disables the speed window.
	float max_speed = -1.0f;

	_FORCE_INLINE_ bool accepts(uint32_t p_tags, uint8_t p_category, float p_speed) const {
		if (!(category_mask & (1u << p_category))) {
			return false;
		}
		if (p_tags & blocked_tags) {
			return false;
		}
		if ((p_tags & required_tags) != required_tags) {
			return false;
		}
		if (any_tags != 0 && !(p_tags & any_tags)) {
			return false;
		}
		if (min_speed >= 0.0f && p_speed < min_speed) {
			return false;
		}
		if (max_speed >= 0.0f && p_speed > max_speed) {
			return false;
		}
		return true;
	}
};

// ---------------------------------------------------------------------------
// Per-search profiling data, surfaced to the editor and to get_debug_info().
// ---------------------------------------------------------------------------
struct MMSearchStats {
	int frames_compared = 0;
	int candidates_visited = 0;
	int nodes_visited = 0;
	int cache_hits = 0;
	int cache_misses = 0;
	bool budget_exceeded = false;
	double search_time_usec = 0.0;

	void reset() {
		frames_compared = 0;
		candidates_visited = 0;
		nodes_visited = 0;
		budget_exceeded = false;
		search_time_usec = 0.0;
	}
};

// ---------------------------------------------------------------------------
// Extra hints handed to the search so it can exploit temporal coherence.
// ---------------------------------------------------------------------------
struct MMSearchContext {
	int continuation_frame = -1; // Frame the character would play if nothing changed.
	int previous_frame = -1; // Frame selected by the previous search.
	int neighborhood_radius = 12; // Frames scanned around the hints before the tree search.
	int max_comparisons = 0; // 0 = unlimited.
	float best_cost_seed = MM_INFINITY; // Prunes the tree from the very first node.
};

// Reduces the boilerplate of trivial accessors while keeping the underscore
// prefixed private member convention used across the framework.
#define MM_ACCESSORS(m_type, m_name)                       \
	void set_##m_name(m_type p_value) { _##m_name = p_value; } \
	m_type get_##m_name() const { return _##m_name; }

// Binds a property that already has set_x / get_x accessors.
#define MM_BIND_PROPERTY(m_class, m_variant, m_name)                                     \
	ClassDB::bind_method(D_METHOD("set_" #m_name, #m_name), &m_class::set_##m_name);      \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &m_class::get_##m_name);               \
	ADD_PROPERTY(PropertyInfo(m_variant, #m_name), "set_" #m_name, "get_" #m_name);

// Same, but for arrays that must be serialized without cluttering the inspector.
#define MM_BIND_STORAGE(m_class, m_variant, m_name)                                      \
	ClassDB::bind_method(D_METHOD("set_" #m_name, #m_name), &m_class::set_##m_name);      \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &m_class::get_##m_name);               \
	ADD_PROPERTY(PropertyInfo(m_variant, #m_name, PROPERTY_HINT_NONE, "",                 \
						  PROPERTY_USAGE_STORAGE),                                       \
			"set_" #m_name, "get_" #m_name);

} // namespace godot

VARIANT_ENUM_CAST(godot::MMCategory);
VARIANT_ENUM_CAST(godot::MMTag);
VARIANT_ENUM_CAST(godot::MMFeatureGroup);
VARIANT_ENUM_CAST(godot::MMQuality);
VARIANT_ENUM_CAST(godot::MMPlaybackMode);

#endif // MM_TYPES_HPP
