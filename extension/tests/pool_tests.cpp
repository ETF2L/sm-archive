/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The scheduling rule with a fake clock, and the pool's threads with a fake
 * runner that records the order jobs start in and waits for a go. */
#include "check.h"
#include "pool.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>

using namespace smarchive;
using namespace std::chrono_literals;

namespace {

QueuedJob Queued(JobId id, LaneId lane, Priority priority, Clock::time_point at)
{
	return QueuedJob{id, lane, priority, at};
}

/* Runs jobs only when released, and notes when each started. */
class FakeRunner {
public:
	JobOutcome Run(const Job &job, const std::atomic<bool> &abort, Progress &)
	{
		JobId id = std::get<ListJob>(job.spec).archivePath.size();
		std::unique_lock<std::mutex> lock(m_mutex);
		m_started.push_back(id);
		m_changed.notify_all();
		/* The abort flag notifies nobody, so it is polled. */
		while (m_released.count(id) == 0 && !abort.load())
			m_changed.wait_for(lock, 10ms);
		JobOutcome outcome;
		if (abort.load())
			outcome.status = Status::Fail(ErrorCode::Cancelled, "cancelled");
		return outcome;
	}

	void Release(JobId id)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_released.insert(id);
		m_changed.notify_all();
	}

	/* Waits until count jobs started; their ids in start order. */
	std::vector<JobId> WaitForStarts(size_t count)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_changed.wait_for(lock, 5s, [&] { return m_started.size() >= count; });
		return m_started;
	}

private:
	std::mutex m_mutex;
	std::condition_variable m_changed;
	std::vector<JobId> m_started;
	std::set<JobId> m_released;
};

/* A job the fake runner knows by its id: the archive path's length. */
Job FakeJob(JobId id)
{
	return Job{ListJob{std::string(id, 'x'), ArchiveChoice{}}, kDefaultLimits};
}

/* Jobs started together on different threads record their starts in any
 * order, so the first count starts are compared as a set. */
std::set<JobId> FirstStarts(const std::vector<JobId> &starts, size_t count)
{
	return std::set<JobId>(starts.begin(), starts.begin() + std::min(count, starts.size()));
}

std::vector<FinishedJob> WaitForFinished(Pool &pool, size_t count)
{
	std::vector<FinishedJob> finished;
	for (int i = 0; i < 500 && finished.size() < count; i++) {
		for (FinishedJob &job : pool.TakeFinished())
			finished.push_back(std::move(job));
		std::this_thread::sleep_for(10ms);
	}
	return finished;
}

} // namespace

TEST(HighJobsGoFirst)
{
	Clock::time_point now = Clock::now();
	std::deque<QueuedJob> queue = {Queued(1, kNoLane, Priority::Normal, now), Queued(2, kNoLane, Priority::High, now)};
	CHECK(PickNext(queue, {}, now, 30s) == 1);
	CHECK(PickNext({}, {}, now, 30s) == 0);
}

TEST(WaitingNormalJobsAge)
{
	Clock::time_point now = Clock::now();
	std::deque<QueuedJob> queue = {Queued(1, kNoLane, Priority::Normal, now - 31s),
	                               Queued(2, kNoLane, Priority::High, now)};
	CHECK(PickNext(queue, {}, now, 30s) == 0);
}

TEST(LanesRunInOrder)
{
	Clock::time_point now = Clock::now();
	std::deque<QueuedJob> queue = {Queued(1, 7, Priority::Normal, now), Queued(2, 7, Priority::High, now),
	                               Queued(3, kNoLane, Priority::Normal, now)};
	/* Job 2 may not overtake job 1 of its lane, however high. */
	CHECK(PickNext(queue, {}, now, 30s) == 0);
	CHECK(PickNext(queue, {7}, now, 30s) == 2);
	std::deque<QueuedJob> laneOnly = {Queued(1, 7, Priority::Normal, now)};
	CHECK(PickNext(laneOnly, {7}, now, 30s) == laneOnly.size());
}

TEST(PoolRunsJobsInParallel)
{
	FakeRunner runner;
	Pool pool(2, [&](JobId, const Job &job, const std::atomic<bool> &abort, Progress &progress) {
		return runner.Run(job, abort, progress);
	});
	for (JobId id = 1; id <= 3; id++)
		pool.Submit(id, FakeJob(id), kNoLane, Priority::Normal, std::make_shared<Progress>());
	CHECK(runner.WaitForStarts(2).size() == 2);
	std::this_thread::sleep_for(50ms);
	CHECK(runner.WaitForStarts(2).size() == 2);
	runner.Release(2);
	std::vector<JobId> starts = runner.WaitForStarts(3);
	CHECK(FirstStarts(starts, 2) == (std::set<JobId>{1, 2}));
	CHECK(starts.size() == 3 && starts[2] == 3);
	runner.Release(1);
	runner.Release(3);
	std::vector<FinishedJob> finished = WaitForFinished(pool, 3);
	CHECK(finished.size() == 3);
	CHECK(!finished.empty() && finished[0].id == 2);
}

TEST(PoolKeepsLanesSerial)
{
	FakeRunner runner;
	Pool pool(4, [&](JobId, const Job &job, const std::atomic<bool> &abort, Progress &progress) {
		return runner.Run(job, abort, progress);
	});
	pool.Submit(1, FakeJob(1), 9, Priority::Normal, std::make_shared<Progress>());
	pool.Submit(2, FakeJob(2), 9, Priority::High, std::make_shared<Progress>());
	pool.Submit(3, FakeJob(3), kNoLane, Priority::Normal, std::make_shared<Progress>());
	CHECK(FirstStarts(runner.WaitForStarts(2), 2) == (std::set<JobId>{1, 3}));
	runner.Release(1);
	std::vector<JobId> starts = runner.WaitForStarts(3);
	CHECK(starts.size() == 3 && starts[2] == 2);
	runner.Release(2);
	runner.Release(3);
	CHECK(WaitForFinished(pool, 3).size() == 3);
}

TEST(PoolCancelsQueuedAndRunningJobs)
{
	FakeRunner runner;
	Pool pool(1, [&](JobId, const Job &job, const std::atomic<bool> &abort, Progress &progress) {
		return runner.Run(job, abort, progress);
	});
	pool.Submit(1, FakeJob(1), kNoLane, Priority::Normal, std::make_shared<Progress>());
	pool.Submit(2, FakeJob(2), kNoLane, Priority::Normal, std::make_shared<Progress>());
	runner.WaitForStarts(1);
	CHECK(pool.Cancel(2));
	CHECK(pool.Cancel(1));
	CHECK(!pool.Cancel(99));
	std::vector<FinishedJob> finished = WaitForFinished(pool, 2);
	CHECK(finished.size() == 2);
	for (const FinishedJob &job : finished)
		CHECK(job.outcome.status.code == ErrorCode::Cancelled);
	CHECK(runner.WaitForStarts(1).size() == 1);
}

TEST(PoolChangesItsThreadCount)
{
	FakeRunner runner;
	Pool pool(1, [&](JobId, const Job &job, const std::atomic<bool> &abort, Progress &progress) {
		return runner.Run(job, abort, progress);
	});
	for (JobId id = 1; id <= 3; id++)
		pool.Submit(id, FakeJob(id), kNoLane, Priority::Normal, std::make_shared<Progress>());
	CHECK(runner.WaitForStarts(1).size() == 1);
	pool.SetThreadCount(3);
	CHECK(runner.WaitForStarts(3).size() == 3);
	CHECK(pool.State().threads == 3);
	pool.SetThreadCount(100);
	CHECK(pool.State().threads == kMaxThreads);
	pool.SetThreadCount(1);
	for (JobId id = 1; id <= 3; id++)
		runner.Release(id);
	CHECK(WaitForFinished(pool, 3).size() == 3);
	CHECK(pool.State().threads == 1);
}

TEST(PoolStopsWithoutOutcomes)
{
	FakeRunner runner;
	Pool pool(1, [&](JobId, const Job &job, const std::atomic<bool> &abort, Progress &progress) {
		return runner.Run(job, abort, progress);
	});
	pool.Submit(1, FakeJob(1), kNoLane, Priority::Normal, std::make_shared<Progress>());
	pool.Submit(2, FakeJob(2), kNoLane, Priority::Normal, std::make_shared<Progress>());
	runner.WaitForStarts(1);
	pool.Stop();
	CHECK(pool.TakeFinished().empty());
	pool.Submit(3, FakeJob(3), kNoLane, Priority::Normal, std::make_shared<Progress>());
	CHECK(pool.State().jobs.empty());
}
