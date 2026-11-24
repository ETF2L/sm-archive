/* Copyright 2026 ETF2L Contributors, Dave */
#include "extension.h"

#include <utility>

using namespace smarchive;

SMArchive g_SMArchive;
SMEXT_LINK(&g_SMArchive);

namespace {

void OnGameFrame(bool simulating)
{
	g_SMArchive.OnFrame();
}

/* A plugin's job: its callback gets an ArchiveResult, its progress
 * callback, if any, the counters. */
class PluginReceiver : public JobReceiver {
public:
	PluginReceiver(IPluginContext *context, const PluginCallbacks &callbacks)
		: m_context(context), m_callbacks(callbacks)
	{
	}

	void OnFinished(JobId id, JobOutcome outcome) override
	{
		IPluginFunction *function;
		if (!m_context->GetFunctionByIdOrNull(m_callbacks.callback, &function) || !function)
			return;
		auto data = std::make_shared<const ResultData>(ResultData{id, std::move(outcome)});
		ErrorCode code = data->outcome.status.code;
		HandleSecurity security(m_context->GetIdentity(), myself->GetIdentity());
		HandleError handleError;
		auto *object = new ResultObject{data};
		Handle_t result = handlesys->CreateHandleEx(g_SMArchive.resultType, object, &security, nullptr, &handleError);
		if (result == BAD_HANDLE) {
			delete object;
			smutils->LogError(myself, "Cannot create the result of job %u (error %d)", static_cast<unsigned int>(id),
			                  handleError);
		}
		function->PushCell(static_cast<cell_t>(code));
		function->PushCell(static_cast<cell_t>(result));
		function->PushCell(m_callbacks.data);
		function->Execute(nullptr);
		/* The result lives as long as the callback; Clone() keeps it longer. A
		 * plugin that deleted it already makes this fail, which is fine. */
		handlesys->FreeHandle(result, &security);
	}

	void OnProgress(JobId id, const ProgressReport &report) override
	{
		IPluginFunction *function;
		if (!m_callbacks.hasProgressCallback ||
		    !m_context->GetFunctionByIdOrNull(m_callbacks.progressCallback, &function) || !function)
			return;
		auto saturated = [](uint64_t value) { return static_cast<cell_t>(value > 0x7fffffff ? 0x7fffffff : value); };
		function->PushCell(static_cast<cell_t>(id));
		function->PushCell(saturated(report.bytesRead));
		function->PushCell(saturated(report.bytesWritten));
		function->PushCell(saturated(report.entriesDone));
		function->PushCell(report.inputSize < 0 ? -1 : saturated(static_cast<uint64_t>(report.inputSize)));
		function->PushCell(m_callbacks.data);
		function->Execute(nullptr);
	}

private:
	IPluginContext *m_context;
	PluginCallbacks m_callbacks;
};

/* A shim's job: its listener gets how it ended, nothing of its progress. */
class ShimReceiver : public JobReceiver {
public:
	explicit ShimReceiver(smarchive_api::IJobListener *listener) : m_listener(listener) {}

	void OnFinished(JobId id, JobOutcome outcome) override
	{
		smarchive_api::Outcome finished{static_cast<int>(outcome.status.code), outcome.status.libraryCode};
		m_listener->OnJobFinished(id, finished);
	}

	void OnProgress(JobId, const ProgressReport &) override {}

private:
	smarchive_api::IJobListener *m_listener;
};

} // namespace

bool SMArchive::CreateHandleTypes(char *error, size_t maxlength)
{
	struct NamedType {
		const char *name;
		HandleType_t *type;
	};
	const NamedType types[] = {
		{"CodecOptions", &codecOptionsType},
		{"ArchiveWriteOptions", &writeOptionsType},
		{"ArchiveLimits", &limitsType},
		{"ArchiveSources", &sourcesType},
		{"ArchiveResult", &resultType},
		{"ArchivePrefixes", &prefixesType},
	};
	for (const NamedType &named : types) {
		HandleError handleError;
		*named.type = handlesys->CreateType(named.name, this, 0, nullptr, nullptr, myself->GetIdentity(),
		                                    &handleError);
		if (*named.type == 0) {
			snprintf(error, maxlength, "cannot create the handle type %s (error %d)", named.name, handleError);
			return false;
		}
	}
	return true;
}

bool SMArchive::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	if (!CreateHandleTypes(error, maxlength))
		return false;
	m_desk = std::make_unique<JobDesk>();
	sharesys->AddNatives(myself, g_OptionNatives);
	sharesys->AddNatives(myself, g_JobNatives);
	sharesys->AddNatives(myself, g_ResultNatives);
	sharesys->AddNatives(myself, g_ServiceNatives);
	sharesys->RegisterLibrary(myself, "smarchive");
	sharesys->AddInterface(myself, this);
	plsys->AddPluginsListener(this);
	smutils->AddGameFrameHook(OnGameFrame);
	return true;
}

void SMArchive::SDK_OnUnload()
{
	smutils->RemoveGameFrameHook(OnGameFrame);
	plsys->RemovePluginsListener(this);
	m_desk.reset();
	for (HandleType_t type : {codecOptionsType, writeOptionsType, limitsType, sourcesType, resultType, prefixesType})
		handlesys->RemoveType(type, myself->GetIdentity());
}

void SMArchive::OnPluginUnloaded(IPlugin *plugin)
{
	m_desk->OwnerUnloaded(plugin->GetBaseContext());
}

void SMArchive::OnHandleDestroy(HandleType_t type, void *object)
{
	if (type == codecOptionsType)
		delete static_cast<CodecOptions *>(object);
	else if (type == writeOptionsType)
		delete static_cast<WriteOptions *>(object);
	else if (type == limitsType)
		delete static_cast<Limits *>(object);
	else if (type == sourcesType)
		delete static_cast<SourcesObject *>(object);
	else if (type == resultType)
		delete static_cast<ResultObject *>(object);
	else if (type == prefixesType)
		delete static_cast<PrefixesObject *>(object);
}

JobId SMArchive::Submit(IPluginContext *context, JobRequest request)
{
	IPlugin *plugin = plsys->FindPluginByContext(context->GetContext());
	JobSubmission submission{context, plugin ? plugin->GetFilename() : "?", std::move(request.description),
	                         std::move(request.job), request.isSerial ? m_desk->OwnerLane(context) : kNoLane,
	                         request.priority};
	return m_desk->Submit(std::move(submission), std::make_unique<PluginReceiver>(context, request.callbacks));
}

bool SMArchive::CancelOwnJob(IPluginContext *context, JobId id)
{
	return m_desk->Cancel(context, id);
}

uint64_t SMArchive::CreateLane()
{
	return m_desk->NewLane();
}

uint64_t SMArchive::QueueCodecFile(const smarchive_api::CodecFileRequest &request, uint64_t lane,
                                   smarchive_api::IJobListener *listener)
{
	bool isBrotli = request.codec == smarchive_api::kCodecBrotli;
	CodecChoice choice;
	choice.isCompress = request.isCompress;
	choice.codec = isBrotli ? Codec::Brotli : Codec::Bzip2;
	if (isBrotli)
		choice.options = BrotliOptions{request.quality, request.window, static_cast<BrotliMode>(request.mode)};
	else
		choice.options = Bzip2Options{request.level};
	OutputFile output{request.outputPath, std::string(request.tempPath), request.overwrite, request.createPath};

	JobSubmission submission{listener,
	                         isBrotli ? "smbrotli" : "smbz2",
	                         std::string(request.isCompress ? "compress " : "decompress ") + request.inputPath,
	                         Job{CodecFileJob{choice, request.inputPath, output}, m_defaultLimits},
	                         lane,
	                         Priority::Normal};
	return m_desk->Submit(std::move(submission), std::make_unique<ShimReceiver>(listener));
}

void SMArchive::Cancel(uint64_t job)
{
	m_desk->Drop(job);
}

void SMArchive::SetThreadCount(int threads)
{
	m_desk->SetThreadCount(threads);
}

void SMArchive::OnFrame()
{
	m_desk->Poll(Clock::now());
}

std::vector<std::string> SMArchive::StatusLines()
{
	return m_desk->StatusLines(Clock::now());
}

std::vector<std::string> SMArchive::VersionLines() const
{
	std::vector<std::string> lines;
	lines.push_back(std::string(SMEXT_CONF_NAME) + " " + SMARCHIVE_VERSION_STRING);
	for (Codec codec : kAllCodecs)
		lines.push_back(std::string(CodecName(codec)) + ": " + CodecLibraryVersion(codec));
	lines.push_back("tar, zip, 7z, rar: " + ArchiveLibraryVersion());
	return lines;
}
