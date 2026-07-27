#include "mm_box_animation.hpp"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void MMBoxAnimation::set_databases(const TypedArray<MotionMatchingDatabase> &p_databases) {
	_databases = p_databases;
	emit_changed();
}

Ref<MotionMatchingDatabase> MMBoxAnimation::add_database(const String &p_name) {
	Ref<MotionMatchingDatabase> database;
	database.instantiate();
	if (!p_name.is_empty()) {
		database->set_name(p_name);
	}
	_databases.push_back(database);
	emit_changed();
	return database;
}

void MMBoxAnimation::remove_database(int p_index) {
	if (p_index < 0 || p_index >= _databases.size()) {
		return;
	}
	_databases.remove_at(p_index);
	emit_changed();
}

Ref<MotionMatchingDatabase> MMBoxAnimation::get_database(int p_index) const {
	if (p_index < 0 || p_index >= _databases.size()) {
		return Ref<MotionMatchingDatabase>();
	}
	return _databases[p_index];
}

Ref<MotionMatchingDatabase> MMBoxAnimation::find_database(const String &p_name) const {
	for (int i = 0; i < _databases.size(); i++) {
		Ref<MotionMatchingDatabase> database = _databases[i];
		if (database.is_valid() && database->get_name() == p_name) {
			return database;
		}
	}
	return Ref<MotionMatchingDatabase>();
}

void MMBoxAnimation::set_database_at(int p_index, const Ref<MotionMatchingDatabase> &p_database) {
	if (p_index < 0 || p_index >= _databases.size()) {
		return;
	}
	_databases[p_index] = p_database;
	emit_changed();
}

void MMBoxAnimation::_bind_methods() {
	MM_BIND_PROPERTY(MMBoxAnimation, Variant::STRING, name)

	ClassDB::bind_method(D_METHOD("set_databases", "databases"), &MMBoxAnimation::set_databases);
	ClassDB::bind_method(D_METHOD("get_databases"), &MMBoxAnimation::get_databases);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "databases", PROPERTY_HINT_ARRAY_TYPE,
						  "24/17:MotionMatchingDatabase"),
			"set_databases", "get_databases");

	ClassDB::bind_method(D_METHOD("add_database", "name"), &MMBoxAnimation::add_database, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("remove_database", "index"), &MMBoxAnimation::remove_database);
	ClassDB::bind_method(D_METHOD("get_database_count"), &MMBoxAnimation::get_database_count);
	ClassDB::bind_method(D_METHOD("get_database", "index"), &MMBoxAnimation::get_database);
	ClassDB::bind_method(D_METHOD("find_database", "name"), &MMBoxAnimation::find_database);
	ClassDB::bind_method(D_METHOD("set_database_at", "index", "database"), &MMBoxAnimation::set_database_at);
}
