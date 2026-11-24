/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The SourceMod side of the extension: natives queue jobs on the core's job
 * desk, which owns their ids, owners, lanes and progress pacing, and every
 * game frame it hands the finished jobs and the progress to receivers that
 * call the plugins' callbacks. A plugin's jobs are dropped when it unloads.
 * The same desk serves the shims through ISMArchiveShims. */
#ifndef SMARCHIVE_EXTENSION_H
#define SMARCHIVE_EXTENSION_H

#include "smsdk_ext.h"

#include "api/smarchive_api.h"
#include "jobdesk.h"

#include <memory>
#include <string>
#include <vector>

/* What a result handle holds: the finished job's outcome, shared by its
 * clones. */
struct ResultData {
	smarchive::JobId id;
	smarchive::JobOutcome outcome;
};

struct ResultObject {
	std::shared_ptr<const ResultData> data;
};

/* A plugin's source list of an archive to create. */
struct SourcesObject {
	std::vector<smarchive::ArchiveSource> sources;
};

/* A plugin's prefixes of an extraction, clean archive paths. */
struct PrefixesObject {
	std::vector<std::string> prefixes;
};

/* How a plugin's job calls back. */
struct PluginCallbacks {
	funcid_t callback = 0;
	cell_t data = 0;
	bool hasProgressCallback = false;
	funcid_t progressCallback = 0;
};

/* A job as the natives describe it before it is queued. */
struct JobRequest {
	smarchive::Job job;
	bool isSerial = false;
	smarchive::Priority priority = smarchive::Priority::Normal;
	/* For sm_archive_status: what the job does. */
	std::string description;
	PluginCallbacks callbacks;
};

class SMArchive : public SDKExtension,
                  public IPluginsListener,
                  public IHandleTypeDispatch,
                  public smarchive_api::ISMArchiveShims {
public: // SDKExtension
	bool SDK_OnLoad(char *error, size_t maxlength, bool late) override;
	void SDK_OnUnload() override;

public: // IPluginsListener
	void OnPluginUnloaded(IPlugin *plugin) override;

public: // IHandleTypeDispatch
	void OnHandleDestroy(HandleType_t type, void *object) override;

public: // ISMArchiveShims
	uint64_t CreateLane() override;
	uint64_t QueueCodecFile(const smarchive_api::CodecFileRequest &request, uint64_t lane,
	                        smarchive_api::IJobListener *listener) override;
	void Cancel(uint64_t job) override;

public:
	/* Queues the job of a plugin; returns its id. */
	smarchive::JobId Submit(IPluginContext *context, JobRequest request);

	/* Cancels a job of the plugin; false when it has no such job. */
	bool CancelOwnJob(IPluginContext *context, smarchive::JobId id);

	/* Calls the callbacks of the jobs finished since the last frame, and the
	 * progress callbacks of the running ones. */
	void OnFrame();

	const smarchive::Limits &DefaultLimits() const { return m_defaultLimits; }
	void SetDefaultLimits(const smarchive::Limits &limits) { m_defaultLimits = limits; }
	void SetThreadCount(int threads);

	/* The lines sm_archive_status and sm_archive_versions print. */
	std::vector<std::string> StatusLines();
	std::vector<std::string> VersionLines() const;

	HandleType_t codecOptionsType = 0;
	HandleType_t writeOptionsType = 0;
	HandleType_t limitsType = 0;
	HandleType_t sourcesType = 0;
	HandleType_t resultType = 0;
	HandleType_t prefixesType = 0;

private:
	bool CreateHandleTypes(char *error, size_t maxlength);

	std::unique_ptr<smarchive::JobDesk> m_desk;
	smarchive::Limits m_defaultLimits = smarchive::kDefaultLimits;
};

extern SMArchive g_SMArchive;

/* The natives, one table per file. */
extern const sp_nativeinfo_t g_OptionNatives[];
extern const sp_nativeinfo_t g_JobNatives[];
extern const sp_nativeinfo_t g_ResultNatives[];
extern const sp_nativeinfo_t g_ServiceNatives[];

#endif // SMARCHIVE_EXTENSION_H
