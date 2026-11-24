/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The internal interface the shims smbz2.ext and smbrotli.ext reach
 * smarchive through. It is versioned but not documented and not meant for
 * other extensions. Only plain types cross it: each extension links its own
 * C++ runtime. */
#ifndef SMARCHIVE_API_H
#define SMARCHIVE_API_H

#include <IShareSys.h>

#include <stdint.h>

#define SMINTERFACE_SMARCHIVE_SHIMS_NAME    "ISMArchiveShims"
#define SMINTERFACE_SMARCHIVE_SHIMS_VERSION 1

namespace smarchive_api {

/* The plugin API's CodecFormat values the shims use. */
constexpr int kCodecBzip2 = 3;
constexpr int kCodecBrotli = 4;

/* One file compressed or decompressed with bzip2 or brotli. Paths are
 * absolute and checked by the shim. */
struct CodecFileRequest {
	bool isCompress;
	int codec;
	/* bzip2: level. */
	int level;
	/* brotli: quality, window (0 automatic), mode. */
	int quality;
	int window;
	int mode;
	const char *inputPath;
	const char *outputPath;
	const char *tempPath;
	bool overwrite;
	bool createPath;
};

/* How a job ended: the plugin API's ArchiveError and the library's code. */
struct Outcome {
	int error;
	int libraryCode;
};

class IJobListener {
public:
	/* On the main thread, on a game frame after the job ended. */
	virtual void OnJobFinished(uint64_t job, const Outcome &outcome) = 0;
};

class ISMArchiveShims : public SourceMod::SMInterface {
public:
	const char *GetInterfaceName() override { return SMINTERFACE_SMARCHIVE_SHIMS_NAME; }
	unsigned int GetInterfaceVersion() override { return SMINTERFACE_SMARCHIVE_SHIMS_VERSION; }

	/* A serial lane of its own: its jobs run one after the other. */
	virtual uint64_t CreateLane() = 0;

	/* Queues the request on lane with the default limits; returns the job's
	 * id. listener must outlive the job or cancel it. */
	virtual uint64_t QueueCodecFile(const CodecFileRequest &request, uint64_t lane, IJobListener *listener) = 0;

	/* Drops or stops the job; its listener is not called. */
	virtual void Cancel(uint64_t job) = 0;
};

} // namespace smarchive_api

#endif // SMARCHIVE_API_H
