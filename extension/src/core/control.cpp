/* Copyright 2026 ETF2L Contributors, Dave */
#include "control.h"

#include <string>

namespace smarchive {

Status JobControl::CheckAbort() const
{
	if (m_abort.load(std::memory_order_relaxed))
		return Status::Fail(ErrorCode::Cancelled, "the job was cancelled");
	return Status::Ok();
}

void JobControl::CountRead(uint64_t bytes)
{
	m_read += bytes;
	m_progress.bytesRead.store(m_read, std::memory_order_relaxed);
}

Status JobControl::CheckRatio(uint64_t written, uint64_t read, const char *what) const
{
	if (m_limits.maxRatio == 0 || written <= kRatioThreshold)
		return Status::Ok();
	uint64_t denominator = read == 0 ? 1 : read;
	if (written / denominator < m_limits.maxRatio)
		return Status::Ok();
	return Status::Fail(ErrorCode::LimitRatio, std::string(what) + " unpacks to more than " +
	                                           std::to_string(m_limits.maxRatio) + " times its packed size");
}

Status JobControl::CountWritten(uint64_t bytes)
{
	m_written += bytes;
	m_progress.bytesWritten.store(m_written, std::memory_order_relaxed);
	if (m_isInEntry)
		m_entryWritten += bytes;
	if (!m_enforceOutputLimits)
		return Status::Ok();

	if (m_limits.maxSize != 0 && m_written > m_limits.maxSize)
		return Status::Fail(ErrorCode::LimitSize,
		                    "the output exceeds the size limit of " + std::to_string(m_limits.maxSize) + " bytes");
	Status overall = CheckRatio(m_written, m_read, "the input");
	if (!overall.IsOk() || !m_isInEntry)
		return overall;
	return CheckRatio(m_entryWritten, EntryBytesRead(), "an entry");
}

Status JobControl::CountDecoded(uint64_t bytes)
{
	m_decoded += bytes;
	if (!m_enforceOutputLimits)
		return Status::Ok();
	if (m_limits.maxSize != 0 && m_decoded > m_limits.maxSize)
		return Status::Fail(ErrorCode::LimitSize, "the archive decodes to more than the size limit of " +
		                                          std::to_string(m_limits.maxSize) + " bytes");
	return CheckRatio(m_decoded, m_read, "the archive");
}

Status JobControl::CountEntry()
{
	m_entries++;
	m_progress.entriesDone.store(m_entries, std::memory_order_relaxed);
	if (m_limits.maxEntries != 0 && m_entries > m_limits.maxEntries)
		return Status::Fail(ErrorCode::LimitEntries,
		                    "the archive has more than " + std::to_string(m_limits.maxEntries) + " entries");
	return Status::Ok();
}

void JobControl::BeginEntryData()
{
	m_entryReadStart = m_read;
	m_entryWritten = 0;
	m_isInEntry = true;
}

uint64_t JobControl::EntryBytesRead() const
{
	return m_read - m_entryReadStart;
}

void JobControl::FinishEntry()
{
	m_isInEntry = false;
}

} // namespace smarchive
