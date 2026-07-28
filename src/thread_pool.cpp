#include "cache.hpp"

#include "cost_function.hpp"
#include "pose_search.hpp"

using namespace godot;

// ---------------------------------------------------------------------------
// MMSearchWorker
//
// One thread, one pending request, one pending result.
//
// A deeper queue would only add latency: by the time an old request finished,
// the character would already want something else. Overwriting the pending
// request is therefore the correct policy, not a shortcut.
//
// The database and the tree are immutable once built, so the worker reads them
// without a lock. Only the two hand off slots are guarded.
// ---------------------------------------------------------------------------

MMSearchWorker::~MMSearchWorker() {
	stop();
}

void MMSearchWorker::start(const MMPoseSearch *p_search, const MMCostFunction *p_cost) {
	stop();
	_search = p_search;
	_cost = p_cost;
	if (_search == nullptr || _cost == nullptr) {
		return;
	}
	_running.store(true);
	_thread = std::thread(&MMSearchWorker::_thread_main, this);
}

void MMSearchWorker::stop() {
	if (!_running.load()) {
		return;
	}
	_running.store(false);
	_signal.notify_all();
	if (_thread.joinable()) {
		_thread.join();
	}
	_has_request.store(false);
	_has_response.store(false);
}

uint64_t MMSearchWorker::submit(const float *p_query, int p_dimension, const MMSearchFilter &p_filter,
		const MMSearchContext &p_context, int p_max_dimension) {
	if (!_running.load()) {
		return 0;
	}
	ERR_FAIL_COND_V_MSG(p_query == nullptr && p_dimension > 0, 0,
			"MMSearchWorker::submit(): query pointer is null but dimension is nonzero.");

	uint64_t id = 0;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_pending.query.resize(p_dimension);
		for (int i = 0; i < p_dimension; i++) {
			_pending.query.write[i] = p_query[i];
		}
		_pending.filter = p_filter;
		_pending.context = p_context;
		_pending.max_dimension = p_max_dimension;
		_pending.id = _next_id++;
		id = _pending.id;
		_has_request.store(true);
	}
	_signal.notify_one();
	return id;
}

bool MMSearchWorker::poll(Response &r_response) {
	if (!_has_response.load()) {
		return false;
	}
	std::lock_guard<std::mutex> lock(_mutex);
	if (!_has_response.load()) {
		return false;
	}
	r_response = _finished;
	_has_response.store(false);
	return true;
}

void MMSearchWorker::_thread_main() {
	while (_running.load()) {
		Request request;
		{
			std::unique_lock<std::mutex> lock(_mutex);
			_signal.wait(lock, [this]() { return _has_request.load() || !_running.load(); });
			if (!_running.load()) {
				return;
			}
			request = _pending;
			_has_request.store(false);
		}

		if (_search == nullptr || _cost == nullptr || request.query.is_empty()) {
			continue;
		}

		Ref<MMCostFunction> cost = Ref<MMCostFunction>(const_cast<MMCostFunction *>(_cost));

		Response response;
		response.id = request.id;
		response.result = _search->search(request.query.ptr(), cost, request.filter,
				request.context, response.stats, request.max_dimension);

		{
			std::lock_guard<std::mutex> lock(_mutex);
			_finished = response;
			_has_response.store(true);
		}
	}
}
