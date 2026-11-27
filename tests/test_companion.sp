/**
 * Copyright 2026 ETF2L Contributors, Dave
 *
 * The tests of the companion plugin (scripting/smarchive.sp), run with
 * smrunner (sm-runner) against the built extension:
 *
 *     smrunner test test_companion.smx --extensions <dist>/extensions \
 *         --plugin smarchive.smx
 *
 * Its cvars, their config file, how their values reach the extension, and
 * the admin commands sm_archive_status and sm_archive_versions.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smarchive>
#include <smrunner>

/* ------------------------------------------------------------------ helpers */

ConVar FindCvar(const char[] name) {
	ConVar cvar = FindConVar(name);
	Assert.True(cvar != null, "%s exists", name);
	return cvar;
}

/* ------------------------------------------------------------------- cvars */

public void Test_CvarsStartWithTheirDefaults() {
	Assert.Equal(FindCvar("sm_archive_threads").IntValue, 2);
	Assert.Equal(FindCvar("sm_archive_max_size").IntValue, 1024);
	Assert.Equal(FindCvar("sm_archive_max_entries").IntValue, 10000);
	Assert.Equal(FindCvar("sm_archive_max_ratio").IntValue, 1000);
	Assert.Equal(FindCvar("sm_archive_max_memory").IntValue, 64);
}

public void Test_TheCvarsConfigFileIsWritten() {
	Assert.True(FileExists("cfg/sourcemod/smarchive.cfg"), "AutoExecConfig wrote cfg/sourcemod/smarchive.cfg");
}

public void Test_ThreadsCvarIsClampedToOneToEight() {
	ConVar threads = FindCvar("sm_archive_threads");
	threads.IntValue = 20;
	Assert.Equal(threads.IntValue, 8);
	threads.IntValue = 0;
	Assert.Equal(threads.IntValue, 1);
}

public void Test_LimitCvarsBecomeTheDefaultsOfNewLimits() {
	FindCvar("sm_archive_max_size").IntValue = 2;
	FindCvar("sm_archive_max_entries").IntValue = 7;
	FindCvar("sm_archive_max_ratio").IntValue = 0;
	FindCvar("sm_archive_max_memory").IntValue = 3;

	ArchiveLimits limits = new ArchiveLimits();
	Assert.Equal(limits.MaxSize, 2 * 1048576, "2 MiB");
	Assert.Equal(limits.MaxEntries, 7);
	Assert.Equal(limits.MaxRatio, 0);
	Assert.Equal(limits.MaxMemory, 3 * 1048576, "3 MiB");
	delete limits;
}

public void OnTestBoot(const char[] test) {
	if (StrEqual(test, "Test_TheConfigFileSetsTheCvars")) {
		CreateDirectory("cfg/sourcemod", 511);
		File file = OpenFile("cfg/sourcemod/smarchive.cfg", "w");
		file.WriteLine("sm_archive_threads \"5\"");
		file.WriteLine("sm_archive_max_entries \"42\"");
		delete file;
	}
}

public void Test_TheConfigFileSetsTheCvars() {
	Assert.Equal(FindCvar("sm_archive_threads").IntValue, 5);

	char output[512];
	Runner.CommandOutput(output, sizeof(output), "sm_archive_status");
	Assert.StrContains(output, "5 worker thread(s)", "the extension took the config's threads");

	ArchiveLimits limits = new ArchiveLimits();
	Assert.Equal(limits.MaxEntries, 42, "the extension took the config's limit");
	delete limits;
}

/* ---------------------------------------------------------------- commands */

public void Test_StatusTellsTheThreadsAndJobs() {
	char output[512];
	Runner.CommandOutput(output, sizeof(output), "sm_archive_status");
	Assert.StrContains(output, "2 worker thread(s), 0 job(s) running, 0 queued");

	FindCvar("sm_archive_threads").IntValue = 3;
	Runner.CommandOutput(output, sizeof(output), "sm_archive_status");
	Assert.StrContains(output, "3 worker thread(s)", "a changed cvar reaches the extension");
}

int g_calls;

void OnDone(ArchiveError error, ArchiveResult result, any data) {
	g_calls++;
}

bool HasCalledBack(any count) {
	return g_calls >= count;
}

public void Test_StatusListsTheQueuedJobs() {
	FindCvar("sm_archive_threads").IntValue = 1;
	char text[4096];
	for (int i = 0; i < 80; i++) {
		StrCat(text, sizeof(text), "The quick brown fox jumps over the lazy dog. ");
	}
	XzOptions xz = new XzOptions();
	xz.Preset = 9;
	xz.Extreme = true;
	for (int i = 0; i < 4; i++) {
		Compression.CompressString(text, xz, OnDone, i, .serial = true);
	}
	delete xz;

	char output[2048];
	Runner.CommandOutput(output, sizeof(output), "sm_archive_status");
	Assert.StrContains(output, "1 worker thread(s)");
	Assert.StrContains(output, "queued  normal serial", "a queued serial job is listed");
	Assert.StrContains(output, "test_companion.smx: compress 3600 bytes in memory", "with its plugin and what it does");
	Assert.True(Runner.WaitUntil(HasCalledBack, 30.0, 4), "the jobs called back");
}

public void Test_VersionsTellTheExtensionAndItsLibraries() {
	char output[1024];
	Runner.CommandOutput(output, sizeof(output), "sm_archive_versions");
	Assert.StrContains(output, "SourceMod compression and archives ");
	Assert.StrContains(output, "gzip: ");
	Assert.StrContains(output, "bzip2: ");
	Assert.StrContains(output, "brotli: ");
	Assert.StrContains(output, "xz: ");
	Assert.StrContains(output, "zstd: ");
	Assert.StrContains(output, "lz4: ");
	Assert.StrContains(output, "tar, zip, 7z, rar: ");
}

public void Test_CommandsNeedTheConfigFlag() {
	int player = Runner.ConnectClient("player");
	Runner.ClientCommand(player, "sm_archive_status");
	Assert.ClientReceived(player, RunnerMessage_Console, "You do not have access to this command");

	int admin = Runner.ConnectClient("admin");
	SetUserFlagBits(admin, ADMFLAG_CONFIG);
	Runner.ClientCommand(admin, "sm_archive_versions");
	Assert.ClientReceived(admin, RunnerMessage_Console, "SourceMod compression and archives ");
	Runner.ClientCommand(admin, "sm_archive_status");
	Assert.ClientReceived(admin, RunnerMessage_Console, "worker thread(s)");
}
