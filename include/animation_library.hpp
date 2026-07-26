#ifndef MM_ANIMATION_LIBRARY_HPP
#define MM_ANIMATION_LIBRARY_HPP

#include "feature.hpp"
#include "feature_extractor.hpp"
#include "mm_types.hpp"

#include <godot_cpp/classes/animation_library.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {

class MMAnimationLibraryTools : public RefCounted {
	GDCLASS(MMAnimationLibraryTools, RefCounted);

protected:
	static void _bind_methods();

public:
	// Suggests a category and tag mask per clip. The result is a plain
	// dictionary so a tool script or the editor dock can edit it before it is
	// handed to the extractor.
	static Dictionary auto_tag_library(const Ref<AnimationLibrary> &p_library) {
		Dictionary settings;
		ERR_FAIL_COND_V_MSG(p_library.is_null(), settings, "auto_tag_library: library is null.");
		const PackedStringArray names = p_library->get_animation_list();
		for (int i = 0; i < names.size(); i++) {
			const int tags = MMFeatureExtractor::guess_tags_from_name(names[i]);
			Dictionary entry;
			entry["tags"] = tags;
			entry["category"] = MMFeatureExtractor::guess_category_from_tags(tags);
			settings[names[i]] = entry;
		}
		return settings;
	}

	// Reports the problems that make a database useless before hours are spent
	// building one: no root track, missing tracked bones, zero length clips.
	static Array validate_library(const Ref<AnimationLibrary> &p_library, Skeleton3D *p_skeleton,
			const Ref<MMFeatureSchema> &p_schema) {
		Array issues;
		ERR_FAIL_COND_V_MSG(p_library.is_null(), issues, "validate_library: library is null.");
		ERR_FAIL_NULL_V_MSG(p_skeleton, issues, "validate_library: skeleton is null.");
		ERR_FAIL_COND_V_MSG(p_schema.is_null(), issues, "validate_library: schema is null.");

		const String root_bone = p_schema->get_root_bone();
		const PackedStringArray tracked = p_schema->get_pose_bones();

		for (int b = 0; b < tracked.size(); b++) {
			if (p_skeleton->find_bone(tracked[b]) < 0) {
				Dictionary issue;
				issue["severity"] = "error";
				issue["clip"] = String();
				issue["message"] = "Skeleton has no bone named " + tracked[b];
				issues.push_back(issue);
			}
		}

		const PackedStringArray names = p_library->get_animation_list();
		for (int i = 0; i < names.size(); i++) {
			Ref<Animation> animation = p_library->get_animation(names[i]);
			if (animation.is_null()) {
				continue;
			}

			if (animation->get_length() <= 0.0) {
				Dictionary issue;
				issue["severity"] = "error";
				issue["clip"] = names[i];
				issue["message"] = "Clip has zero length";
				issues.push_back(issue);
				continue;
			}

			bool has_root = false;
			for (int t = 0; t < animation->get_track_count(); t++) {
				const NodePath path = animation->track_get_path(t);
				if (path.get_subname_count() > 0 &&
						String(path.get_subname(path.get_subname_count() - 1)) == root_bone) {
					has_root = true;
					break;
				}
			}
			if (!has_root) {
				Dictionary issue;
				issue["severity"] = "warning";
				issue["clip"] = names[i];
				issue["message"] = "No track for root bone " + root_bone +
						"; the hips will be projected instead";
				issues.push_back(issue);
			}
		}
		return issues;
	}

	// Flattens several libraries into one so a single database can span an
	// unarmed set, a rifle set and a traversal set.
	static Ref<AnimationLibrary> merge_libraries(const Array &p_libraries, const PackedStringArray &p_prefixes) {
		Ref<AnimationLibrary> merged;
		merged.instantiate();
		for (int i = 0; i < p_libraries.size(); i++) {
			Ref<AnimationLibrary> library = p_libraries[i];
			if (library.is_null()) {
				continue;
			}
			const String prefix = i < p_prefixes.size() ? p_prefixes[i] : String();
			const PackedStringArray names = library->get_animation_list();
			for (int n = 0; n < names.size(); n++) {
				merged->add_animation(StringName(prefix + names[n]), library->get_animation(names[n]));
			}
		}
		return merged;
	}
};

} // namespace godot

#endif // MM_ANIMATION_LIBRARY_HPP
