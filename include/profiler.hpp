#ifndef MM_PROFILER_HPP
#define MM_PROFILER_HPP

#include "mm_types.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// MMProfiler
//
// Ring buffers over the last N ticks. Averages hide spikes and spikes are the
// only thing that matters here, so the percentile readouts are the important
// numbers: a 0.4 ms average with a 9 ms worst case is a stutter, not a win.
// ---------------------------------------------------------------------------
class MMProfiler : public RefCounted {
	GDCLASS(MMProfiler, RefCounted);

private:
	struct Sample {
		double search_usec = 0.0;
		int frames_compared = 0;
		int candidates = 0;
		int nodes = 0;
		bool budget_exceeded = false;
	};

	Vector<Sample> _samples;
	int _capacity = 240;
	int _write = 0;
	int _count = 0;
	int _switches = 0;
	int _searches = 0;
	int _budget_overruns = 0;

protected:
	static void _bind_methods();

public:
	MMProfiler();

	void set_capacity(int p_capacity);
	int get_capacity() const { return _capacity; }

	void record(double p_search_usec, int p_frames_compared, int p_candidates, int p_nodes,
			bool p_budget_exceeded);
	void record_switch();
	void reset();

	double get_average_usec() const;
	double get_percentile_usec(float p_percentile) const;
	double get_worst_usec() const;
	int get_average_frames_compared() const;
	int get_switch_count() const { return _switches; }
	int get_search_count() const { return _searches; }
	int get_budget_overruns() const { return _budget_overruns; }

	// One dictionary for the editor panel and for get_debug_info().
	Dictionary get_report() const;
	PackedFloat64Array get_history_usec() const;
};

} // namespace godot

#endif // MM_PROFILER_HPP
