/* Copyright 2026 ETF2L Contributors, Dave
 *
 * What both shims share: an SMbz2 or SMbrotli whose jobs run on smarchive.
 * The natives keep their old names, arguments, checks and callbacks; the
 * jobs run in one serial lane per shim, in the order queued by all plugins
 * together, with temp files "<outFile>.tmp" and the default limits. Unlike
 * the old extensions, a plugin's jobs are cancelled when it unloads. */
#ifndef SMARCHIVE_SHIM_H
#define SMARCHIVE_SHIM_H

#include "smsdk_ext.h"

#include "smarchive_api.h"

#include <map>
#include <string>

/* A queued job's way back to its plugin. */
struct ShimCallback {
	IPluginContext *context;
	funcid_t function;
	cell_t data;
	/* The paths as the plugin passed them. */
	std::string inputPath;
	std::string outputPath;
};

class Shim : public SDKExtension, public IPluginsListener, public smarchive_api::IJobListener {
public: // SDKExtension
	bool SDK_OnLoad(char *error, size_t maxlength, bool late) override;
	void SDK_OnUnload() override;

public: // IPluginsListener
	void OnPluginUnloaded(IPlugin *plugin) override;

public: // IJobListener
	void OnJobFinished(uint64_t job, const smarchive_api::Outcome &outcome) override;

public:
	/* Checks the paths (params[1] and params[2]) and the callback
	 * (params[3], data params[4]) and queues request with them. */
	cell_t Queue(IPluginContext *context, const cell_t *params, smarchive_api::CodecFileRequest request);

protected:
	/* The old extension's natives and library name. */
	virtual const sp_nativeinfo_t *Natives() const = 0;
	virtual const char *LibraryName() const = 0;
	/* The old error enum's value for how a job ended. */
	virtual cell_t OldError(const smarchive_api::Outcome &outcome) const = 0;

private:
	smarchive_api::ISMArchiveShims *m_archive = nullptr;
	uint64_t m_lane = 0;
	std::map<uint64_t, ShimCallback> m_pending;
};

#endif // SMARCHIVE_SHIM_H
