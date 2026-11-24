/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The job desk with a fake runner that holds every job until it is
 * released, and receivers that note what they are told: ids, owners and
 * their lanes, cancellation, unloading, the progress pacing with a given
 * clock and the status lines. */
#include "check.h"
#include "jobdesk.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>

using namespace smarchive;
using namespace std::chrono_literals;

namespace {

/* Runs jobs only when released; a running job's progress can be set. */
class HoldingRunner {
public:
	JobOutcome Run(JobId id, const std::atomic<bool> &abort, Progress &progress)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_started.push_back(id);
		m_progress[id] = &progress;
		m_changed.notify_all();
		/* The abort flag notifies nobody, so it is polled. */
		while (m_released.count(id) == 0 && !abort.load())
			m_changed.wait_for(lock, 10ms);
		m_progress.erase(id);
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

	/* Sets the counters of a running job. */
	void SetProgress(JobId id, uint64_t read, uint64_t written, uint64_t entries)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		Progress *progress = m_progress.at(id);
		progress->bytesRead.store(read);
		progress->bytesWritten.store(written);
		progress->entriesDone.store(entries);
	}

	Pool::Runner AsRunner()
	{
		return [this](JobId id, const Job &, const std::atomic<bool> &abort, Progress &progress) {
			return Run(id, abort, progress);
		};
	}

private:
	std::mutex m_mutex;
	std::condition_variable m_changed;
	std::vector<JobId> m_started;
	std::set<JobId> m_released;
	std::map<JobId, Progress *> m_progress;
};

/* What the receivers of a test were told, in order. */
struct Told {
	struct Finished {
		JobId id;
		ErrorCode code;
	};
	std::vector<Finished> finished;
	std::vector<std::pair<JobId, ProgressReport>> progress;
};

class NotingReceiver : public JobReceiver {
public:
	explicit NotingReceiver(Told &told) : m_told(told) {}

	void OnFinished(JobId id, JobOutcome outcome) override
	{
		m_told.finished.push_back(Told::Finished{id, outcome.status.code});
	}

	void OnProgress(JobId id, const ProgressReport &report) override { m_told.progress.emplace_back(id, report); }

private:
	Told &m_told;
};

/* Notes like NotingReceiver, then runs a hook that calls the desk back. */
class HookedReceiver : public NotingReceiver {
public:
	using Hook = std::function<void(JobId)>;

	HookedReceiver(Told &told, Hook onFinished, Hook onProgress)
		: NotingReceiver(told), m_onFinished(std::move(onFinished)), m_onProgress(std::move(onProgress))
	{
	}

	void OnFinished(JobId id, JobOutcome outcome) override
	{
		NotingReceiver::OnFinished(id, std::move(outcome));
		m_onFinished(id);
	}

	void OnProgress(JobId id, const ProgressReport &report) override
	{
		NotingReceiver::OnProgress(id, report);
		m_onProgress(id);
	}

private:
	Hook m_onFinished;
	Hook m_onProgress;
};

/* Two owners, told apart by their addresses as plugins are. */
int g_pluginA, g_pluginB;
const OwnerKey kOwnerA = &g_pluginA, kOwnerB = &g_pluginB;

JobId SubmitFor(JobDesk &desk, OwnerKey owner, Told &told, LaneId lane = kNoLane, std::string description = "job")
{
	JobSubmission submission{owner, "owner.smx", std::move(description), Job{ListJob{"x", ArchiveChoice{}}}, lane,
	                         Priority::Normal};
	return desk.Submit(std::move(submission), std::make_unique<NotingReceiver>(told));
}

/* Polls until count jobs were delivered in all, or 5 seconds passed. */
void PollUntilFinished(JobDesk &desk, const Told &told, size_t count)
{
	for (int i = 0; i < 500 && told.finished.size() < count; i++) {
		desk.Poll(Clock::now());
		std::this_thread::sleep_for(10ms);
	}
}

} // namespace

TEST(DeskNumbersJobsAndDeliversEachOnce)
{
	HoldingRunner runner;
	JobDesk desk(2, runner.AsRunner());
	Told told;
	JobId first = SubmitFor(desk, kOwnerA, told), second = SubmitFor(desk, kOwnerB, told);
	CHECK(first == 1 && second == 2);
	/* The runner is given the id the desk returned. */
	std::vector<JobId> started = runner.WaitForStarts(2);
	CHECK(std::set<JobId>(started.begin(), started.end()) == (std::set<JobId>{1, 2}));
	runner.Release(2);
	PollUntilFinished(desk, told, 1);
	runner.Release(1);
	PollUntilFinished(desk, told, 2);
	CHECK(told.finished.size() == 2);
	CHECK(told.finished.size() == 2 && told.finished[0].id == 2 && told.finished[1].id == 1);
	desk.Poll(Clock::now());
	CHECK(told.finished.size() == 2);
}

TEST(DeskKeepsALanePerOwner)
{
	HoldingRunner runner;
	JobDesk desk(1, runner.AsRunner());
	LaneId laneA = desk.OwnerLane(kOwnerA);
	CHECK(laneA != kNoLane);
	CHECK(desk.OwnerLane(kOwnerA) == laneA);
	CHECK(desk.OwnerLane(kOwnerB) != laneA);
	LaneId shimLane = desk.NewLane();
	CHECK(shimLane != laneA && shimLane != desk.OwnerLane(kOwnerB));
	desk.OwnerUnloaded(kOwnerA);
	CHECK(desk.OwnerLane(kOwnerA) != laneA);
}

TEST(DeskCancelsOnlyAnOwnersOwnJobs)
{
	HoldingRunner runner;
	JobDesk desk(1, runner.AsRunner());
	Told told;
	JobId running = SubmitFor(desk, kOwnerA, told), queued = SubmitFor(desk, kOwnerA, told);
	runner.WaitForStarts(1);
	CHECK(!desk.Cancel(kOwnerB, running));
	CHECK(!desk.Cancel(kOwnerA, 99));
	CHECK(desk.Cancel(kOwnerA, queued));
	CHECK(desk.Cancel(kOwnerA, running));
	/* Cancelled jobs are still delivered, as Cancelled. */
	PollUntilFinished(desk, told, 2);
	CHECK(told.finished.size() == 2);
	for (const Told::Finished &finished : told.finished)
		CHECK(finished.code == ErrorCode::Cancelled);
}

TEST(DeskDropsTheJobsOfAnUnloadedOwner)
{
	HoldingRunner runner;
	JobDesk desk(2, runner.AsRunner());
	Told toldA, toldB;
	JobId runningA = SubmitFor(desk, kOwnerA, toldA);
	JobId runningB = SubmitFor(desk, kOwnerB, toldB);
	runner.WaitForStarts(2);
	JobId queuedA = SubmitFor(desk, kOwnerA, toldA);
	desk.OwnerUnloaded(kOwnerA);
	CHECK(!desk.Cancel(kOwnerA, runningA) && !desk.Cancel(kOwnerA, queuedA));
	/* A dropped job of a shim is told nothing either. */
	Told toldShim;
	JobId shimJob = SubmitFor(desk, kOwnerB, toldShim, desk.NewLane());
	desk.Drop(shimJob);
	runner.Release(runningB);
	PollUntilFinished(desk, toldB, 1);
	/* The unloaded owner's running job is stopped, it is never released. */
	const std::string idle = "2 worker thread(s), 0 job(s) running, 0 queued";
	for (int i = 0; i < 500 && desk.StatusLines(Clock::now())[0] != idle; i++)
		std::this_thread::sleep_for(10ms);
	CHECK(desk.StatusLines(Clock::now())[0] == idle);
	desk.Poll(Clock::now());
	CHECK(toldA.finished.empty() && toldShim.finished.empty());
	CHECK(toldB.finished.size() == 1 && toldB.finished[0].id == runningB);
}

TEST(DeskPacesProgress)
{
	HoldingRunner runner;
	JobDesk desk(1, runner.AsRunner());
	Told told;
	Clock::time_point submitted = Clock::now();
	JobId id = SubmitFor(desk, kOwnerA, told);
	runner.WaitForStarts(1);
	runner.SetProgress(id, 10, 5, 1);

	desk.Poll(submitted + kProgressInterval - 50ms);
	CHECK(told.progress.empty());
	Clock::time_point reported = submitted + 10s;
	desk.Poll(reported);
	CHECK(told.progress.size() == 1);
	CHECK(!told.progress.empty() && told.progress[0].first == id && told.progress[0].second.bytesRead == 10 &&
	      told.progress[0].second.bytesWritten == 5 && told.progress[0].second.entriesDone == 1);

	/* A change is told only once the interval passed since the last report. */
	runner.SetProgress(id, 20, 5, 1);
	desk.Poll(reported + kProgressInterval - 1ms);
	CHECK(told.progress.size() == 1);
	desk.Poll(reported + kProgressInterval);
	CHECK(told.progress.size() == 2);
	/* Nothing is told while nothing changed. */
	desk.Poll(reported + 1h);
	CHECK(told.progress.size() == 2);
	runner.Release(id);
	PollUntilFinished(desk, told, 1);
}

TEST(DeskStatusNamesOwnersAndJobs)
{
	HoldingRunner runner;
	JobDesk desk(1, runner.AsRunner());
	Told told;
	JobId running = SubmitFor(desk, kOwnerA, told, kNoLane, "list a.zip");
	runner.WaitForStarts(1);
	JobId queued = SubmitFor(desk, kOwnerA, told, desk.OwnerLane(kOwnerA), "list b.zip");
	runner.SetProgress(running, 3, 2, 1);
	std::vector<std::string> lines = desk.StatusLines(Clock::now() + 5s);
	CHECK(lines.size() == 3);
	CHECK(lines.size() == 3 && lines[0] == "1 worker thread(s), 1 job(s) running, 1 queued");
	CHECK(lines.size() == 3 &&
	      lines[1] == "#1 running normal 5s owner.smx: list a.zip (3 bytes read, 2 written, 1 entries)");
	CHECK(lines.size() == 3 && lines[2] == "#2 queued  normal serial 5s owner.smx: list b.zip");
	runner.Release(running);
	runner.Release(queued);
	PollUntilFinished(desk, told, 2);
}

TEST(DeskSurvivesReceiversThatCallItBack)
{
	HoldingRunner runner;
	JobDesk desk(2, runner.AsRunner());
	Told told;
	JobId first = 0, second = 0, queued = 0;
	bool hasCancelledItself = true;
	auto submission = [] {
		return JobSubmission{kOwnerA, "owner.smx", "job", Job{ListJob{"x", ArchiveChoice{}}}, kNoLane,
		                     Priority::Normal};
	};
	/* Finished, the first job's receiver queues one more and finds itself
	 * forgotten; told its progress, it drops the second job. */
	first = desk.Submit(submission(), std::make_unique<HookedReceiver>(
		told,
		[&](JobId id) {
			queued = desk.Submit(submission(), std::make_unique<NotingReceiver>(told));
			hasCancelledItself = desk.Cancel(kOwnerA, id);
		},
		[&](JobId) { desk.Drop(second); }));
	second = desk.Submit(submission(), std::make_unique<NotingReceiver>(told));
	runner.WaitForStarts(2);
	runner.SetProgress(first, 1, 0, 0);
	runner.SetProgress(second, 1, 0, 0);
	/* Both are due; the second is dropped before its turn and told nothing. */
	desk.Poll(Clock::now() + 10s);
	CHECK(told.progress.size() == 1 && told.progress[0].first == first);

	runner.Release(first);
	PollUntilFinished(desk, told, 1);
	CHECK(!hasCancelledItself && queued == 3);
	runner.Release(queued);
	PollUntilFinished(desk, told, 2);
	CHECK(told.finished.size() == 2 && told.finished[0].id == first && told.finished[1].id == queued);
}
