/* Copyright 2026 ETF2L Contributors, Dave */
#include "pool.h"

#include <utility>

namespace smarchive {

size_t PickNext(const std::deque<QueuedJob> &queue, const std::set<LaneId> &busyLanes, Clock::time_point now,
                Clock::duration aging)
{
	size_t firstStartable = queue.size();
	std::set<LaneId> lanesAhead;
	for (size_t i = 0; i < queue.size(); i++) {
		const QueuedJob &job = queue[i];
		bool isStartable = job.lane == kNoLane || (busyLanes.count(job.lane) == 0 && lanesAhead.count(job.lane) == 0);
		if (job.lane != kNoLane)
			lanesAhead.insert(job.lane);
		if (!isStartable)
			continue;
		bool countsAsHigh = job.priority == Priority::High || now - job.queuedAt >= aging;
		if (countsAsHigh)
			return i;
		if (firstStartable == queue.size())
			firstStartable = i;
	}
	return firstStartable;
}

namespace {

int ClampThreads(int threads)
{
	return threads < kMinThreads ? kMinThreads : threads > kMaxThreads ? kMaxThreads : threads;
}

} // namespace

Pool::Pool(int threads, Runner runner, Clock::duration aging)
	: m_runner(std::move(runner)), m_aging(aging), m_targetThreads(ClampThreads(threads))
{
	std::lock_guard<std::mutex> lock(m_mutex);
	StartWorkers();
}

Pool::~Pool()
{
	Stop();
}

void Pool::StartWorkers()
{
	while (m_liveThreads < m_targetThreads) {
		auto hasEnded = std::make_shared<std::atomic<bool>>(false);
		m_workers.push_back(Worker{std::thread(&Pool::Work, this, hasEnded), hasEnded});
		m_liveThreads++;
	}
}

void Pool::JoinEndedWorkers()
{
	for (auto it = m_workers.begin(); it != m_workers.end();) {
		if (!it->hasEnded->load()) {
			++it;
			continue;
		}
		it->thread.join();
		it = m_workers.erase(it);
	}
}

void Pool::Submit(JobId id, Job job, LaneId lane, Priority priority, std::shared_ptr<Progress> progress)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_isStopping)
			return;
		QueuedJob schedule{id, lane, priority, Clock::now()};
		m_order.push_back(schedule);
		m_queued.emplace(id, Pending{schedule, std::move(job), std::move(progress)});
	}
	m_wakeUp.notify_all();
}

bool Pool::Cancel(JobId id)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto running = m_running.find(id);
	if (running != m_running.end()) {
		running->second.abort->store(true);
		return true;
	}
	auto queued = m_queued.find(id);
	if (queued == m_queued.end())
		return false;
	m_queued.erase(queued);
	for (auto it = m_order.begin(); it != m_order.end(); ++it) {
		if (it->id == id) {
			m_order.erase(it);
			break;
		}
	}
	JobOutcome outcome;
	outcome.status = Status::Fail(ErrorCode::Cancelled, "the job was cancelled before it started");
	m_finished.push_back(FinishedJob{id, std::move(outcome)});
	return true;
}

void Pool::SetThreadCount(int threads)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_isStopping)
			return;
		m_targetThreads = ClampThreads(threads);
		JoinEndedWorkers();
		StartWorkers();
	}
	m_wakeUp.notify_all();
}

std::vector<FinishedJob> Pool::TakeFinished()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return std::exchange(m_finished, {});
}

PoolState Pool::State()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	PoolState state;
	state.threads = m_targetThreads;
	for (const auto &[id, running] : m_running)
		state.jobs.push_back(PoolState::Entry{id, running.lane, running.priority, true, running.queuedAt});
	for (const QueuedJob &queued : m_order)
		state.jobs.push_back(PoolState::Entry{queued.id, queued.lane, queued.priority, false, queued.queuedAt});
	return state;
}

void Pool::Stop()
{
	std::vector<Worker> workers;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_isStopping = true;
		m_order.clear();
		m_queued.clear();
		m_finished.clear();
		for (auto &[id, running] : m_running)
			running.abort->store(true);
		workers = std::move(m_workers);
		m_workers.clear();
	}
	m_wakeUp.notify_all();
	for (Worker &worker : workers)
		worker.thread.join();
}

size_t Pool::PickNextLocked() const
{
	return PickNext(m_order, m_busyLanes, Clock::now(), m_aging);
}

void Pool::Work(std::shared_ptr<std::atomic<bool>> hasEnded)
{
	std::unique_lock<std::mutex> lock(m_mutex);
	for (;;) {
		m_wakeUp.wait(lock, [this] {
			return m_isStopping || m_liveThreads > m_targetThreads || PickNextLocked() < m_order.size();
		});
		if (m_isStopping || m_liveThreads > m_targetThreads) {
			m_liveThreads--;
			hasEnded->store(true);
			return;
		}

		size_t index = PickNextLocked();
		QueuedJob schedule = m_order[index];
		m_order.erase(m_order.begin() + static_cast<std::ptrdiff_t>(index));
		auto queued = m_queued.find(schedule.id);
		Pending pending = std::move(queued->second);
		m_queued.erase(queued);
		auto abort = std::make_shared<std::atomic<bool>>(false);
		m_running.emplace(schedule.id, Running{schedule.lane, schedule.priority, schedule.queuedAt, abort});
		if (schedule.lane != kNoLane)
			m_busyLanes.insert(schedule.lane);

		lock.unlock();
		JobOutcome outcome = m_runner(schedule.id, pending.job, *abort, *pending.progress);
		lock.lock();

		m_running.erase(schedule.id);
		m_busyLanes.erase(schedule.lane);
		if (!m_isStopping)
			m_finished.push_back(FinishedJob{schedule.id, std::move(outcome)});
		/* The lane may let its next job start now. */
		m_wakeUp.notify_all();
	}
}

} // namespace smarchive
