#include "profiler.hpp"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

MMProfiler::MMProfiler() {
	_samples.resize(_capacity);
}

void MMProfiler::set_capacity(int p_capacity) {
	_capacity = MAX(8, p_capacity);
	_samples.resize(_capacity);
	reset();
}

void MMProfiler::record(double p_search_usec, int p_frames_compared, int p_candidates, int p_nodes,
		bool p_budget_exceeded) {
	Sample sample;
	sample.search_usec = p_search_usec;
	sample.frames_compared = p_frames_compared;
	sample.candidates = p_candidates;
	sample.nodes = p_nodes;
	sample.budget_exceeded = p_budget_exceeded;

	_samples.write[_write] = sample;
	_write = (_write + 1) % _capacity;
	_count = MIN(_count + 1, _capacity);
	_searches++;
	if (p_budget_exceeded) {
		_budget_overruns++;
	}
}

void MMProfiler::record_switch() {
	_switches++;
}

void MMProfiler::reset() {
	_write = 0;
	_count = 0;
	_switches = 0;
	_searches = 0;
	_budget_overruns = 0;
}

double MMProfiler::get_average_usec() const {
	if (_count == 0) {
		return 0.0;
	}
	double total = 0.0;
	for (int i = 0; i < _count; i++) {
		total += _samples[i].search_usec;
	}
	return total / (double)_count;
}

double MMProfiler::get_percentile_usec(float p_percentile) const {
	if (_count == 0) {
		return 0.0;
	}
	Vector<double> sorted;
	sorted.resize(_count);
	for (int i = 0; i < _count; i++) {
		sorted.write[i] = _samples[i].search_usec;
	}
	// Insertion sort: the buffer is small and this avoids pulling in a sort
	// dependency for a debug path.
	for (int i = 1; i < _count; i++) {
		const double value = sorted[i];
		int j = i - 1;
		while (j >= 0 && sorted[j] > value) {
			sorted.write[j + 1] = sorted[j];
			j--;
		}
		sorted.write[j + 1] = value;
	}
	const int index = CLAMP((int)(p_percentile * (float)(_count - 1)), 0, _count - 1);
	return sorted[index];
}

double MMProfiler::get_worst_usec() const {
	double worst = 0.0;
	for (int i = 0; i < _count; i++) {
		worst = MAX(worst, _samples[i].search_usec);
	}
	return worst;
}

int MMProfiler::get_average_frames_compared() const {
	if (_count == 0) {
		return 0;
	}
	int64_t total = 0;
	for (int i = 0; i < _count; i++) {
		total += _samples[i].frames_compared;
	}
	return (int)(total / _count);
}

Dictionary MMProfiler::get_report() const {
	Dictionary report;
	report["samples"] = _count;
	report["searches"] = _searches;
	report["switches"] = _switches;
	report["budget_overruns"] = _budget_overruns;
	report["average_usec"] = get_average_usec();
	report["median_usec"] = get_percentile_usec(0.5f);
	report["p95_usec"] = get_percentile_usec(0.95f);
	report["p99_usec"] = get_percentile_usec(0.99f);
	report["worst_usec"] = get_worst_usec();
	report["average_frames_compared"] = get_average_frames_compared();
	return report;
}

PackedFloat64Array MMProfiler::get_history_usec() const {
	PackedFloat64Array history;
	history.resize(_count);
	for (int i = 0; i < _count; i++) {
		const int index = (_write - _count + i + _capacity) % _capacity;
		history.set(i, _samples[index].search_usec);
	}
	return history;
}

void MMProfiler::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_capacity", "capacity"), &MMProfiler::set_capacity);
	ClassDB::bind_method(D_METHOD("get_capacity"), &MMProfiler::get_capacity);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "capacity"), "set_capacity", "get_capacity");

	ClassDB::bind_method(D_METHOD("record", "search_usec", "frames_compared", "candidates", "nodes",
								 "budget_exceeded"),
			&MMProfiler::record);
	ClassDB::bind_method(D_METHOD("record_switch"), &MMProfiler::record_switch);
	ClassDB::bind_method(D_METHOD("reset"), &MMProfiler::reset);
	ClassDB::bind_method(D_METHOD("get_average_usec"), &MMProfiler::get_average_usec);
	ClassDB::bind_method(D_METHOD("get_percentile_usec", "percentile"), &MMProfiler::get_percentile_usec);
	ClassDB::bind_method(D_METHOD("get_worst_usec"), &MMProfiler::get_worst_usec);
	ClassDB::bind_method(D_METHOD("get_report"), &MMProfiler::get_report);
	ClassDB::bind_method(D_METHOD("get_history_usec"), &MMProfiler::get_history_usec);
}
