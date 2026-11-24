/* Copyright 2026 ETF2L Contributors, Dave */
#include "stream.h"

#include <sys/stat.h>

#include <cstring>

namespace smarchive {

Status FileSource::Read(uint8_t *buffer, size_t capacity, size_t &got)
{
	got = fread(buffer, 1, capacity, m_file.get());
	if (ferror(m_file.get()))
		return Status::Fail(ErrorCode::Read, "reading the input failed");
	m_control.CountRead(got);
	return Status::Ok();
}

Status FileSource::Seek(int64_t offset, int whence, int64_t &position)
{
	if (fseeko(m_file.get(), static_cast<off_t>(offset), whence) != 0)
		return Status::Fail(ErrorCode::Read, "seeking in the input failed");
	position = static_cast<int64_t>(ftello(m_file.get()));
	return Status::Ok();
}

int64_t FileSource::Size() const
{
	struct stat info;
	if (fstat(fileno(m_file.get()), &info) != 0)
		return -1;
	return static_cast<int64_t>(info.st_size);
}

Status FileSource::Peek(uint8_t *buffer, size_t capacity, size_t &got)
{
	rewind(m_file.get());
	got = fread(buffer, 1, capacity, m_file.get());
	bool isFailed = ferror(m_file.get()) != 0;
	rewind(m_file.get());
	if (isFailed)
		return Status::Fail(ErrorCode::Read, "reading the input failed");
	return Status::Ok();
}

Status FileSink::Write(const uint8_t *data, size_t length)
{
	if (length > 0 && fwrite(data, 1, length, m_file.get()) != length)
		return Status::Fail(ErrorCode::Write, "writing the output failed");
	return m_control.CountWritten(length);
}

Status FileSink::Finish()
{
	FILE *file = m_file.release();
	if (!file)
		return Status::Ok();
	if (fclose(file) != 0)
		return Status::Fail(ErrorCode::Write, "closing the output failed");
	return Status::Ok();
}

Status MemorySource::Read(uint8_t *buffer, size_t capacity, size_t &got)
{
	size_t left = m_bytes.size() - m_position;
	got = left < capacity ? left : capacity;
	if (got > 0)
		memcpy(buffer, m_bytes.data() + m_position, got);
	m_position += got;
	m_control.CountRead(got);
	return Status::Ok();
}

Status MemorySink::Write(const uint8_t *data, size_t length)
{
	uint64_t maxMemory = m_control.GetLimits().maxMemory;
	if (maxMemory != 0 && m_bytes.size() + length > maxMemory)
		return Status::Fail(ErrorCode::LimitMemory,
		                    "the output exceeds the memory limit of " + std::to_string(maxMemory) + " bytes");
	m_bytes.insert(m_bytes.end(), data, data + length);
	return m_control.CountWritten(length);
}

Status TransformSource::Refill()
{
	size_t got = 0;
	Status status = m_upstream.Read(m_input.data(), m_input.size(), got);
	if (!status.IsOk())
		return status;
	m_inputStart = 0;
	m_inputEnd = got;
	m_isUpstreamDone = got == 0;
	return Status::Ok();
}

Status TransformSource::RefuseTrailingData()
{
	if (m_inputStart < m_inputEnd)
		return Status::Fail(ErrorCode::TrailingData, "data follows the end of the compressed stream");
	if (m_isUpstreamDone)
		return Status::Ok();
	Status status = Refill();
	if (!status.IsOk())
		return status;
	if (!m_isUpstreamDone)
		return Status::Fail(ErrorCode::TrailingData, "data follows the end of the compressed stream");
	return Status::Ok();
}

Status TransformSource::Read(uint8_t *buffer, size_t capacity, size_t &got)
{
	got = 0;
	while (!m_isDone) {
		if (m_inputStart == m_inputEnd && !m_isUpstreamDone) {
			Status status = Refill();
			if (!status.IsOk())
				return status;
		}

		TransformStep step;
		Status status = m_transform->Process(m_input.data() + m_inputStart, m_inputEnd - m_inputStart, buffer,
		                                     capacity, m_isUpstreamDone, step);
		if (!status.IsOk())
			return status;
		m_inputStart += step.consumed;

		if (step.isDone) {
			m_isDone = true;
			Status trailing = RefuseTrailingData();
			if (!trailing.IsOk())
				return trailing;
		}
		if (step.produced > 0) {
			got = step.produced;
			return Status::Ok();
		}
		bool isStuck = step.consumed == 0 && (m_isUpstreamDone || m_inputStart < m_inputEnd);
		if (isStuck && !m_isDone) {
			if (m_isUpstreamDone)
				return Status::Fail(ErrorCode::UnexpectedEOF, "the input ends in the middle of a stream");
			return Status::Fail(ErrorCode::Internal, "the codec made no progress");
		}
	}
	return Status::Ok();
}

Status TransformSink::Run(const uint8_t *data, size_t length, bool isLastInput)
{
	for (;;) {
		TransformStep step;
		Status status = m_transform->Process(data, length, m_output.data(), m_output.size(), isLastInput, step);
		if (!status.IsOk())
			return status;
		data += step.consumed;
		length -= step.consumed;
		if (step.produced > 0) {
			status = m_downstream.Write(m_output.data(), step.produced);
			if (!status.IsOk())
				return status;
		}
		if (isLastInput ? step.isDone : length == 0 && step.produced < m_output.size())
			return Status::Ok();
		if (step.consumed == 0 && step.produced == 0)
			return Status::Fail(ErrorCode::Internal, "the codec made no progress");
	}
}

Status TransformSink::Write(const uint8_t *data, size_t length)
{
	if (length == 0)
		return Status::Ok();
	return Run(data, length, false);
}

Status TransformSink::Finish()
{
	Status status = Run(nullptr, 0, true);
	if (!status.IsOk())
		return status;
	return m_downstream.Finish();
}

Status Pump(Source &source, Sink &sink, JobControl &control)
{
	std::vector<uint8_t> buffer(kChunkSize);
	for (;;) {
		Status status = control.CheckAbort();
		if (!status.IsOk())
			return status;
		size_t got = 0;
		status = source.Read(buffer.data(), buffer.size(), got);
		if (!status.IsOk())
			return status;
		if (got == 0)
			return sink.Finish();
		status = sink.Write(buffer.data(), got);
		if (!status.IsOk())
			return status;
	}
}

} // namespace smarchive
