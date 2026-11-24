/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The natives of the companion plugin smarchive.smx, which owns the cvars
 * and admin commands the extension cannot create without the engine: it
 * hands over the cvars' values and prints the reports. Not meant for other
 * plugins. */
#include "natives_common.h"

#include <string>
#include <vector>

using namespace smarchive;

namespace {

/* The lines of the last report taken, read one by one. */
std::vector<std::string> g_report;

/* The plugin API's SMArchiveReport. */
constexpr cell_t kReportStatus = 0;
constexpr cell_t kReportVersions = 1;

// SMArchive_SetThreads(int threads)
cell_t SetThreads(IPluginContext *context, const cell_t *params)
{
	if (params[1] < kMinThreads || params[1] > kMaxThreads)
		return context->ThrowNativeError("Threads %d is outside %d-%d", params[1], kMinThreads, kMaxThreads);
	g_SMArchive.SetThreadCount(params[1]);
	return 0;
}

// SMArchive_SetDefaultLimits(int maxSizeMiB, int maxEntries, int maxRatio, int maxMemoryMiB)
cell_t SetDefaultLimits(IPluginContext *context, const cell_t *params)
{
	for (int i = 1; i <= 4; i++)
		if (params[i] < 0)
			return context->ThrowNativeError("A limit cannot be negative (%d); 0 is no limit", params[i]);
	Limits limits{static_cast<uint64_t>(params[1]) * kMiB, static_cast<uint64_t>(params[2]),
	              static_cast<uint64_t>(params[3]), static_cast<uint64_t>(params[4]) * kMiB};
	g_SMArchive.SetDefaultLimits(limits);
	return 0;
}

// int SMArchive_TakeReport(SMArchiveReport report): its number of lines.
cell_t TakeReport(IPluginContext *context, const cell_t *params)
{
	if (params[1] == kReportStatus)
		g_report = g_SMArchive.StatusLines();
	else if (params[1] == kReportVersions)
		g_report = g_SMArchive.VersionLines();
	else
		return context->ThrowNativeError("Invalid SMArchiveReport %d", params[1]);
	return static_cast<cell_t>(g_report.size());
}

// SMArchive_GetReportLine(int line, char[] buffer, int maxlength)
cell_t GetReportLine(IPluginContext *context, const cell_t *params)
{
	if (params[1] < 0 || static_cast<size_t>(params[1]) >= g_report.size())
		return context->ThrowNativeError("Report line %d is outside 0-%d", params[1],
		                                 static_cast<int>(g_report.size()) - 1);
	return natives::WriteString(context, params, params[2], params[3], g_report[static_cast<size_t>(params[1])]);
}

} // namespace

const sp_nativeinfo_t g_ServiceNatives[] = {
	{"SMArchive_SetThreads",       SetThreads},
	{"SMArchive_SetDefaultLimits", SetDefaultLimits},
	{"SMArchive_TakeReport",       TakeReport},
	{"SMArchive_GetReportLine",    GetReportLine},
	{nullptr,                      nullptr},
};
