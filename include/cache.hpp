#ifndef MM_CACHE_HPP
#define MM_CACHE_HPP

#include "mm_types.hpp"

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace godot {

class MMPoseSearch;
class MMCostFunction;

// ---------------------------------------------------------------------------
// MMSearchCache
//
// Small, allocation free caches that exploit the fact that motion changes
// smoothly. The previous match cache alone removes most of the tree descents
// during steady state locomotion.
// ---------------------------------------------------------------------------
class MMSearchCache {
private:
	struct Entry {
		uint64_t key = 0;
		int frame = -1;
		float cost = MM_INFINITY;
		uint32_t stamp = 0;
	};

	Vector<Entry> _entries;
	uint32_t _stamp = 0;
	int _hits = 0;
	int _misses = 0;

public:
	void resize(int p_capacity);
	void clear();

	// Quantized query key. Two queries that round to the same cell reuse the
	// same answer, which is safe because the cost surface is smooth.
	static uint64_t make_key(const float *p_query, int p_dimension, float p_quantization);

	bool lookup(uint64_t p_key, int &r_frame, float &r_cost);
	void store(uint64_t p_key, int p_frame, float p_cost);

	void begin_frame() { _stamp++; }
	int get_hits() const { return _hits; }
	int get_misses() const { return _misses; }
	void reset_counters() {
		_hits = 0;
		_misses = 0;
	}
};

// ---------------------------------------------------------------------------
// MMSearchWorker
//
// Single background thread with a one slot request queue and a one slot result
// queue. Motion matching only ever needs the most recent request, so a deeper
// queue would just add latency: a newer request simply overwrites the pending
// one.
//
// The database is read only once built, so the worker needs no lock on the
// feature block itself. Only the request and result slots are guarded.
// ---------------------------------------------------------------------------
class MMSearchWorker {
public:
	struct Request {
		Vector<float> query;
		MMSearchFilter filter;
		MMSearchContext context;
		uint64_t id = 0;
		// -1 means "use the tree's full dimension" -- see
		// MMPoseSearch::search()'s p_max_dimension parameter. Kept separate
		// from query's own size (always the full query buffer) rather than
		// truncating the copy itself, so a mismatched buffer length can
		// never read past what was actually copied.
		int max_dimension = -1;
	};

	struct Response {
		MMMatchResult result;
		MMSearchStats stats;
		uint64_t id = 0;
	};

private:
	std::thread _thread;
	std::mutex _mutex;
	std::condition_variable _signal;
	std::atomic<bool> _running{ false };
	std::atomic<bool> _has_request{ false };
	std::atomic<bool> _has_response{ false };

	Request _pending;
	Response _finished;
	uint64_t _next_id = 1;

	const MMPoseSearch *_search = nullptr;
	const MMCostFunction *_cost = nullptr;

	void _thread_main();

public:
	MMSearchWorker() = default;
	~MMSearchWorker();

	void start(const MMPoseSearch *p_search, const MMCostFunction *p_cost);
	void stop();
	bool is_running() const { return _running.load(); }

	// Overwrites any request that has not been picked up yet. p_max_dimension
	// is forwarded to MMPoseSearch::search() unchanged -- see its comment.
	uint64_t submit(const float *p_query, int p_dimension, const MMSearchFilter &p_filter,
			const MMSearchContext &p_context, int p_max_dimension = -1);

	// Non blocking. Returns false when the worker is still thinking.
	bool poll(Response &r_response);
};

} // namespace godot

#endif // MM_CACHE_HPP
