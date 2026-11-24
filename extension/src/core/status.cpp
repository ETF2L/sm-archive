/* Copyright 2026 ETF2L Contributors, Dave */
#include "status.h"

namespace smarchive {

const char *ErrorCodeName(ErrorCode code)
{
	switch (code) {
	case ErrorCode::None:          return "ArchiveError_None";
	case ErrorCode::OpenInput:     return "ArchiveError_OpenInput";
	case ErrorCode::OpenOutput:    return "ArchiveError_OpenOutput";
	case ErrorCode::OutputExists:  return "ArchiveError_OutputExists";
	case ErrorCode::CreatePath:    return "ArchiveError_CreatePath";
	case ErrorCode::Read:          return "ArchiveError_Read";
	case ErrorCode::Write:         return "ArchiveError_Write";
	case ErrorCode::Rename:        return "ArchiveError_Rename";
	case ErrorCode::Memory:        return "ArchiveError_Memory";
	case ErrorCode::Corrupt:       return "ArchiveError_Corrupt";
	case ErrorCode::UnexpectedEOF: return "ArchiveError_UnexpectedEOF";
	case ErrorCode::TrailingData:  return "ArchiveError_TrailingData";
	case ErrorCode::UnknownFormat: return "ArchiveError_UnknownFormat";
	case ErrorCode::Unsupported:   return "ArchiveError_Unsupported";
	case ErrorCode::Encrypted:     return "ArchiveError_Encrypted";
	case ErrorCode::EntryName:     return "ArchiveError_EntryName";
	case ErrorCode::UnsafeEntry:   return "ArchiveError_UnsafeEntry";
	case ErrorCode::LimitSize:     return "ArchiveError_LimitSize";
	case ErrorCode::LimitEntries:  return "ArchiveError_LimitEntries";
	case ErrorCode::LimitRatio:    return "ArchiveError_LimitRatio";
	case ErrorCode::LimitMemory:   return "ArchiveError_LimitMemory";
	case ErrorCode::EntryNotFound: return "ArchiveError_EntryNotFound";
	case ErrorCode::Cancelled:     return "ArchiveError_Cancelled";
	case ErrorCode::Internal:      return "ArchiveError_Internal";
	}
	return nullptr;
}

} // namespace smarchive
