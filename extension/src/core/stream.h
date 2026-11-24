/* Copyright 2026 ETF2L Contributors, Dave
 *
 * Byte streams: sources to read from and sinks to write to, over files and
 * memory, and the adapters that run a codec's Transform over either, so
 * codecs compose with files, memory and containers without knowing them. */
#ifndef SMARCHIVE_CORE_STREAM_H
#define SMARCHIVE_CORE_STREAM_H

#include "control.h"
#include "status.h"

#include <stdio.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace smarchive {

/* The chunk size of every copy loop and of the adapters' buffers. */
constexpr size_t kChunkSize = 64 * 1024;

class Source {
public:
	virtual ~Source() = default;
	/* Reads up to capacity bytes; got is 0 only at the end of the stream. */
	virtual Status Read(uint8_t *buffer, size_t capacity, size_t &got) = 0;
};

class Sink {
public:
	virtual ~Sink() = default;
	virtual Status Write(const uint8_t *data, size_t length) = 0;
	/* Ends the stream: flushes what is buffered. Nothing is written after. */
	virtual Status Finish() = 0;
};

/* One step of a Transform. */
struct TransformStep {
	size_t consumed = 0;
	size_t produced = 0;
	/* The stream is complete and all its output was produced. */
	bool isDone = false;
};

/* A codec's encoder or decoder: takes bytes and gives bytes, zlib style.
 * isLastInput says that no input follows the bytes given; an encoder then
 * finishes its stream, a decoder must reach a stream's end (or fail with
 * UnexpectedEOF). A decoder that meets bytes it cannot take as a further
 * stream fails with TrailingData. */
class Transform {
public:
	virtual ~Transform() = default;
	virtual Status Process(const uint8_t *input, size_t inputLength, uint8_t *output, size_t outputLength,
	                       bool isLastInput, TransformStep &step) = 0;
};

struct FileCloser {
	void operator()(FILE *file) const { fclose(file); }
};
using UniqueFile = std::unique_ptr<FILE, FileCloser>;

/* A file opened for reading; counts what it reads as the job's input. */
class FileSource : public Source {
public:
	FileSource(UniqueFile file, JobControl &control) : m_file(std::move(file)), m_control(control) {}

	Status Read(uint8_t *buffer, size_t capacity, size_t &got) override;

	/* fseeko(); the new position in position. */
	Status Seek(int64_t offset, int whence, int64_t &position);

	/* The file's size, -1 if it cannot be told. */
	int64_t Size() const;

	/* Reads the first bytes of the file and goes back to its start, without
	 * counting them as read. */
	Status Peek(uint8_t *buffer, size_t capacity, size_t &got);

private:
	UniqueFile m_file;
	JobControl &m_control;
};

/* A file opened for writing; counts what it writes as the job's output. */
class FileSink : public Sink {
public:
	FileSink(UniqueFile file, JobControl &control) : m_file(std::move(file)), m_control(control) {}

	Status Write(const uint8_t *data, size_t length) override;
	/* Closes the file; a failed close is a failed write. */
	Status Finish() override;

private:
	UniqueFile m_file;
	JobControl &m_control;
};

/* Bytes in memory; counts them as the job's input. */
class MemorySource : public Source {
public:
	MemorySource(const std::vector<uint8_t> &bytes, JobControl &control) : m_bytes(bytes), m_control(control) {}

	Status Read(uint8_t *buffer, size_t capacity, size_t &got) override;

private:
	const std::vector<uint8_t> &m_bytes;
	size_t m_position = 0;
	JobControl &m_control;
};

/* Collects the output in memory, up to the job's maxMemory. */
class MemorySink : public Sink {
public:
	MemorySink(std::vector<uint8_t> &bytes, JobControl &control) : m_bytes(bytes), m_control(control) {}

	Status Write(const uint8_t *data, size_t length) override;
	Status Finish() override { return Status::Ok(); }

private:
	std::vector<uint8_t> &m_bytes;
	JobControl &m_control;
};

/* Reads what a Transform makes of another source's bytes. */
class TransformSource : public Source {
public:
	TransformSource(Source &upstream, std::unique_ptr<Transform> transform)
		: m_upstream(upstream), m_transform(std::move(transform)), m_input(kChunkSize)
	{
	}

	Status Read(uint8_t *buffer, size_t capacity, size_t &got) override;

private:
	Status Refill();
	/* After the stream's end: anything left is trailing data. */
	Status RefuseTrailingData();

	Source &m_upstream;
	std::unique_ptr<Transform> m_transform;
	std::vector<uint8_t> m_input;
	size_t m_inputStart = 0;
	size_t m_inputEnd = 0;
	bool m_isUpstreamDone = false;
	bool m_isDone = false;
};

/* Writes what a Transform makes of the bytes given to another sink. */
class TransformSink : public Sink {
public:
	TransformSink(Sink &downstream, std::unique_ptr<Transform> transform)
		: m_downstream(downstream), m_transform(std::move(transform)), m_output(kChunkSize)
	{
	}

	Status Write(const uint8_t *data, size_t length) override;
	/* Finishes the transform's stream, then the downstream sink. */
	Status Finish() override;

private:
	Status Run(const uint8_t *data, size_t length, bool isLastInput);

	Sink &m_downstream;
	std::unique_ptr<Transform> m_transform;
	std::vector<uint8_t> m_output;
};

/* Copies the source into the sink until the source ends, then finishes the
 * sink; stops when the job is cancelled. */
Status Pump(Source &source, Sink &sink, JobControl &control);

} // namespace smarchive

#endif // SMARCHIVE_CORE_STREAM_H
