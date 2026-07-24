#include "traversal.hpp"

#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// Small helper so every probe in this file reads the same way.
static Dictionary mm_raycast(PhysicsDirectSpaceState3D *p_space, const Vector3 &p_from,
		const Vector3 &p_to, uint32_t p_mask) {
	Ref<PhysicsRayQueryParameters3D> query = PhysicsRayQueryParameters3D::create(p_from, p_to);
	query->set_collision_mask(p_mask);
	query->set_hit_from_inside(false);
	return p_space->intersect_ray(query);
}

void MMTraversal::clear() {
	_detected = TRAVERSAL_NONE;
	_entry_point = Vector3();
	_top_point = Vector3();
	_exit_point = Vector3();
	_obstacle_height = 0.0f;
	_obstacle_depth = 0.0f;
	_surface_angle = 0.0f;
}

// The probe follows the predicted trajectory rather than the character's
// current facing. A character about to turn tests the wall it will actually
// meet, which is the difference between a vault that triggers on time and one
// that triggers a beat too late.
MMTraversal::TraversalType MMTraversal::probe(PhysicsDirectSpaceState3D *p_space,
		const Transform3D &p_character, const Ref<MMTrajectory> &p_trajectory) {
	clear();
	if (p_space == nullptr) {
		return TRAVERSAL_NONE;
	}

	Vector3 forward = -p_character.basis.get_column(2);
	if (p_trajectory.is_valid() && p_trajectory->get_sample_count() > 0) {
		const int mid = p_trajectory->get_sample_count() / 2;
		const Vector3 target = p_trajectory->get_sample_position(mid);
		Vector3 to_target = target - p_character.origin;
		to_target.y = 0.0f;
		if (to_target.length_squared() > 0.01f) {
			forward = to_target.normalized();
		}
	}
	forward.y = 0.0f;
	if (forward.length_squared() < 0.000001f) {
		return TRAVERSAL_NONE;
	}
	forward.normalize();

	const Vector3 up = Vector3(0, 1, 0);
	const Vector3 origin = p_character.origin;

	// Step 1: is there anything in front of the knees?
	const Vector3 probe_origin = origin + up * 0.35f;
	const Dictionary front = mm_raycast(p_space, probe_origin,
			probe_origin + forward * _max_probe_distance, _collision_mask);
	if (front.is_empty()) {
		return TRAVERSAL_NONE;
	}

	_entry_point = front["position"];
	const Vector3 normal = front["normal"];
	_surface_angle = Math::acos(CLAMP(normal.dot(up), -1.0f, 1.0f));

	// A ramp is not an obstacle. Anything the character can simply walk up is
	// left to the locomotion system.
	if (_surface_angle < Math::deg_to_rad(50.0f)) {
		return TRAVERSAL_NONE;
	}

	// Step 2: find the top edge by dropping onto the obstacle from above.
	const Vector3 above = Vector3(_entry_point.x, origin.y + _mantle_height + 0.5f, _entry_point.z) +
			forward * 0.15f;
	const Dictionary top = mm_raycast(p_space, above, above - up * (_mantle_height + 1.0f), _collision_mask);
	if (top.is_empty()) {
		// Nothing to stand on: it is a wall, not a ledge.
		_obstacle_height = _mantle_height;
		_detected = TRAVERSAL_CLIMB;
		return _detected;
	}

	_top_point = top["position"];
	_obstacle_height = _top_point.y - origin.y;

	// Step 3: measure the depth by looking for the far edge, then for the
	// landing surface behind it. Depth is what separates a vault from a mantle.
	const Vector3 across_start = _top_point + up * 0.1f + forward * 0.05f;
	const Dictionary across = mm_raycast(p_space, across_start + up * 0.4f,
			across_start + up * 0.4f + forward * _max_obstacle_depth, _collision_mask);

	float depth = _max_obstacle_depth;
	for (float distance = 0.1f; distance <= _max_obstacle_depth; distance += 0.1f) {
		const Vector3 sample = _top_point + up * 0.3f + forward * distance;
		const Dictionary ground = mm_raycast(p_space, sample, sample - up * 0.6f, _collision_mask);
		if (ground.is_empty()) {
			depth = distance;
			break;
		}
	}
	_obstacle_depth = depth;

	// Landing point on the far side.
	const Vector3 landing_probe = _top_point + forward * (depth + 0.4f) + up * 0.5f;
	const Dictionary landing = mm_raycast(p_space, landing_probe,
			landing_probe - up * (_mantle_height + 1.0f), _collision_mask);
	_exit_point = landing.is_empty() ? _top_point + forward * (depth + 0.4f)
									 : (Vector3)landing["position"];

	// Step 4: classify.
	if (_obstacle_height <= _step_height) {
		_detected = TRAVERSAL_STEP;
	} else if (_obstacle_height <= _low_vault_height) {
		_detected = _obstacle_depth > 0.8f ? TRAVERSAL_MANTLE : TRAVERSAL_LOW_VAULT;
	} else if (_obstacle_height <= _high_vault_height) {
		_detected = _obstacle_depth > 0.8f ? TRAVERSAL_MANTLE : TRAVERSAL_HIGH_VAULT;
	} else if (_obstacle_height <= _mantle_height) {
		_detected = TRAVERSAL_MANTLE;
	} else {
		_detected = TRAVERSAL_CLIMB;
	}

	return _detected;
}

int MMTraversal::get_required_tags() const {
	switch (_detected) {
		case TRAVERSAL_LOW_VAULT:
		case TRAVERSAL_HIGH_VAULT:
			return MM_TAG_VAULT;
		case TRAVERSAL_MANTLE:
			return MM_TAG_MANTLE;
		case TRAVERSAL_CLIMB:
			return MM_TAG_CLIMB;
		case TRAVERSAL_SLIDE_UNDER:
			return MM_TAG_SLIDE;
		default:
			return 0;
	}
}

Dictionary MMTraversal::get_result() const {
	Dictionary result;
	result["type"] = (int)_detected;
	result["entry_point"] = _entry_point;
	result["top_point"] = _top_point;
	result["exit_point"] = _exit_point;
	result["height"] = _obstacle_height;
	result["depth"] = _obstacle_depth;
	result["surface_angle"] = _surface_angle;
	result["required_tags"] = get_required_tags();
	return result;
}

void MMTraversal::_bind_methods() {
	MM_BIND_PROPERTY(MMTraversal, Variant::FLOAT, max_probe_distance)
	MM_BIND_PROPERTY(MMTraversal, Variant::FLOAT, character_height)
	MM_BIND_PROPERTY(MMTraversal, Variant::FLOAT, step_height)
	MM_BIND_PROPERTY(MMTraversal, Variant::FLOAT, low_vault_height)
	MM_BIND_PROPERTY(MMTraversal, Variant::FLOAT, high_vault_height)
	MM_BIND_PROPERTY(MMTraversal, Variant::FLOAT, mantle_height)
	MM_BIND_PROPERTY(MMTraversal, Variant::FLOAT, max_obstacle_depth)
	MM_BIND_PROPERTY(MMTraversal, Variant::INT, collision_mask)

	ClassDB::bind_method(D_METHOD("probe", "space_state", "character_transform", "trajectory"),
			&MMTraversal::probe);
	ClassDB::bind_method(D_METHOD("clear"), &MMTraversal::clear);
	ClassDB::bind_method(D_METHOD("get_detected_type"), &MMTraversal::get_detected_type);
	ClassDB::bind_method(D_METHOD("get_required_tags"), &MMTraversal::get_required_tags);
	ClassDB::bind_method(D_METHOD("get_entry_point"), &MMTraversal::get_entry_point);
	ClassDB::bind_method(D_METHOD("get_top_point"), &MMTraversal::get_top_point);
	ClassDB::bind_method(D_METHOD("get_exit_point"), &MMTraversal::get_exit_point);
	ClassDB::bind_method(D_METHOD("get_obstacle_height"), &MMTraversal::get_obstacle_height);
	ClassDB::bind_method(D_METHOD("get_obstacle_depth"), &MMTraversal::get_obstacle_depth);
	ClassDB::bind_method(D_METHOD("get_result"), &MMTraversal::get_result);

	BIND_ENUM_CONSTANT(TRAVERSAL_NONE);
	BIND_ENUM_CONSTANT(TRAVERSAL_STEP);
	BIND_ENUM_CONSTANT(TRAVERSAL_LOW_VAULT);
	BIND_ENUM_CONSTANT(TRAVERSAL_HIGH_VAULT);
	BIND_ENUM_CONSTANT(TRAVERSAL_MANTLE);
	BIND_ENUM_CONSTANT(TRAVERSAL_CLIMB);
	BIND_ENUM_CONSTANT(TRAVERSAL_SLIDE_UNDER);
}
