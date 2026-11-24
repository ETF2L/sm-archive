/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The job desk: the bookkeeping of queued and running jobs on top of the
 * pool. It gives every job its id, keeps the job's owner (a plugin or a
 * shim, by a key it only compares) and one serial lane per owner, drops an
 * owner's jobs when it unloads, and on every poll hands the finished jobs
 * to their receivers and tells the running ones' receivers their progress,
 * at most every kProgressInterval and only when it changed. Everything but
 * the pool's workers runs on the thread that calls it. */
#ifndef SMARCHIVE_CORE_JOBDESK_H
#define SMARCHIVE_CORE_JOBDESK_H

#include "pool.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace smarchive {

/* A job's receiver is told its progress at most this often. */
constexpr std::chrono::milliseconds kProgressInterval{250};

/* The owner of a job: the plugin or shim that queued it. */
using OwnerKey = const void *;

/* A running job's counters as its receiver is told them. */
struct ProgressReport {
	uint64_t bytesRead = 0;
	uint64_t bytesWritten = 0;
	uint64_t entriesDone = 0;
	/* The input's size in bytes, -1 while unknown. */
	int64_t inputSize = -1;
};

/* How a job reaches back to its owner. */
class JobReceiver {
public:
	virtual ~JobReceiver() = default;
	/* Once, when the job ended, cancelled too; never for a dropped job. */
	virtual void OnFinished(JobId id, JobOutcome outcome) = 0;
	/* While the job is queued or runs, when its counters changed. */
	virtual void OnProgress(JobId id, const ProgressReport &report) = 0;
};

/* A job as its owner queues it. */
struct JobSubmission {
	OwnerKey owner = nullptr;
	/* For sm_archive_status: the owner's name and what the job does. */
	std::string ownerName;
	std::string description;
	Job job;
	/* kNoLane, the owner's OwnerLane() or a NewLane(). */
	LaneId lane = kNoLane;
	Priority priority = Priority::Normal;
};

class JobDesk {
public:
	explicit JobDesk(int threads = kDefaultThreads, Pool::Runner runner = RunJob,
	                 Clock::duration aging = kDefaultAging);

	/* Queues the job; returns the id RunJob() runs it as. */
	JobId Submit(JobSubmission submission, std::unique_ptr<JobReceiver> receiver);

	/* The serial lane of owner, the same for all its jobs until it unloads. */
	LaneId OwnerLane(OwnerKey owner);

	/* A serial lane of no owner, for a shim's jobs. */
	LaneId NewLane();

	/* Cancels a job of owner as Pool::Cancel() does; its receiver is still
	 * told how it ended. False when owner has no such job or it ended. */
	bool Cancel(OwnerKey owner, JobId id);

	/* Forgets the job, whoever owns it, and cancels it; its receiver is
	 * told nothing. */
	void Drop(JobId id);

	/* Drops every job of owner and forgets its lane. */
	void OwnerUnloaded(OwnerKey owner);

	void SetThreadCount(int threads);

	/* Hands the jobs finished since the last poll to their receivers, then
	 * tells the others' receivers their progress as of now. */
	void Poll(Clock::time_point now);

	/* The lines sm_archive_status prints: the pool, then one per job. */
	std::vector<std::string> StatusLines(Clock::time_point now);

private:
	/* What the desk keeps of a job until it is delivered or dropped. */
	struct Ticket {
		OwnerKey owner;
		std::string ownerName;
		std::string description;
		std::unique_ptr<JobReceiver> receiver;
		std::shared_ptr<Progress> progress;
		ProgressReport reported;
		Clock::time_point reportedAt;
	};

	void Deliver(FinishedJob finished);
	void ReportProgress(Clock::time_point now);
	/* The report due for ticket as of now, noted as told. */
	static std::optional<ProgressReport> TakeDueReport(Ticket &ticket, Clock::time_point now);

	std::map<JobId, Ticket> m_tickets;
	std::map<OwnerKey, LaneId> m_ownerLanes;
	JobId m_nextJobId = 1;
	LaneId m_nextLane = 1;
	/* Declared last, so it is stopped before the receivers go. */
	Pool m_pool;
};

} // namespace smarchive

#endif // SMARCHIVE_CORE_JOBDESK_H
