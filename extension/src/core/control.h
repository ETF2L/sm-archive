/* Copyright 2026 ETF2L Contributors, Dave
 *
 * What a running job is watched by: its abort flag, the progress counters
 * the main thread reads, and the limits of what it may unpack. */
#ifndef SMARCHIVE_CORE_CONTROL_H
#define SMARCHIVE_CORE_CONTROL_H

#include "status.h"

#include <atomic>
#include <cstdint>

namespace smarchive {

/* Written by the worker, read by the main thread at any time. */
struct Progress {
	std::atomic<uint64_t> bytesRead{0};
	std::atomic<uint64_t> bytesWritten{0};
	std::atomic<uint64_t> entriesDone{0};
	/* The input's size in bytes, -1 while unknown. */
	std::atomic<int64_t> inputSize{-1};
};

/* What unpacking may produce; 0 in any field means no limit. */
struct Limits {
	/* Bytes written in total; also the bytes a codec decodes from an
	 * archive (a compressed tar). */
	uint64_t maxSize;
	/* Entries of an archive, every kind counted. */
	uint64_t maxEntries;
	/* Bytes written per byte read, overall and per entry. */
	uint64_t maxRatio;
	/* Bytes of output kept in memory. */
	uint64_t maxMemory;
};

constexpr uint64_t kMiB = 1024ull * 1024ull;
constexpr Limits kDefaultLimits{1024 * kMiB, 10000, 1000, 64 * kMiB};
constexpr Limits kNoLimits{0, 0, 0, 0};

/* The ratio is only judged once this much was written, so small files of
 * zeros do not trip it. */
constexpr uint64_t kRatioThreshold = 16 * kMiB;

class JobControl {
public:
	/* enforceOutputLimits: whether written bytes count against maxSize and
	 * maxRatio (unpacking does, packing does not). */
	JobControl(const std::atomic<bool> &abort, Progress &progress, const Limits &limits, bool enforceOutputLimits)
		: m_abort(abort), m_progress(progress), m_limits(limits), m_enforceOutputLimits(enforceOutputLimits)
	{
	}

	const Limits &GetLimits() const { return m_limits; }
	Progress &GetProgress() { return m_progress; }

	/* Cancelled once the job's abort flag is set. */
	Status CheckAbort() const;

	/* Bytes taken from the input file. */
	void CountRead(uint64_t bytes);

	/* Bytes written to the output; fails at maxSize or maxRatio. */
	Status CountWritten(uint64_t bytes);

	/* Bytes a codec decoded from an archive for its container: what the job
	 * reads, the data of the entries it skips included. Fails at maxSize or
	 * maxRatio, judged as for the output. */
	Status CountDecoded(uint64_t bytes);

	/* One more archive entry seen; fails beyond maxEntries. */
	Status CountEntry();

	/* Starts the per-entry ratio of the next entry's data. */
	void BeginEntryData();

	/* The input bytes read since BeginEntryData(): what one entry took of the
	 * archive, as far as it can be told (solid archives decode ahead). */
	uint64_t EntryBytesRead() const;

	void FinishEntry();

private:
	Status CheckRatio(uint64_t written, uint64_t read, const char *what) const;

	const std::atomic<bool> &m_abort;
	Progress &m_progress;
	Limits m_limits;
	bool m_enforceOutputLimits;
	uint64_t m_read = 0;
	uint64_t m_written = 0;
	uint64_t m_decoded = 0;
	uint64_t m_entries = 0;
	uint64_t m_entryReadStart = 0;
	uint64_t m_entryWritten = 0;
	bool m_isInEntry = false;
};

} // namespace smarchive

#endif // SMARCHIVE_CORE_CONTROL_H
