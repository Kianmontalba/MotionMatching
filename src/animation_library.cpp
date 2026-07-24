#include "animation_library.hpp"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void MMAnimationLibraryTools::_bind_methods() {
	ClassDB::bind_static_method("MMAnimationLibraryTools", D_METHOD("auto_tag_library", "library"),
			&MMAnimationLibraryTools::auto_tag_library);
	ClassDB::bind_static_method("MMAnimationLibraryTools",
			D_METHOD("validate_library", "library", "skeleton", "schema"),
			&MMAnimationLibraryTools::validate_library);
	ClassDB::bind_static_method("MMAnimationLibraryTools",
			D_METHOD("merge_libraries", "libraries", "prefixes"),
			&MMAnimationLibraryTools::merge_libraries);
}
