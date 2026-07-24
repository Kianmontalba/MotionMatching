#include "cache.hpp"

#include "cost_function.hpp"
#include "pose_search.hpp"

using namespace godot;

// ---------------------------------------------------------------------------
// MMSearchCache
//
// A direct mapped cache keyed on a quantized query. Motion matching queries
// move smoothly, so two consecutive ticks very often land in the same cell and
// the second one costs a single array lookup instead of a tree descent.
// ---------------------------------------------------------------------------

void MMSearchCache::resize(int p_capacity) {
	const int capacity = MAX(16, p_capacity);
	_entries.resize(capacity);
	clear();
}

void MMSearchCache::clear() {
	for (int i = 0; i < _entries.size(); i++) {
		_entries.write[i] = Entry();
	}
	_stamp = 0;
	reset_counters();
}

uint64_t MMSearchCache::make_key(const float *p_query, int p_dimension, float p_quantization) {
	// FNV style mix over the quantized query. Cheap, no allocation, and stable
	// across platforms because it only ever sees integers.
	uint64_t hash = 1469598103934665603ULL;
	const float inverse = 1.0f / MAX(p_quantization, 0.0001f);
	for (int i = 0; i < p_dimension; i++) {
		const int32_t cell = (int32_t)(p_query[i] * inverse);
		hash ^= (uint64_t)(uint32_t)cell;
		hash *= 1099511628211ULL;
	}
	return hash;
}

bool MMSearchCache::lookup(uint64_t p_key, int &r_frame, float &r_cost) {
	if (_entries.is_empty()) {
		_misses++;
		return false;
	}
	const int slot = (int)(p_key % (uint64_t)_entries.size());
	const Entry &entry = _entries[slot];
	if (entry.key == p_key && entry.frame >= 0) {
		r_frame = entry.frame;
		r_cost = entry.cost;
		_hits++;
		return true;
	}
	_misses++;
	return false;
}

void MMSearchCache::store(uint64_t p_key, int p_frame, float p_cost) {
	if (_entries.is_empty()) {
		return;
	}
	const int slot = (int)(p_key % (uint64_t)_entries.size());
	Entry &entry = _entries.write[slot];
	entry.key = p_key;
	entry.frame = p_frame;
	entry.cost = p_cost;
	entry.stamp = _stamp;
}
