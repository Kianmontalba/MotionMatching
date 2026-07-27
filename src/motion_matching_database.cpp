#include "motion_matching.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// ---------------------------------------------------------------------------
// MotionMatchingResource
//
// The single asset a designer touches. Everything the runtime needs hangs off
// it, so a weapon swap, an AI movement set or a mobile quality preset is one
// resource swap and nothing else.
// ---------------------------------------------------------------------------

void MotionMatchingResource::set_animation_library(const Ref<AnimationLibrary> &p_library) {
	_animation_library = p_library;
	emit_changed();
}

void MotionMatchingResource::set_database(const Ref<MMExtraDatabase> &p_database) {
	_database = p_database;
	if (_database.is_valid() && _schema.is_null()) {
		Ref<MotionMatchingDatabase> active = _database->get_active_database();
		if (active.is_valid()) {
			_schema = active->get_schema();
		}
	}
	emit_changed();
}

void MotionMatchingResource::set_schema(const Ref<MMFeatureSchema> &p_schema) {
	_schema = p_schema;
	if (_cost_function.is_valid() && _schema.is_valid()) {
		_cost_function->rebuild(_schema);
	}
	emit_changed();
}

void MotionMatchingResource::set_cost_function(const Ref<MMCostFunction> &p_cost) {
	_cost_function = p_cost;
	if (_cost_function.is_valid() && _schema.is_valid()) {
		_cost_function->rebuild(_schema);
	}
	emit_changed();
}

// Structured pre-flight check. Deliberately mirrors
// MMAnimationLibraryTools::validate_library()'s issue shape (severity,
// clip, message) rather than inventing a second format — "clip" is left
// empty for resource-level issues, since these aren't about any one clip.
Array MotionMatchingResource::validate() const {
	Array issues;

	if (_animation_library.is_null()) {
		Dictionary issue;
		issue["severity"] = "warning";
		issue["clip"] = String();
		issue["message"] = "No animation_library assigned. This is only required for rebuilding "
							"the database from the editor; a controller using a pre-built database "
							"can run without it, but the Database editor tab needs it to rescan.";
		issues.push_back(issue);
	}

	if (_database.is_null()) {
		Dictionary issue;
		issue["severity"] = "error";
		issue["clip"] = String();
		issue["message"] = "No database assigned. The controller cannot search or play anything "
							"until one is built and assigned.";
		issues.push_back(issue);
		// Nothing else below can be checked meaningfully without a database.
		return issues;
	}

	if (_database->get_frame_count() <= 0) {
		Dictionary issue;
		issue["severity"] = "error";
		issue["clip"] = String();
		issue["message"] = "Database has zero frames. Was it built and finalize()d, or does the "
							"source animation library have no usable clips?";
		issues.push_back(issue);
	}

	if (_database->get_dimension() <= 0) {
		Dictionary issue;
		issue["severity"] = "error";
		issue["clip"] = String();
		issue["message"] = "Database reports zero feature dimensions. It was likely never "
							"finalize()d after building.";
		issues.push_back(issue);
	}

	if (!_database->is_format_compatible()) {
		Dictionary issue;
		issue["severity"] = "warning";
		issue["clip"] = String();
		issue["message"] = vformat(
				"Database format_version (%d) does not match the addon's current version (%d). "
				"It was likely built by a different addon version; rebuilding it is recommended.",
				_database->get_format_version(), MM_DATABASE_FORMAT_VERSION);
		issues.push_back(issue);
	}

	if (_schema.is_valid() && _database->get_dimension() > 0 &&
			_schema->get_dimension() != _database->get_dimension()) {
		Dictionary issue;
		issue["severity"] = "error";
		issue["clip"] = String();
		issue["message"] = vformat(
				"Schema dimension (%d) does not match database dimension (%d). The schema was "
				"likely changed after this database was built; rebuild the database or reassign "
				"the schema the database was actually built with.",
				_schema->get_dimension(), _database->get_dimension());
		issues.push_back(issue);
	}

	if (_schema.is_null() && _database.is_valid()) {
		Ref<MMFeatureSchema> database_schema = _database->get_schema();
		if (database_schema.is_null()) {
			Dictionary issue;
			issue["severity"] = "warning";
			issue["clip"] = String();
			issue["message"] = "No schema assigned on the resource, and the database has none "
								"embedded either. A default schema will be used, which may not "
								"match how this database was actually built.";
			issues.push_back(issue);
		}
	}

	return issues;
}

void MotionMatchingResource::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_animation_library", "library"),
			&MotionMatchingResource::set_animation_library);
	ClassDB::bind_method(D_METHOD("get_animation_library"), &MotionMatchingResource::get_animation_library);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "animation_library", PROPERTY_HINT_RESOURCE_TYPE,
						  "AnimationLibrary"),
			"set_animation_library", "get_animation_library");

	ClassDB::bind_method(D_METHOD("set_database", "database"), &MotionMatchingResource::set_database);
	ClassDB::bind_method(D_METHOD("get_database"), &MotionMatchingResource::get_database);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "database", PROPERTY_HINT_RESOURCE_TYPE,
						  "MMExtraDatabase"),
			"set_database", "get_database");

	ClassDB::bind_method(D_METHOD("set_schema", "schema"), &MotionMatchingResource::set_schema);
	ClassDB::bind_method(D_METHOD("get_schema"), &MotionMatchingResource::get_schema);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "schema", PROPERTY_HINT_RESOURCE_TYPE, "MMFeatureSchema"),
			"set_schema", "get_schema");

	ClassDB::bind_method(D_METHOD("set_cost_function", "cost_function"),
			&MotionMatchingResource::set_cost_function);
	ClassDB::bind_method(D_METHOD("get_cost_function"), &MotionMatchingResource::get_cost_function);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "cost_function", PROPERTY_HINT_RESOURCE_TYPE,
						  "MMCostFunction"),
			"set_cost_function", "get_cost_function");

	MM_BIND_PROPERTY(MotionMatchingResource, Variant::NODE_PATH, skeleton_path)
	MM_BIND_PROPERTY(MotionMatchingResource, Variant::STRING, left_foot_override)
	MM_BIND_PROPERTY(MotionMatchingResource, Variant::STRING, right_foot_override)

	ADD_GROUP("Search", "");
	MM_BIND_PROPERTY(MotionMatchingResource, Variant::FLOAT, search_interval)
	MM_BIND_PROPERTY(MotionMatchingResource, Variant::FLOAT, minimum_clip_time)
	MM_BIND_PROPERTY(MotionMatchingResource, Variant::FLOAT, improvement_threshold)
	MM_BIND_PROPERTY(MotionMatchingResource, Variant::FLOAT, switch_cooldown)
	MM_BIND_PROPERTY(MotionMatchingResource, Variant::INT, max_comparisons)
	MM_BIND_PROPERTY(MotionMatchingResource, Variant::FLOAT, search_budget_msec)
	MM_BIND_PROPERTY(MotionMatchingResource, Variant::INT, neighborhood_radius)

	ADD_GROUP("Playback", "");
	MM_BIND_PROPERTY(MotionMatchingResource, Variant::FLOAT, blend_time)
	MM_BIND_PROPERTY(MotionMatchingResource, Variant::FLOAT, minimum_blend_time)
	MM_BIND_PROPERTY(MotionMatchingResource, Variant::BOOL, allow_same_clip_jump)

	ADD_GROUP("Trajectory", "");
	MM_BIND_PROPERTY(MotionMatchingResource, Variant::FLOAT, trajectory_halflife_position)
	MM_BIND_PROPERTY(MotionMatchingResource, Variant::FLOAT, trajectory_halflife_direction)
	MM_BIND_PROPERTY(MotionMatchingResource, Variant::FLOAT, max_speed)

	ADD_GROUP("Quality", "");
	ClassDB::bind_method(D_METHOD("set_quality", "quality"), &MotionMatchingResource::set_quality);
	ClassDB::bind_method(D_METHOD("get_quality"), &MotionMatchingResource::get_quality);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "quality", PROPERTY_HINT_ENUM, "Ultra,High,Medium,Low"),
			"set_quality", "get_quality");
	MM_BIND_PROPERTY(MotionMatchingResource, Variant::BOOL, debug_enabled)

	ClassDB::bind_method(D_METHOD("validate"), &MotionMatchingResource::validate);
}
