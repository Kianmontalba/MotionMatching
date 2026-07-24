#include "clip_analysis.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// Classification is deliberately ordered: the states that must never be
// confused with locomotion are decided first, and locomotion is what is left
// over. A misfiled jump breaks gameplay; a misfiled jog only costs quality.
int MMClipAnalyzer::classify(const MMClipStats &p_stats, const String &p_name) const {
	int tags = 0;

	// --- Name overrides ---------------------------------------------------
	// Applied first when a team has a convention worth trusting, and skipped
	// entirely otherwise. Nothing here is shipped pre-filled.
	if (_use_name_rules && !_name_rules.is_empty() && !p_name.is_empty()) {
		const String lowered = p_name.to_lower();
		const Array keys = _name_rules.keys();
		for (int i = 0; i < keys.size(); i++) {
			const String keyword = String(keys[i]).to_lower();
			if (!keyword.is_empty() && lowered.contains(keyword)) {
				tags |= (int)_name_rules[keys[i]];
			}
		}
		if (tags != 0 && !_use_motion_analysis) {
			return tags;
		}
	}

	if (!_use_motion_analysis) {
		return tags != 0 ? tags : (int)MM_TAG_IDLE;
	}

	// --- Airborne ---------------------------------------------------------
	// No foot on the ground for long enough that it cannot be a running
	// stride. Rising means a takeoff, falling means a descent, and a clip that
	// ends planted after being airborne is a landing.
	if (p_stats.longest_airborne >= _airborne_seconds) {
		if (p_stats.net_height_gain > 0.1f) {
			tags |= MM_TAG_JUMP;
		}
		if (p_stats.net_height_gain < -0.1f) {
			tags |= MM_TAG_FALL;
		}
		if (tags == 0) {
			tags |= MM_TAG_FALL;
		}
		if (p_stats.contact_ratio > 0.25f && p_stats.end_speed < p_stats.peak_speed * 0.5f) {
			tags |= MM_TAG_LAND;
		}
		return tags;
	}

	// --- Traversal --------------------------------------------------------
	// A large vertical excursion while the feet leave the ground and the body
	// ends up somewhere it could not have walked to.
	if (p_stats.vertical_range >= _traversal_height && p_stats.contact_ratio < 0.7f) {
		if (p_stats.net_height_gain > _traversal_height * 0.8f) {
			tags |= MM_TAG_MANTLE;
		} else if (Math::abs(p_stats.net_height_gain) < _traversal_height * 0.4f &&
				p_stats.net_displacement > 0.8f) {
			tags |= MM_TAG_VAULT;
		} else if (p_stats.net_height_gain > _traversal_height * 1.6f) {
			tags |= MM_TAG_CLIMB;
		} else {
			tags |= MM_TAG_VAULT;
		}
		return tags;
	}

	// --- Ground level special cases --------------------------------------
	if (p_stats.crouch_ratio > 0.6f) {
		tags |= MM_TAG_CROUCH;
		if (p_stats.crouch_ratio > 0.9f && p_stats.average_speed > _walk_speed) {
			tags |= MM_TAG_SLIDE;
		}
	}

	// --- Gait -------------------------------------------------------------
	const float speed = p_stats.average_speed;
	if (speed < _idle_speed) {
		tags |= MM_TAG_IDLE;
	} else if (speed < _walk_speed) {
		tags |= MM_TAG_WALK;
	} else if (speed < _jog_speed) {
		tags |= MM_TAG_JOG;
	} else if (speed < _run_speed) {
		tags |= MM_TAG_RUN;
	} else {
		tags |= MM_TAG_SPRINT;
	}

	// --- Motion shape -----------------------------------------------------
	// Direction relative to the character's own facing, so a clip that travels
	// sideways is a strafe whether or not anyone named it one.
	if (p_stats.lateral_ratio > _strafe_ratio) {
		tags |= MM_TAG_STRAFE;
	}
	if (p_stats.backward_ratio > _backward_ratio) {
		tags |= MM_TAG_STRAFE;
	}

	if (p_stats.total_turn > _turn_threshold) {
		// A turn in place pivots; a turn while travelling is a moving turn.
		tags |= p_stats.net_displacement < 0.5f ? MM_TAG_PIVOT : MM_TAG_TURN;
	}

	// --- Transitions ------------------------------------------------------
	const float delta = p_stats.end_speed - p_stats.start_speed;
	if (delta > _start_stop_delta) {
		tags |= MM_TAG_START;
	} else if (delta < -_start_stop_delta) {
		tags |= MM_TAG_STOP;
	}

	if (tags == 0) {
		tags = MM_TAG_IDLE;
	}
	return tags;
}

int MMClipAnalyzer::category_for_tags(int p_tags) {
	if (p_tags & (MM_TAG_JUMP | MM_TAG_FALL | MM_TAG_LAND)) {
		return MM_CATEGORY_AIRBORNE;
	}
	if (p_tags & (MM_TAG_VAULT | MM_TAG_MANTLE | MM_TAG_CLIMB | MM_TAG_ROLL)) {
		return MM_CATEGORY_TRAVERSAL;
	}
	if (p_tags & (MM_TAG_ATTACK | MM_TAG_RELOAD | MM_TAG_HIT)) {
		return MM_CATEGORY_COMBAT;
	}
	return MM_CATEGORY_LOCOMOTION;
}

Dictionary MMClipAnalyzer::stats_to_dictionary(const MMClipStats &p_stats) {
	Dictionary stats;
	stats["duration"] = p_stats.duration;
	stats["hip_height"] = p_stats.hip_height;
	stats["average_speed"] = p_stats.average_speed;
	stats["peak_speed"] = p_stats.peak_speed;
	stats["start_speed"] = p_stats.start_speed;
	stats["end_speed"] = p_stats.end_speed;
	stats["net_displacement"] = p_stats.net_displacement;
	stats["vertical_range"] = p_stats.vertical_range;
	stats["net_height_gain"] = p_stats.net_height_gain;
	stats["total_turn"] = p_stats.total_turn;
	stats["lateral_ratio"] = p_stats.lateral_ratio;
	stats["backward_ratio"] = p_stats.backward_ratio;
	stats["contact_ratio"] = p_stats.contact_ratio;
	stats["longest_airborne"] = p_stats.longest_airborne;
	stats["crouch_ratio"] = p_stats.crouch_ratio;
	return stats;
}

int MMClipAnalyzer::classify_dictionary(const Dictionary &p_stats, const String &p_name) const {
	MMClipStats stats;
	stats.duration = p_stats.get("duration", 0.0f);
	stats.hip_height = p_stats.get("hip_height", 1.0f);
	stats.average_speed = p_stats.get("average_speed", 0.0f);
	stats.peak_speed = p_stats.get("peak_speed", 0.0f);
	stats.start_speed = p_stats.get("start_speed", 0.0f);
	stats.end_speed = p_stats.get("end_speed", 0.0f);
	stats.net_displacement = p_stats.get("net_displacement", 0.0f);
	stats.vertical_range = p_stats.get("vertical_range", 0.0f);
	stats.net_height_gain = p_stats.get("net_height_gain", 0.0f);
	stats.total_turn = p_stats.get("total_turn", 0.0f);
	stats.lateral_ratio = p_stats.get("lateral_ratio", 0.0f);
	stats.backward_ratio = p_stats.get("backward_ratio", 0.0f);
	stats.contact_ratio = p_stats.get("contact_ratio", 1.0f);
	stats.longest_airborne = p_stats.get("longest_airborne", 0.0f);
	stats.crouch_ratio = p_stats.get("crouch_ratio", 0.0f);
	return classify(stats, p_name);
}

// Self calibration. Rather than trusting a fixed scale, the thresholds are
// placed at the natural quartile gaps of the moving clips' speeds, so a
// library authored in an unfamiliar convention or for a non-human rig still
// lands on sensible walk/jog/run/sprint bands without anyone typing a number.
void MMClipAnalyzer::calibrate_speed_bands(const PackedFloat32Array &p_clip_average_speeds) {
	PackedFloat32Array speeds;
	for (int i = 0; i < p_clip_average_speeds.size(); i++) {
		if (p_clip_average_speeds[i] > _idle_speed * 0.5f) {
			speeds.push_back(p_clip_average_speeds[i]);
		}
	}
	if (speeds.size() < 8) {
		return; // Not enough evidence; keep the current bands.
	}

	speeds.sort();
	const int count = speeds.size();

	const float q1 = speeds[count / 4];
	const float q2 = speeds[count / 2];
	const float q3 = speeds[(count * 3) / 4];
	const float top = speeds[count - 1];

	_idle_speed = MAX(0.02f, speeds[0] * 0.5f);
	_walk_speed = q1;
	_jog_speed = q2;
	_run_speed = top <= q3 * 1.05f ? (q2 + top) * 0.5f : q3;

	// Turning and stopping scale with the same evidence: a rig that moves
	// slowly in hip heights per second also turns and stops proportionally.
	_start_stop_delta = MAX(0.2f, _jog_speed * 0.35f);

	emit_changed();
}

void MMClipAnalyzer::_bind_methods() {	ADD_GROUP("Speed bands (hip heights per second)", "");
	MM_BIND_PROPERTY(MMClipAnalyzer, Variant::FLOAT, idle_speed)
	MM_BIND_PROPERTY(MMClipAnalyzer, Variant::FLOAT, walk_speed)
	MM_BIND_PROPERTY(MMClipAnalyzer, Variant::FLOAT, jog_speed)
	MM_BIND_PROPERTY(MMClipAnalyzer, Variant::FLOAT, run_speed)

	ADD_GROUP("Motion shape", "");
	MM_BIND_PROPERTY(MMClipAnalyzer, Variant::FLOAT, turn_threshold)
	MM_BIND_PROPERTY(MMClipAnalyzer, Variant::FLOAT, strafe_ratio)
	MM_BIND_PROPERTY(MMClipAnalyzer, Variant::FLOAT, backward_ratio)
	MM_BIND_PROPERTY(MMClipAnalyzer, Variant::FLOAT, crouch_drop)
	MM_BIND_PROPERTY(MMClipAnalyzer, Variant::FLOAT, airborne_seconds)
	MM_BIND_PROPERTY(MMClipAnalyzer, Variant::FLOAT, traversal_height)
	MM_BIND_PROPERTY(MMClipAnalyzer, Variant::FLOAT, start_stop_delta)

	ADD_GROUP("Name rules (optional override)", "");
	MM_BIND_PROPERTY(MMClipAnalyzer, Variant::DICTIONARY, name_rules)
	MM_BIND_PROPERTY(MMClipAnalyzer, Variant::BOOL, use_name_rules)
	MM_BIND_PROPERTY(MMClipAnalyzer, Variant::BOOL, use_motion_analysis)

	ClassDB::bind_method(D_METHOD("classify_dictionary", "stats", "name"),
			&MMClipAnalyzer::classify_dictionary);
	ClassDB::bind_static_method("MMClipAnalyzer", D_METHOD("category_for_tags", "tags"),
			&MMClipAnalyzer::category_for_tags);
	ClassDB::bind_method(D_METHOD("calibrate_speed_bands", "clip_average_speeds"),
			&MMClipAnalyzer::calibrate_speed_bands);
}
