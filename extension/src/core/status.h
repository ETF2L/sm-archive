/* Copyright 2026 ETF2L Contributors, Dave
 *
 * How an operation ends: one error code for every format, plus the code and
 * message of the library that failed, for logs. The codes are the plugin
 * API's ArchiveError (scripting/include/smarchive.inc), with the same
 * values. */
#ifndef SMARCHIVE_CORE_STATUS_H
#define SMARCHIVE_CORE_STATUS_H

#include <string>

namespace smarchive {

enum class ErrorCode : int {
	None = 0,
	OpenInput,
	OpenOutput,
	OutputExists,
	CreatePath,
	Read,
	Write,
	Rename,
	Memory,
	Corrupt,
	UnexpectedEOF,
	TrailingData,
	UnknownFormat,
	Unsupported,
	Encrypted,
	EntryName,
	UnsafeEntry,
	LimitSize,
	LimitEntries,
	LimitRatio,
	LimitMemory,
	EntryNotFound,
	Cancelled,
	Internal,
};

/* The last value, for range checks of values that come from plugins. */
constexpr ErrorCode kLastErrorCode = ErrorCode::Internal;

struct Status {
	ErrorCode code = ErrorCode::None;
	/* The failing library's own code (0 when none applies), e.g. bzip2's
	 * BZ_DATA_ERROR_MAGIC or brotli's BROTLI_DECODER_ERROR_*. */
	int libraryCode = 0;
	/* What went wrong, in English, for logs; empty on success. */
	std::string message;

	bool IsOk() const { return code == ErrorCode::None; }

	static Status Ok() { return Status{}; }
	static Status Fail(ErrorCode code, std::string message, int libraryCode = 0)
	{
		return Status{code, libraryCode, std::move(message)};
	}
};

/* "ArchiveError_Corrupt" and so on, nullptr for a value outside the enum. */
const char *ErrorCodeName(ErrorCode code);

} // namespace smarchive

#endif // SMARCHIVE_CORE_STATUS_H
