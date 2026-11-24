/* Copyright 2026 ETF2L Contributors, Dave */
#include "jobdesk.h"

#include <utility>

namespace smarchive {

namespace {

const char *PriorityName(Priority priority)
{
	return priority == Priority::High ? "high" : "normal";
}

bool HasSameCounts(const ProgressReport &a, const ProgressReport &b)
{
	return a.bytesRead == b.bytesRead && a.bytesWritten == b.bytesWritten && a.entriesDone == b.entriesDone;
}

ProgressReport ReportOf(const Progress &progress)
{
	return ProgressReport{progress.bytesRead.load(), progress.bytesWritten.load(), progress.entriesDone.load(),
	                      progress.inputSize.load()};
}

} // namespace

JobDesk::JobDesk(int threads, Pool::Runner runner, Clock::duration aging)
	: m_pool(threads, std::move(runner), aging)
{
}

JobId JobDesk::Submit(JobSubmission submission, std::unique_ptr<JobReceiver> receiver)
{
	JobId id = m_nextJobId++;
	auto progress = std::make_shared<Progress>();
	m_tickets.emplace(id, Ticket{submission.owner, std::move(submission.ownerName),
	                             std::move(submission.description), std::move(receiver), progress,
	                             ProgressReport{}, Clock::now()});
	m_pool.Submit(id, std::move(submission.job), submission.lane, submission.priority, std::move(progress));
	return id;
}

LaneId JobDesk::OwnerLane(OwnerKey owner)
{
	auto found = m_ownerLanes.find(owner);
	if (found != m_ownerLanes.end())
		return found->second;
	LaneId lane = NewLane();
	m_ownerLanes.emplace(owner, lane);
	return lane;
}

LaneId JobDesk::NewLane()
{
	return m_nextLane++;
}

bool JobDesk::Cancel(OwnerKey owner, JobId id)
{
	auto found = m_tickets.find(id);
	if (found == m_tickets.end() || found->second.owner != owner)
		return false;
	return m_pool.Cancel(id);
}

void JobDesk::Drop(JobId id)
{
	if (m_tickets.erase(id) != 0)
		m_pool.Cancel(id);
}

void JobDesk::OwnerUnloaded(OwnerKey owner)
{
	for (auto it = m_tickets.begin(); it != m_tickets.end();) {
		if (it->second.owner != owner) {
			++it;
			continue;
		}
		m_pool.Cancel(it->first);
		it = m_tickets.erase(it);
	}
	m_ownerLanes.erase(owner);
}

void JobDesk::SetThreadCount(int threads)
{
	m_pool.SetThreadCount(threads);
}

void JobDesk::Poll(Clock::time_point now)
{
	for (FinishedJob &finished : m_pool.TakeFinished())
		Deliver(std::move(finished));
	ReportProgress(now);
}

void JobDesk::Deliver(FinishedJob finished)
{
	auto found = m_tickets.find(finished.id);
	if (found == m_tickets.end())
		return;
	/* Forgotten before the receiver runs, which may queue or cancel jobs. */
	std::unique_ptr<JobReceiver> receiver = std::move(found->second.receiver);
	m_tickets.erase(found);
	receiver->OnFinished(finished.id, std::move(finished.outcome));
}

void JobDesk::ReportProgress(Clock::time_point now)
{
	std::vector<std::pair<JobId, ProgressReport>> due;
	for (auto &[id, ticket] : m_tickets) {
		if (std::optional<ProgressReport> report = TakeDueReport(ticket, now))
			due.emplace_back(id, *report);
	}
	/* A receiver may drop jobs, so each is looked up again. */
	for (const auto &[id, report] : due) {
		auto found = m_tickets.find(id);
		if (found != m_tickets.end())
			found->second.receiver->OnProgress(id, report);
	}
}

std::optional<ProgressReport> JobDesk::TakeDueReport(Ticket &ticket, Clock::time_point now)
{
	if (now - ticket.reportedAt < kProgressInterval)
		return std::nullopt;
	ProgressReport report = ReportOf(*ticket.progress);
	if (HasSameCounts(report, ticket.reported))
		return std::nullopt;
	ticket.reported = report;
	ticket.reportedAt = now;
	return report;
}

std::vector<std::string> JobDesk::StatusLines(Clock::time_point now)
{
	PoolState state = m_pool.State();
	std::vector<std::string> lines;
	size_t running = 0;
	for (const PoolState::Entry &entry : state.jobs)
		running += entry.isRunning ? 1 : 0;
	lines.push_back(std::to_string(state.threads) + " worker thread(s), " + std::to_string(running) + " job(s) running, " +
	                std::to_string(state.jobs.size() - running) + " queued");
	for (const PoolState::Entry &entry : state.jobs) {
		auto found = m_tickets.find(entry.id);
		if (found == m_tickets.end())
			continue;
		const Ticket &ticket = found->second;
		long waited = static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(now - entry.queuedAt).count());
		std::string line = "#" + std::to_string(entry.id) + " " + (entry.isRunning ? "running" : "queued ") + " " +
		                   PriorityName(entry.priority) + (entry.lane != kNoLane ? " serial" : "") + " " +
		                   std::to_string(waited) + "s " + ticket.ownerName + ": " + ticket.description;
		if (entry.isRunning) {
			ProgressReport progress = ReportOf(*ticket.progress);
			line += " (" + std::to_string(progress.bytesRead) + " bytes read, " +
			        std::to_string(progress.bytesWritten) + " written, " + std::to_string(progress.entriesDone) +
			        " entries)";
		}
		lines.push_back(line);
	}
	return lines;
}

} // namespace smarchive
