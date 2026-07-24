#ifndef MM_SKELETON_PROFILE_HPP
#define MM_SKELETON_PROFILE_HPP

#include "mm_types.hpp"

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// Semantic bone roles.
//
// The framework only ever reasons about roles. Bone *names* live in exactly
// one place, this resource, and are discovered rather than assumed. That is
// what lets the same database pipeline consume a Mixamo rig, a mocap solve, a
// VRM avatar and a studio rig without a single line of pack specific code.
// ---------------------------------------------------------------------------
enum MMBoneRole {
	MM_BONE_ROOT = 0,
	MM_BONE_PELVIS,
	MM_BONE_SPINE,
	MM_BONE_CHEST,
	MM_BONE_NECK,
	MM_BONE_HEAD,
	MM_BONE_LEFT_UPPER_LEG,
	MM_BONE_LEFT_LOWER_LEG,
	MM_BONE_LEFT_FOOT,
	MM_BONE_LEFT_TOE,
	MM_BONE_RIGHT_UPPER_LEG,
	MM_BONE_RIGHT_LOWER_LEG,
	MM_BONE_RIGHT_FOOT,
	MM_BONE_RIGHT_TOE,
	MM_BONE_LEFT_SHOULDER,
	MM_BONE_LEFT_UPPER_ARM,
	MM_BONE_LEFT_LOWER_ARM,
	MM_BONE_LEFT_HAND,
	MM_BONE_RIGHT_SHOULDER,
	MM_BONE_RIGHT_UPPER_ARM,
	MM_BONE_RIGHT_LOWER_ARM,
	MM_BONE_RIGHT_HAND,
	MM_BONE_ROLE_MAX
};

// ---------------------------------------------------------------------------
// MMSkeletonProfile
//
// Detection runs in three passes, strongest evidence first:
//
//   1. Structure. The skeleton graph itself says which chains are legs (two
//      long chains descending from a shared ancestor), which is the head
//      (single chain rising from the chest), and which are arms (two chains
//      branching sideways). Geometry cannot lie about this, and it works on a
//      rig whose bones are named bone_001 through bone_064.
//
//   2. Names. Only used to disambiguate left from right and to correct an
//      ambiguous structural guess. Every common convention is recognised by
//      token, not by exact string, so "mixamorig:LeftFoot", "foot_l",
//      "L_Foot", "Bip01 L Foot" and "clavicle_l" all resolve.
//
//   3. Manual override. Anything the user sets by hand wins and is preserved
//      across re-detection.
// ---------------------------------------------------------------------------
class MMSkeletonProfile : public Resource {
	GDCLASS(MMSkeletonProfile, Resource);

private:
	PackedStringArray _bones;
	PackedStringArray _spine_chain;
	PackedByteArray _locked; // Roles the user assigned by hand.
	bool _detected = false;
	String _detection_report;

	// Working data for a detection pass.
	struct Graph {
		Skeleton3D *skeleton = nullptr;
		int count = 0;
		Vector<int> parents;
		Vector<Vector<int>> children;
		Vector<Transform3D> rest_model;
		Vector<int> depth;
		Vector<int> descendants;
	};

	static void _build_graph(Skeleton3D *p_skeleton, Graph &r_graph);
	static int _deepest_leaf(const Graph &p_graph, int p_from);
	static int _chain_length(const Graph &p_graph, int p_from);
	static int _lowest_common_ancestor(const Graph &p_graph, int p_a, int p_b);
	static int _walk_up(const Graph &p_graph, int p_from, int p_steps);
	static String _normalize(const String &p_name);
	static int _side_hint(const String &p_name); // -1 left, 1 right, 0 unknown.
	static bool _name_suggests(const String &p_name, const char **p_tokens);

	void _set_role_internal(int p_role, const String &p_name);

protected:
	static void _bind_methods();

public:
	MMSkeletonProfile();

	// Runs the full detection. Returns false only when the skeleton is empty
	// or has fewer bones than a biped needs.
	bool auto_detect(Skeleton3D *p_skeleton);

	void set_bone_name(int p_role, const String &p_name);
	String get_bone_name(int p_role) const;
	int find_bone(Skeleton3D *p_skeleton, int p_role) const;

	void set_bones(const PackedStringArray &p_bones);
	PackedStringArray get_bones() const { return _bones; }

	void set_spine_chain(const PackedStringArray &p_chain);
	PackedStringArray get_spine_chain() const { return _spine_chain; }

	// Swaps every left role with its right counterpart, for rigs authored
	// facing the opposite way.
	void swap_sides();

	bool is_detected() const { return _detected; }
	bool has_role(int p_role) const;
	PackedStringArray get_missing_roles() const;
	String get_detection_report() const { return _detection_report; }

	// Convenience sets used to seed a feature schema. The framework asks for
	// roles; the profile answers with whatever this rig calls them.
	PackedStringArray get_default_pose_bones() const;
	PackedStringArray get_foot_bones() const;
	PackedStringArray get_hand_bones() const;

	static String get_role_name(int p_role);
};

} // namespace godot

VARIANT_ENUM_CAST(godot::MMBoneRole);

#endif // MM_SKELETON_PROFILE_HPP
