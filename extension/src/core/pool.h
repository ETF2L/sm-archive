/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The worker pool: threads that take queued jobs and keep their outcomes
 * until the main thread collects them. Jobs start in the order they were
 * queued, high priority ones first, and a normal job that waited longer than
 * the aging time counts as high. Jobs of one serial lane run one after the
 * other in their order, whatever their priority. */
#ifndef SMARCHIVE_CORE_POOL_H
#define SMARCHIVE_CORE_POOL_H

#include "jobs.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace smarchive {

using LaneId = uint64_t;
/* The lane of jobs that run in parallel with every other. */
constexpr LaneId kNoLane = 0;

/* The values of the plugin API's ArchivePriority. */
enum class Priority : int {
	Normal = 0,
	High = 1,
};

using Clock = std::chrono::steady_clock;

constexpr std::chrono::seconds kDefaultAging{30};
constexpr int kMinThreads = 1;
constexpr int kMaxThreads = 8;
constexpr int kDefaultThreads = 2;

struct QueuedJob {
	JobId id;
	LaneId lane;
	Priority priority;
	Clock::time_point queuedAt;
};

/* The job to start next, as its index in queue, or queue.size() for none:
 * the first one that may start (no lane, or its lane idle and no earlier job
 * of its lane queued) among those that count as high, else the first one
 * that may start. */
size_t PickNext(const std::deque<QueuedJob> &queue, const std::set<LaneId> &busyLanes, Clock::time_point now,
                Clock::duration aging);

struct FinishedJob {
	JobId id;
	JobOutcome outcome;
};

/* What sm_archive_status shows. */
struct PoolState {
	struct Entry {
		JobId id;
		LaneId lane;
		Priority priority;
		bool isRunning;
		Clock::time_point queuedAt;
	};
	std::vector<Entry> jobs;
	int threads;
};

class Pool {
public:
	/* Runs a job as its id; RunJob() unless a test fakes it. */
	using Runner = std::function<JobOutcome(JobId, const Job &, const std::atomic<bool> &, Progress &)>;

	explicit Pool(int threads = kDefaultThreads, Runner runner = RunJob, Clock::duration aging = kDefaultAging);
	/* Stops like Stop(). */
	~Pool();
	Pool(const Pool &) = delete;
	Pool &operator=(const Pool &) = delete;

	/* Queues the job; its progress is written to progress. */
	void Submit(JobId id, Job job, LaneId lane, Priority priority, std::shared_ptr<Progress> progress);

	/* A queued job is dropped and finishes as Cancelled at once; a running
	 * one is told to stop and finishes as whatever it then ends with. False
	 * for a job that is neither queued nor running. */
	bool Cancel(JobId id);

	/* Between kMinThreads and kMaxThreads; idle threads beyond it end. */
	void SetThreadCount(int threads);

	/* The jobs finished since the last call, in the order they finished. */
	std::vector<FinishedJob> TakeFinished();

	PoolState State();

	/* Stops every running job, drops the queued ones and every outcome and
	 * joins the threads. Nothing runs after it returns. */
	void Stop();

private:
	struct Pending {
		QueuedJob schedule;
		Job job;
		std::shared_ptr<Progress> progress;
	};
	struct Running {
		LaneId lane;
		Priority priority;
		Clock::time_point queuedAt;
		std::shared_ptr<std::atomic<bool>> abort;
	};
	struct Worker {
		std::thread thread;
		std::shared_ptr<std::atomic<bool>> hasEnded;
	};

	void Work(std::shared_ptr<std::atomic<bool>> hasEnded);
	void StartWorkers();
	void JoinEndedWorkers();
	size_t PickNextLocked() const;

	Runner m_runner;
	Clock::duration m_aging;
	std::mutex m_mutex;
	std::condition_variable m_wakeUp;
	std::deque<QueuedJob> m_order;
	std::map<JobId, Pending> m_queued;
	std::map<JobId, Running> m_running;
	std::set<LaneId> m_busyLanes;
	std::vector<FinishedJob> m_finished;
	std::vector<Worker> m_workers;
	int m_targetThreads;
	int m_liveThreads = 0;
	bool m_isStopping = false;
};

} // namespace smarchive

#endif // SMARCHIVE_CORE_POOL_H
