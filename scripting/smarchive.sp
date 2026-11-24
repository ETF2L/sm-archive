/**
 * Copyright 2026 ETF2L Contributors, Dave
 *
 * The companion plugin of the smarchive extension: the extension uses no
 * engine API, so it cannot create cvars or console commands. This plugin
 * creates them (cfg/sourcemod/smarchive.cfg), hands the cvars' values to
 * the extension whenever they change, and prints its reports for the
 * admin commands. Without it, the built-in defaults apply.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smarchive>

/* The extension's natives for this plugin only. */
enum SMArchiveReport {
	SMArchiveReport_Status = 0,
	SMArchiveReport_Versions
};
native void SMArchive_SetThreads(int threads);
native void SMArchive_SetDefaultLimits(int maxSizeMiB, int maxEntries, int maxRatio, int maxMemoryMiB);
native int SMArchive_TakeReport(SMArchiveReport report);
native int SMArchive_GetReportLine(int line, char[] buffer, int maxlength);

public Plugin myinfo = {
	name = "SMArchive",
	author = "ETF2L Contributors, Dave",
	description = "Cvars and admin commands of the smarchive extension",
	version = "1.0.0",
	url = "https://github.com/etf2l/sm-archive"
};

ConVar g_threads;
ConVar g_maxSize;
ConVar g_maxEntries;
ConVar g_maxRatio;
ConVar g_maxMemory;

public void OnPluginStart() {
	g_threads = CreateConVar("sm_archive_threads", "2", "Worker threads that run the jobs", _, true, 1.0, true, 8.0);
	g_maxSize = CreateConVar("sm_archive_max_size", "1024",
		"Default limit of the bytes one job unpacks, in MiB (0: no limit)", _, true, 0.0, true, 2097151.0);
	g_maxEntries = CreateConVar("sm_archive_max_entries", "10000",
		"Default limit of the entries of an archive (0: no limit)", _, true, 0.0);
	g_maxRatio = CreateConVar("sm_archive_max_ratio", "1000",
		"Default limit of the bytes unpacked per byte read, once more than 16 MiB were unpacked (0: no limit)", _, true, 0.0);
	g_maxMemory = CreateConVar("sm_archive_max_memory", "64",
		"Default limit of the output one job keeps in memory, in MiB (0: no limit)", _, true, 0.0, true, 2097151.0);
	AutoExecConfig(true, "smarchive");

	g_threads.AddChangeHook(OnThreadsChanged);
	g_maxSize.AddChangeHook(OnLimitChanged);
	g_maxEntries.AddChangeHook(OnLimitChanged);
	g_maxRatio.AddChangeHook(OnLimitChanged);
	g_maxMemory.AddChangeHook(OnLimitChanged);

	RegAdminCmd("sm_archive_status", Command_Status, ADMFLAG_CONFIG, "Shows the queued and running archive jobs");
	RegAdminCmd("sm_archive_versions", Command_Versions, ADMFLAG_CONFIG,
		"Shows the versions of SMArchive and its libraries");
}

public void OnConfigsExecuted() {
	ApplyThreads();
	ApplyLimits();
}

void OnThreadsChanged(ConVar cvar, const char[] oldValue, const char[] newValue) {
	ApplyThreads();
}

void OnLimitChanged(ConVar cvar, const char[] oldValue, const char[] newValue) {
	ApplyLimits();
}

void ApplyThreads() {
	SMArchive_SetThreads(g_threads.IntValue);
}

void ApplyLimits() {
	SMArchive_SetDefaultLimits(g_maxSize.IntValue, g_maxEntries.IntValue, g_maxRatio.IntValue, g_maxMemory.IntValue);
}

Action Command_Status(int client, int args) {
	PrintReport(client, SMArchiveReport_Status);
	return Plugin_Handled;
}

Action Command_Versions(int client, int args) {
	PrintReport(client, SMArchiveReport_Versions);
	return Plugin_Handled;
}

void PrintReport(int client, SMArchiveReport report) {
	int lines = SMArchive_TakeReport(report);
	for (int i = 0; i < lines; i++) {
		char line[512];
		SMArchive_GetReportLine(i, line, sizeof(line));
		ReplyToCommand(client, "%s", line);
	}
}
