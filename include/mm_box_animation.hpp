#ifndef MM_BOX_ANIMATION_HPP
#define MM_BOX_ANIMATION_HPP

#include "frame_database.hpp"

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// MMBoxAnimation
//
// One named group of databases -- "Normal Rifle", "Pistol", "Zombie", or
// whatever else a designer is organizing weapon/character animation sets by.
// Renaming the box only changes this label; the databases inside are
// untouched. Each entry is its own separate, independently searchable
// MotionMatchingDatabase ("Walk", "Sprint", "Jump", "Traversal", ...) --
// exactly one of them is ever active at a time, this box never merges them.
// ---------------------------------------------------------------------------
class MMBoxAnimation : public Resource {
	GDCLASS(MMBoxAnimation, Resource);

private:
	String _name;
	TypedArray<MotionMatchingDatabase> _databases;

protected:
	static void _bind_methods();

public:
	MM_ACCESSORS(String, name)

	void set_databases(const TypedArray<MotionMatchingDatabase> &p_databases);
	TypedArray<MotionMatchingDatabase> get_databases() const { return _databases; }

	// The "+"/add step: appends a new, empty, renameable database and
	// returns it so the caller (the dock) can select it immediately.
	// Unlimited -- there is no cap on how many databases one box holds.
	Ref<MotionMatchingDatabase> add_database(const String &p_name = String());
	void remove_database(int p_index);

	int get_database_count() const { return _databases.size(); }
	Ref<MotionMatchingDatabase> get_database(int p_index) const;
	Ref<MotionMatchingDatabase> find_database(const String &p_name) const;

	// Swaps in a freshly built database at an existing slot (build_database()
	// always returns a new object, it never edits one in place) while
	// keeping that slot's position in the list.
	void set_database_at(int p_index, const Ref<MotionMatchingDatabase> &p_database);
};

} // namespace godot

#endif // MM_BOX_ANIMATION_HPP
