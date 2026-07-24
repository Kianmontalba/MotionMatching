#ifdef TOOLS_ENABLED

#include "motion_matching_editor.hpp"

#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

static SpinBox *mm_add_spin(VBoxContainer *p_parent, const String &p_label, double p_min, double p_max,
		double p_step, double p_value) {
	HBoxContainer *row = memnew(HBoxContainer);
	Label *label = memnew(Label);
	label->set_text(p_label);
	label->set_custom_minimum_size(Vector2(220, 0));
	row->add_child(label);

	SpinBox *spin = memnew(SpinBox);
	spin->set_min(p_min);
	spin->set_max(p_max);
	spin->set_step(p_step);
	spin->set_value(p_value);
	row->add_child(spin);

	p_parent->add_child(row);
	return spin;
}

MMTrajectoryEditor::MMTrajectoryEditor() {
	_trajectory.instantiate();

	_halflife_position = mm_add_spin(this, "Velocity halflife (s)", 0.01, 1.0, 0.01, 0.12);
	_halflife_direction = mm_add_spin(this, "Facing halflife (s)", 0.01, 1.0, 0.01, 0.10);
	_max_speed = mm_add_spin(this, "Max speed (m/s)", 0.5, 20.0, 0.1, 6.0);
	_prediction_step = mm_add_spin(this, "Prediction step (s)", 0.005, 0.1, 0.001, 0.0167);

	_preview = memnew(RichTextLabel);
	_preview->set_custom_minimum_size(Vector2(0, 140));
	add_child(_preview);

	_refresh_preview();
}

// A simulated stick release, printed as numbers. Small halflives make the
// predicted line collapse almost instantly, which is what makes a stop clip
// get selected the moment the player lets go.
void MMTrajectoryEditor::_refresh_preview() {
	_trajectory->set_halflife_position((float)_halflife_position->get_value());
	_trajectory->set_halflife_direction((float)_halflife_direction->get_value());
	_trajectory->set_max_speed((float)_max_speed->get_value());
	_trajectory->set_prediction_step((float)_prediction_step->get_value());

	const float speed = (float)_max_speed->get_value();
	_trajectory->set_state(Vector3(), Vector3(0, 0, -speed), Vector3(0, 0, -1));
	_trajectory->set_desired_velocity(Vector3());
	_trajectory->set_desired_facing(Vector3(0, 0, -1));
	_trajectory->update(0.0166);

	_preview->clear();
	_preview->add_text("Stop response at full speed, stick released:\n");
	for (int i = 0; i < _trajectory->get_sample_count(); i++) {
		const Vector3 position = _trajectory->get_sample_position(i);
		const Vector3 velocity = _trajectory->get_sample_velocity(i);
		_preview->add_text(vformat("  sample %d: travels %.2f m, speed %.2f m/s\n", i,
				position.length(), velocity.length()));
	}
	_preview->add_text(
			"\nShorter halflives mean snappier stops and turns. Longer ones mean smoother, "
			"heavier movement. Match this to the weight of the character.");
}

void MMTrajectoryEditor::_on_value_changed(double p_value) {
	_refresh_preview();
}

void MMTrajectoryEditor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_value_changed", "value"), &MMTrajectoryEditor::_on_value_changed);
}

#endif // TOOLS_ENABLED
