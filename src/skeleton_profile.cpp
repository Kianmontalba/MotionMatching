#include "skeleton_profile.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

static const char *MM_ROLE_NAMES[MM_BONE_ROLE_MAX] = {
	"root", "pelvis", "spine", "chest", "neck", "head",
	"left_upper_leg", "left_lower_leg", "left_foot", "left_toe",
	"right_upper_leg", "right_lower_leg", "right_foot", "right_toe",
	"left_shoulder", "left_upper_arm", "left_lower_arm", "left_hand",
	"right_shoulder", "right_upper_arm", "right_lower_arm", "right_hand"
};

MMSkeletonProfile::MMSkeletonProfile() {
	_bones.resize(MM_BONE_ROLE_MAX);
	_locked.resize(MM_BONE_ROLE_MAX);
	for (int i = 0; i < MM_BONE_ROLE_MAX; i++) {
		_bones.set(i, String());
		_locked.set(i, 0);
	}
}

String MMSkeletonProfile::get_role_name(int p_role) {
	if (p_role < 0 || p_role >= MM_BONE_ROLE_MAX) {
		return String();
	}
	return MM_ROLE_NAMES[p_role];
}

// Reduces any naming convention to comparable tokens: namespace prefixes are
// dropped, separators are unified, and case is discarded.
String MMSkeletonProfile::_normalize(const String &p_name) {
	String name = p_name.to_lower();
	const int colon = name.rfind(":");
	if (colon >= 0) {
		name = name.substr(colon + 1);
	}
	name = name.replace(" ", "_").replace(".", "_").replace("-", "_");
	while (name.contains("__")) {
		name = name.replace("__", "_");
	}
	return "_" + name + "_";
}

int MMSkeletonProfile::_side_hint(const String &p_name) {
	const String name = _normalize(p_name);

	static const char *left_tokens[] = { "_l_", "_left_", "_lft_", "_lf_", "left", nullptr };
	static const char *right_tokens[] = { "_r_", "_right_", "_rgt_", "_rt_", "right", nullptr };

	for (int i = 0; left_tokens[i] != nullptr; i++) {
		if (name.contains(left_tokens[i])) {
			return -1;
		}
	}
	for (int i = 0; right_tokens[i] != nullptr; i++) {
		if (name.contains(right_tokens[i])) {
			return 1;
		}
	}
	return 0;
}

bool MMSkeletonProfile::_name_suggests(const String &p_name, const char **p_tokens) {
	const String name = _normalize(p_name);
	for (int i = 0; p_tokens[i] != nullptr; i++) {
		if (name.contains(p_tokens[i])) {
			return true;
		}
	}
	return false;
}

void MMSkeletonProfile::_build_graph(Skeleton3D *p_skeleton, Graph &r_graph) {
	r_graph.skeleton = p_skeleton;
	r_graph.count = p_skeleton->get_bone_count();
	r_graph.parents.resize(r_graph.count);
	r_graph.children.resize(r_graph.count);
	r_graph.rest_model.resize(r_graph.count);
	r_graph.depth.resize(r_graph.count);
	r_graph.descendants.resize(r_graph.count);

	for (int i = 0; i < r_graph.count; i++) {
		r_graph.parents.write[i] = p_skeleton->get_bone_parent(i);
		r_graph.children.write[i] = Vector<int>();
		r_graph.descendants.write[i] = 0;
	}

	// Godot stores bones parent first, so one forward pass resolves rest poses
	// and depth, and one backward pass accumulates descendant counts.
	for (int i = 0; i < r_graph.count; i++) {
		const int parent = r_graph.parents[i];
		if (parent >= 0) {
			r_graph.rest_model.write[i] = r_graph.rest_model[parent] * p_skeleton->get_bone_rest(i);
			r_graph.depth.write[i] = r_graph.depth[parent] + 1;
			r_graph.children.write[parent].push_back(i);
		} else {
			r_graph.rest_model.write[i] = p_skeleton->get_bone_rest(i);
			r_graph.depth.write[i] = 0;
		}
	}

	for (int i = r_graph.count - 1; i >= 0; i--) {
		const int parent = r_graph.parents[i];
		if (parent >= 0) {
			r_graph.descendants.write[parent] += r_graph.descendants[i] + 1;
		}
	}
}

int MMSkeletonProfile::_deepest_leaf(const Graph &p_graph, int p_from) {
	int best = p_from;
	int best_depth = p_graph.depth[p_from];

	Vector<int> stack;
	stack.push_back(p_from);
	while (!stack.is_empty()) {
		const int bone = stack[stack.size() - 1];
		stack.remove_at(stack.size() - 1);
		if (p_graph.depth[bone] > best_depth) {
			best_depth = p_graph.depth[bone];
			best = bone;
		}
		for (int i = 0; i < p_graph.children[bone].size(); i++) {
			stack.push_back(p_graph.children[bone][i]);
		}
	}
	return best;
}

int MMSkeletonProfile::_chain_length(const Graph &p_graph, int p_from) {
	return p_graph.depth[_deepest_leaf(p_graph, p_from)] - p_graph.depth[p_from];
}

int MMSkeletonProfile::_lowest_common_ancestor(const Graph &p_graph, int p_a, int p_b) {
	int a = p_a;
	int b = p_b;
	while (a >= 0 && b >= 0 && a != b) {
		if (p_graph.depth[a] >= p_graph.depth[b]) {
			a = p_graph.parents[a];
		} else {
			b = p_graph.parents[b];
		}
	}
	return a == b ? a : -1;
}

int MMSkeletonProfile::_walk_up(const Graph &p_graph, int p_from, int p_steps) {
	int bone = p_from;
	for (int i = 0; i < p_steps && bone >= 0; i++) {
		const int parent = p_graph.parents[bone];
		if (parent < 0) {
			break;
		}
		bone = parent;
	}
	return bone;
}

void MMSkeletonProfile::_set_role_internal(int p_role, const String &p_name) {
	if (p_role < 0 || p_role >= MM_BONE_ROLE_MAX) {
		return;
	}
	if (_locked[p_role] != 0) {
		return; // A manual assignment is never overwritten by detection.
	}
	_bones.set(p_role, p_name);
}

bool MMSkeletonProfile::auto_detect(Skeleton3D *p_skeleton) {
	ERR_FAIL_NULL_V(p_skeleton, false);

	Graph graph;
	_build_graph(p_skeleton, graph);
	if (graph.count < 8) {
		_detection_report = "Skeleton has too few bones to be a character rig.";
		return false;
	}

	_detection_report = String();
	_spine_chain.clear();

	// --- Root -------------------------------------------------------------
	// The root is the parentless bone with the most descendants. Rigs with a
	// separate IK root or a props root therefore pick the skeletal one.
	int root = -1;
	for (int i = 0; i < graph.count; i++) {
		if (graph.parents[i] < 0) {
			if (root < 0 || graph.descendants[i] > graph.descendants[root]) {
				root = i;
			}
		}
	}
	if (root < 0) {
		return false;
	}
	_set_role_internal(MM_BONE_ROOT, p_skeleton->get_bone_name(root));

	// --- Leg chains -------------------------------------------------------
	// Legs are the two longest chains whose leaf sits lowest in the rest pose.
	// Height is measured relative to the root, so a rig authored at any scale
	// or offset still resolves.
	struct Candidate {
		int bone = -1;
		float height = 0.0f;
		int length = 0;
	};

	Vector<Candidate> leaves;
	for (int i = 0; i < graph.count; i++) {
		if (!graph.children[i].is_empty()) {
			continue;
		}
		Candidate candidate;
		candidate.bone = i;
		candidate.height = graph.rest_model[i].origin.y - graph.rest_model[root].origin.y;
		candidate.length = graph.depth[i];
		leaves.push_back(candidate);
	}
	if (leaves.size() < 3) {
		_detection_report = "Could not find enough limb chains.";
		return false;
	}

	// Sort leaves by height, lowest first. Insertion sort: a rig has tens of
	// leaves, not thousands.
	for (int i = 1; i < leaves.size(); i++) {
		const Candidate value = leaves[i];
		int j = i - 1;
		while (j >= 0 && leaves[j].height > value.height) {
			leaves.write[j + 1] = leaves[j];
			j--;
		}
		leaves.write[j + 1] = value;
	}

	// The two lowest leaves that do not share a parent are the feet or toes.
	int foot_a = leaves[0].bone;
	int foot_b = -1;
	for (int i = 1; i < leaves.size(); i++) {
		if (_lowest_common_ancestor(graph, foot_a, leaves[i].bone) != graph.parents[foot_a]) {
			foot_b = leaves[i].bone;
			break;
		}
	}
	if (foot_b < 0) {
		foot_b = leaves[1].bone;
	}

	// A toe is a short bone hanging off the foot. If the lowest leaf's parent
	// still has a long chain above it, the leaf is a toe and the foot is one
	// step up.
	static const char *toe_tokens[] = { "toe", "ball", nullptr };
	bool has_toes = _name_suggests(p_skeleton->get_bone_name(foot_a), toe_tokens) ||
			graph.depth[foot_a] - graph.depth[_walk_up(graph, foot_a, 3)] >= 3;

	int left_foot = has_toes ? graph.parents[foot_a] : foot_a;
	int right_foot = has_toes ? graph.parents[foot_b] : foot_b;
	int left_toe = has_toes ? foot_a : -1;
	int right_toe = has_toes ? foot_b : -1;

	int left_lower = graph.parents[left_foot];
	int right_lower = graph.parents[right_foot];
	int left_upper = left_lower >= 0 ? graph.parents[left_lower] : -1;
	int right_upper = right_lower >= 0 ? graph.parents[right_lower] : -1;

	// --- Pelvis and spine -------------------------------------------------
	const int pelvis = _lowest_common_ancestor(graph, left_upper, right_upper);
	if (pelvis < 0) {
		_detection_report = "Leg chains do not share an ancestor.";
		return false;
	}
	_set_role_internal(MM_BONE_PELVIS, p_skeleton->get_bone_name(pelvis));

	// --- Head and arms ----------------------------------------------------
	// The head is the highest leaf; the arms are the two chains that branch
	// away from the spine below it.
	int head_leaf = leaves[leaves.size() - 1].bone;
	int left_hand = -1;
	int right_hand = -1;
	float widest_left = 0.0f;
	float widest_right = 0.0f;

	for (int i = 0; i < leaves.size(); i++) {
		const int bone = leaves[i].bone;
		if (bone == left_foot || bone == right_foot || bone == left_toe || bone == right_toe ||
				bone == head_leaf) {
			continue;
		}
		if (leaves[i].height < 0.0f) {
			continue; // Below the root: not an arm.
		}
		const float lateral = graph.rest_model[bone].origin.x - graph.rest_model[root].origin.x;
		if (lateral < 0.0f && Math::abs(lateral) > widest_left) {
			widest_left = Math::abs(lateral);
			left_hand = bone;
		} else if (lateral > 0.0f && lateral > widest_right) {
			widest_right = lateral;
			right_hand = bone;
		}
	}

	// Fingers: if the widest leaf sits several bones below a common ancestor,
	// the hand is that ancestor, not the fingertip.
	if (left_hand >= 0 && right_hand >= 0) {
		const int wrist_l = _lowest_common_ancestor(graph, left_hand, _deepest_leaf(graph, left_hand));
		if (wrist_l >= 0 && graph.children[wrist_l].size() > 1) {
			left_hand = wrist_l;
		}
		const int wrist_r = _lowest_common_ancestor(graph, right_hand, _deepest_leaf(graph, right_hand));
		if (wrist_r >= 0 && graph.children[wrist_r].size() > 1) {
			right_hand = wrist_r;
		}
	}

	// --- Left and right ---------------------------------------------------
	// Names decide the side when they say anything at all; otherwise the rest
	// pose does, assuming the Godot convention of -Z forward and +X right.
	const int hint = _side_hint(p_skeleton->get_bone_name(left_foot));
	bool flip = false;
	if (hint != 0) {
		flip = hint > 0;
	} else {
		flip = graph.rest_model[left_foot].origin.x > graph.rest_model[right_foot].origin.x;
	}

	if (flip) {
		SWAP(left_foot, right_foot);
		SWAP(left_toe, right_toe);
		SWAP(left_lower, right_lower);
		SWAP(left_upper, right_upper);
		SWAP(left_hand, right_hand);
	}

	_set_role_internal(MM_BONE_LEFT_FOOT, p_skeleton->get_bone_name(left_foot));
	_set_role_internal(MM_BONE_RIGHT_FOOT, p_skeleton->get_bone_name(right_foot));
	if (left_toe >= 0) {
		_set_role_internal(MM_BONE_LEFT_TOE, p_skeleton->get_bone_name(left_toe));
	}
	if (right_toe >= 0) {
		_set_role_internal(MM_BONE_RIGHT_TOE, p_skeleton->get_bone_name(right_toe));
	}
	if (left_lower >= 0) {
		_set_role_internal(MM_BONE_LEFT_LOWER_LEG, p_skeleton->get_bone_name(left_lower));
	}
	if (right_lower >= 0) {
		_set_role_internal(MM_BONE_RIGHT_LOWER_LEG, p_skeleton->get_bone_name(right_lower));
	}
	if (left_upper >= 0) {
		_set_role_internal(MM_BONE_LEFT_UPPER_LEG, p_skeleton->get_bone_name(left_upper));
	}
	if (right_upper >= 0) {
		_set_role_internal(MM_BONE_RIGHT_UPPER_LEG, p_skeleton->get_bone_name(right_upper));
	}

	if (left_hand >= 0) {
		_set_role_internal(MM_BONE_LEFT_HAND, p_skeleton->get_bone_name(left_hand));
		const int lower = graph.parents[left_hand];
		const int upper = lower >= 0 ? graph.parents[lower] : -1;
		const int shoulder = upper >= 0 ? graph.parents[upper] : -1;
		if (lower >= 0) {
			_set_role_internal(MM_BONE_LEFT_LOWER_ARM, p_skeleton->get_bone_name(lower));
		}
		if (upper >= 0) {
			_set_role_internal(MM_BONE_LEFT_UPPER_ARM, p_skeleton->get_bone_name(upper));
		}
		if (shoulder >= 0) {
			_set_role_internal(MM_BONE_LEFT_SHOULDER, p_skeleton->get_bone_name(shoulder));
		}
	}
	if (right_hand >= 0) {
		_set_role_internal(MM_BONE_RIGHT_HAND, p_skeleton->get_bone_name(right_hand));
		const int lower = graph.parents[right_hand];
		const int upper = lower >= 0 ? graph.parents[lower] : -1;
		const int shoulder = upper >= 0 ? graph.parents[upper] : -1;
		if (lower >= 0) {
			_set_role_internal(MM_BONE_RIGHT_LOWER_ARM, p_skeleton->get_bone_name(lower));
		}
		if (upper >= 0) {
			_set_role_internal(MM_BONE_RIGHT_UPPER_ARM, p_skeleton->get_bone_name(upper));
		}
		if (shoulder >= 0) {
			_set_role_internal(MM_BONE_RIGHT_SHOULDER, p_skeleton->get_bone_name(shoulder));
		}
	}

	// --- Spine chain ------------------------------------------------------
	// Everything between the pelvis and the chest, in order. The chest is the
	// branch point where the arms leave the spine.
	int chest = -1;
	if (left_hand >= 0 && right_hand >= 0) {
		chest = _lowest_common_ancestor(graph, left_hand, right_hand);
	}
	if (chest < 0) {
		chest = _walk_up(graph, head_leaf, 2);
	}

	Vector<int> spine;
	int walker = chest;
	while (walker >= 0 && walker != pelvis) {
		spine.insert(0, walker);
		walker = graph.parents[walker];
	}
	for (int i = 0; i < spine.size(); i++) {
		_spine_chain.push_back(p_skeleton->get_bone_name(spine[i]));
	}
	if (!spine.is_empty()) {
		_set_role_internal(MM_BONE_SPINE, p_skeleton->get_bone_name(spine[0]));
		_set_role_internal(MM_BONE_CHEST, p_skeleton->get_bone_name(spine[spine.size() - 1]));
	}

	// --- Head and neck ----------------------------------------------------
	static const char *head_tokens[] = { "head", "skull", nullptr };
	int head = head_leaf;
	while (head >= 0 && !_name_suggests(p_skeleton->get_bone_name(head), head_tokens) &&
			graph.children[head].is_empty() && graph.parents[head] >= 0 &&
			graph.children[graph.parents[head]].size() == 1) {
		// Walk up past hair, eye or accessory leaves toward the real head.
		const int parent = graph.parents[head];
		if (parent == chest) {
			break;
		}
		head = parent;
	}
	_set_role_internal(MM_BONE_HEAD, p_skeleton->get_bone_name(head));
	const int neck = graph.parents[head];
	if (neck >= 0 && neck != chest) {
		_set_role_internal(MM_BONE_NECK, p_skeleton->get_bone_name(neck));
	}

	_detected = true;

	const PackedStringArray missing = get_missing_roles();
	_detection_report = vformat(
			"Detected %d of %d roles from %d bones. Spine chain: %d bones.%s",
			MM_BONE_ROLE_MAX - missing.size(), MM_BONE_ROLE_MAX, graph.count, _spine_chain.size(),
			missing.is_empty() ? String() : "\nUnresolved: " + String(", ").join(missing));

	emit_changed();
	return true;
}

void MMSkeletonProfile::set_bone_name(int p_role, const String &p_name) {
	if (p_role < 0 || p_role >= MM_BONE_ROLE_MAX) {
		return;
	}
	_bones.set(p_role, p_name);
	_locked.set(p_role, p_name.is_empty() ? 0 : 1);
	emit_changed();
}

String MMSkeletonProfile::get_bone_name(int p_role) const {
	if (p_role < 0 || p_role >= MM_BONE_ROLE_MAX) {
		return String();
	}
	return _bones[p_role];
}

int MMSkeletonProfile::find_bone(Skeleton3D *p_skeleton, int p_role) const {
	if (p_skeleton == nullptr) {
		return -1;
	}
	const String name = get_bone_name(p_role);
	return name.is_empty() ? -1 : p_skeleton->find_bone(name);
}

void MMSkeletonProfile::set_bones(const PackedStringArray &p_bones) {
	_bones = p_bones;
	_bones.resize(MM_BONE_ROLE_MAX);
	emit_changed();
}

void MMSkeletonProfile::set_spine_chain(const PackedStringArray &p_chain) {
	_spine_chain = p_chain;
	emit_changed();
}

void MMSkeletonProfile::swap_sides() {
	static const int pairs[][2] = {
		{ MM_BONE_LEFT_UPPER_LEG, MM_BONE_RIGHT_UPPER_LEG },
		{ MM_BONE_LEFT_LOWER_LEG, MM_BONE_RIGHT_LOWER_LEG },
		{ MM_BONE_LEFT_FOOT, MM_BONE_RIGHT_FOOT },
		{ MM_BONE_LEFT_TOE, MM_BONE_RIGHT_TOE },
		{ MM_BONE_LEFT_SHOULDER, MM_BONE_RIGHT_SHOULDER },
		{ MM_BONE_LEFT_UPPER_ARM, MM_BONE_RIGHT_UPPER_ARM },
		{ MM_BONE_LEFT_LOWER_ARM, MM_BONE_RIGHT_LOWER_ARM },
		{ MM_BONE_LEFT_HAND, MM_BONE_RIGHT_HAND }
	};
	for (const int(&pair)[2] : pairs) {
		const String temp = _bones[pair[0]];
		_bones.set(pair[0], _bones[pair[1]]);
		_bones.set(pair[1], temp);
	}
	emit_changed();
}

bool MMSkeletonProfile::has_role(int p_role) const {
	return !get_bone_name(p_role).is_empty();
}

PackedStringArray MMSkeletonProfile::get_missing_roles() const {
	PackedStringArray missing;
	for (int i = 0; i < MM_BONE_ROLE_MAX; i++) {
		if (_bones[i].is_empty()) {
			missing.push_back(get_role_name(i));
		}
	}
	return missing;
}

// The default feature set: pelvis plus both feet. It is the smallest set that
// captures gait, and it is what most databases should start from.
PackedStringArray MMSkeletonProfile::get_default_pose_bones() const {
	PackedStringArray bones;
	if (has_role(MM_BONE_PELVIS)) {
		bones.push_back(get_bone_name(MM_BONE_PELVIS));
	}
	if (has_role(MM_BONE_LEFT_FOOT)) {
		bones.push_back(get_bone_name(MM_BONE_LEFT_FOOT));
	}
	if (has_role(MM_BONE_RIGHT_FOOT)) {
		bones.push_back(get_bone_name(MM_BONE_RIGHT_FOOT));
	}
	return bones;
}

PackedStringArray MMSkeletonProfile::get_foot_bones() const {
	PackedStringArray bones;
	if (has_role(MM_BONE_LEFT_FOOT)) {
		bones.push_back(get_bone_name(MM_BONE_LEFT_FOOT));
	}
	if (has_role(MM_BONE_RIGHT_FOOT)) {
		bones.push_back(get_bone_name(MM_BONE_RIGHT_FOOT));
	}
	return bones;
}

PackedStringArray MMSkeletonProfile::get_hand_bones() const {
	PackedStringArray bones;
	if (has_role(MM_BONE_LEFT_HAND)) {
		bones.push_back(get_bone_name(MM_BONE_LEFT_HAND));
	}
	if (has_role(MM_BONE_RIGHT_HAND)) {
		bones.push_back(get_bone_name(MM_BONE_RIGHT_HAND));
	}
	return bones;
}

void MMSkeletonProfile::_bind_methods() {
	ClassDB::bind_method(D_METHOD("auto_detect", "skeleton"), &MMSkeletonProfile::auto_detect);
	ClassDB::bind_method(D_METHOD("set_bone_name", "role", "name"), &MMSkeletonProfile::set_bone_name);
	ClassDB::bind_method(D_METHOD("get_bone_name", "role"), &MMSkeletonProfile::get_bone_name);
	ClassDB::bind_method(D_METHOD("find_bone", "skeleton", "role"), &MMSkeletonProfile::find_bone);
	ClassDB::bind_method(D_METHOD("swap_sides"), &MMSkeletonProfile::swap_sides);
	ClassDB::bind_method(D_METHOD("is_detected"), &MMSkeletonProfile::is_detected);
	ClassDB::bind_method(D_METHOD("has_role", "role"), &MMSkeletonProfile::has_role);
	ClassDB::bind_method(D_METHOD("get_missing_roles"), &MMSkeletonProfile::get_missing_roles);
	ClassDB::bind_method(D_METHOD("get_detection_report"), &MMSkeletonProfile::get_detection_report);
	ClassDB::bind_method(D_METHOD("get_default_pose_bones"), &MMSkeletonProfile::get_default_pose_bones);
	ClassDB::bind_method(D_METHOD("get_foot_bones"), &MMSkeletonProfile::get_foot_bones);
	ClassDB::bind_method(D_METHOD("get_hand_bones"), &MMSkeletonProfile::get_hand_bones);

	ClassDB::bind_method(D_METHOD("set_bones", "bones"), &MMSkeletonProfile::set_bones);
	ClassDB::bind_method(D_METHOD("get_bones"), &MMSkeletonProfile::get_bones);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "bones"), "set_bones", "get_bones");

	ClassDB::bind_method(D_METHOD("set_spine_chain", "chain"), &MMSkeletonProfile::set_spine_chain);
	ClassDB::bind_method(D_METHOD("get_spine_chain"), &MMSkeletonProfile::get_spine_chain);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "spine_chain"), "set_spine_chain",
			"get_spine_chain");

	ClassDB::bind_static_method("MMSkeletonProfile", D_METHOD("get_role_name", "role"),
			&MMSkeletonProfile::get_role_name);

	BIND_ENUM_CONSTANT(MM_BONE_ROOT);
	BIND_ENUM_CONSTANT(MM_BONE_PELVIS);
	BIND_ENUM_CONSTANT(MM_BONE_SPINE);
	BIND_ENUM_CONSTANT(MM_BONE_CHEST);
	BIND_ENUM_CONSTANT(MM_BONE_NECK);
	BIND_ENUM_CONSTANT(MM_BONE_HEAD);
	BIND_ENUM_CONSTANT(MM_BONE_LEFT_UPPER_LEG);
	BIND_ENUM_CONSTANT(MM_BONE_LEFT_LOWER_LEG);
	BIND_ENUM_CONSTANT(MM_BONE_LEFT_FOOT);
	BIND_ENUM_CONSTANT(MM_BONE_LEFT_TOE);
	BIND_ENUM_CONSTANT(MM_BONE_RIGHT_UPPER_LEG);
	BIND_ENUM_CONSTANT(MM_BONE_RIGHT_LOWER_LEG);
	BIND_ENUM_CONSTANT(MM_BONE_RIGHT_FOOT);
	BIND_ENUM_CONSTANT(MM_BONE_RIGHT_TOE);
	BIND_ENUM_CONSTANT(MM_BONE_LEFT_SHOULDER);
	BIND_ENUM_CONSTANT(MM_BONE_LEFT_UPPER_ARM);
	BIND_ENUM_CONSTANT(MM_BONE_LEFT_LOWER_ARM);
	BIND_ENUM_CONSTANT(MM_BONE_LEFT_HAND);
	BIND_ENUM_CONSTANT(MM_BONE_RIGHT_SHOULDER);
	BIND_ENUM_CONSTANT(MM_BONE_RIGHT_UPPER_ARM);
	BIND_ENUM_CONSTANT(MM_BONE_RIGHT_LOWER_ARM);
	BIND_ENUM_CONSTANT(MM_BONE_RIGHT_HAND);
}
