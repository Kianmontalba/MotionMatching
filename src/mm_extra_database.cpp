#include "mm_extra_database.hpp"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void MMExtraDatabase::set_boxes(const TypedArray<MMBoxAnimation> &p_boxes) {
	_boxes = p_boxes;
	emit_changed();
}

Ref<MMBoxAnimation> MMExtraDatabase::add_box(const String &p_name) {
	Ref<MMBoxAnimation> box;
	box.instantiate();
	if (!p_name.is_empty()) {
		box->set_name(p_name);
	}
	_boxes.push_back(box);
	emit_changed();
	return box;
}

void MMExtraDatabase::remove_box(int p_index) {
	if (p_index < 0 || p_index >= _boxes.size()) {
		return;
	}
	_boxes.remove_at(p_index);
	emit_changed();
}

Ref<MMBoxAnimation> MMExtraDatabase::get_box(int p_index) const {
	if (p_index < 0 || p_index >= _boxes.size()) {
		return Ref<MMBoxAnimation>();
	}
	return _boxes[p_index];
}

Ref<MMBoxAnimation> MMExtraDatabase::find_box(const String &p_name) const {
	for (int i = 0; i < _boxes.size(); i++) {
		Ref<MMBoxAnimation> box = _boxes[i];
		if (box.is_valid() && box->get_name() == p_name) {
			return box;
		}
	}
	return Ref<MMBoxAnimation>();
}

Ref<MotionMatchingDatabase> MMExtraDatabase::get_active_database() const {
	Ref<MMBoxAnimation> box = get_box(_active_box_index);
	if (box.is_null()) {
		return Ref<MotionMatchingDatabase>();
	}
	return box->get_database(_active_database_index);
}

bool MMExtraDatabase::set_active(const String &p_box_name, const String &p_database_name) {
	for (int i = 0; i < _boxes.size(); i++) {
		Ref<MMBoxAnimation> box = _boxes[i];
		if (box.is_null() || box->get_name() != p_box_name) {
			continue;
		}
		const TypedArray<MotionMatchingDatabase> databases = box->get_databases();
		for (int j = 0; j < databases.size(); j++) {
			Ref<MotionMatchingDatabase> database = databases[j];
			if (database.is_valid() && database->get_name() == p_database_name) {
				_active_box_index = i;
				_active_database_index = j;
				emit_changed();
				return true;
			}
		}
		return false;
	}
	return false;
}

void MMExtraDatabase::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_boxes", "boxes"), &MMExtraDatabase::set_boxes);
	ClassDB::bind_method(D_METHOD("get_boxes"), &MMExtraDatabase::get_boxes);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "boxes", PROPERTY_HINT_ARRAY_TYPE,
						  "24/17:MMBoxAnimation"),
			"set_boxes", "get_boxes");

	ClassDB::bind_method(D_METHOD("add_box", "name"), &MMExtraDatabase::add_box, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("remove_box", "index"), &MMExtraDatabase::remove_box);
	ClassDB::bind_method(D_METHOD("get_box_count"), &MMExtraDatabase::get_box_count);
	ClassDB::bind_method(D_METHOD("get_box", "index"), &MMExtraDatabase::get_box);
	ClassDB::bind_method(D_METHOD("find_box", "name"), &MMExtraDatabase::find_box);

	MM_BIND_PROPERTY(MMExtraDatabase, Variant::INT, active_box_index)
	MM_BIND_PROPERTY(MMExtraDatabase, Variant::INT, active_database_index)

	ClassDB::bind_method(D_METHOD("get_active_database"), &MMExtraDatabase::get_active_database);
	ClassDB::bind_method(D_METHOD("set_active", "box_name", "database_name"), &MMExtraDatabase::set_active);
}
