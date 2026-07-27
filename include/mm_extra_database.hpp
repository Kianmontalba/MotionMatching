#ifndef MM_EXTRA_DATABASE_HPP
#define MM_EXTRA_DATABASE_HPP

#include "mm_box_animation.hpp"

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// MMExtraDatabase
//
// Lives where a single MotionMatchingDatabase used to sit directly on
// MotionMatchingResource. Holds nothing of its own except an "add" list of
// MMBoxAnimation -- one box per weapon or character animation set.
// Deliberately kept empty otherwise: naming and per-database detail live one
// level down, in MMBoxAnimation and MotionMatchingDatabase respectively.
// ---------------------------------------------------------------------------
class MMExtraDatabase : public Resource {
	GDCLASS(MMExtraDatabase, Resource);

private:
	TypedArray<MMBoxAnimation> _boxes;

	// Which box/database inside it the controller actually searches against
	// right now. Indices, not object pointers, so the choice survives a
	// .tres round trip. Only ever one database is active at a time.
	int _active_box = 0;
	int _active_database = 0;

protected:
	static void _bind_methods();

public:
	void set_boxes(const TypedArray<MMBoxAnimation> &p_boxes);
	TypedArray<MMBoxAnimation> get_boxes() const { return _boxes; }

	// The "+"/add step: appends a new, empty, renameable box and returns it.
	// Unlimited -- there is no cap on how many boxes this holds.
	Ref<MMBoxAnimation> add_box(const String &p_name = String());
	void remove_box(int p_index);

	int get_box_count() const { return _boxes.size(); }
	Ref<MMBoxAnimation> get_box(int p_index) const;
	Ref<MMBoxAnimation> find_box(const String &p_name) const;

	MM_ACCESSORS(int, active_box)
	MM_ACCESSORS(int, active_database)

	// Whichever single database _active_box/_active_database currently point
	// at -- this is what the controller runs its search against. Null if
	// nothing has been added yet.
	Ref<MotionMatchingDatabase> get_active_database() const;

	// Points the active selection at a box/database found by name, for game
	// or editor code that wants to switch sets ("Pistol" -> "Sprint").
	// Returns false (and leaves the selection unchanged) if either name
	// isn't found.
	bool set_active(const String &p_box_name, const String &p_database_name);
};

} // namespace godot

#endif // MM_EXTRA_DATABASE_HPP
