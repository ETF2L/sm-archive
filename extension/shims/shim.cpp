/* Copyright 2026 ETF2L Contributors, Dave */
#include "shim.h"
#include "paths.h"

namespace {

/* A path a plugin passes, as written and resolved below the game
 * directory; false after a native error, also when it does not fit
 * PLATFORM_MAX_PATH there. */
bool ReadGamePath(IPluginContext *context, cell_t address, const char *argumentName, std::string &relative,
                  std::string &resolved)
{
	char *text;
	context->LocalToString(address, &text);
	if (!smarchive::IsGameRelativePath(text)) {
		context->ReportError("%s \"%s\" must be a path relative to the game directory: it must %s", argumentName,
		                     text, smarchive::kGamePathRule);
		return false;
	}
	char buffer[PLATFORM_MAX_PATH];
	size_t length = smutils->BuildPath(Path_Game, buffer, sizeof(buffer), "%s", text);
	if (length >= sizeof(buffer) - 1) {
		context->ReportError("%s is too long: below the game directory it must stay shorter than %d bytes (\"%s\")",
		                     argumentName, static_cast<int>(sizeof(buffer) - 1), text);
		return false;
	}
	relative = text;
	resolved = buffer;
	return true;
}

} // namespace

bool Shim::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	sharesys->AddDependency(myself, "smarchive.ext", true, true);
	if (!sharesys->RequestInterface(SMINTERFACE_SMARCHIVE_SHIMS_NAME, SMINTERFACE_SMARCHIVE_SHIMS_VERSION, myself,
	                                reinterpret_cast<SMInterface **>(&m_archive))) {
		snprintf(error, maxlength, "smarchive.ext %d or newer is not loaded", SMINTERFACE_SMARCHIVE_SHIMS_VERSION);
		return false;
	}
	m_lane = m_archive->CreateLane();
	sharesys->AddNatives(myself, Natives());
	sharesys->RegisterLibrary(myself, LibraryName());
	plsys->AddPluginsListener(this);
	return true;
}

void Shim::SDK_OnUnload()
{
	plsys->RemovePluginsListener(this);
	for (const auto &[job, callback] : m_pending)
		m_archive->Cancel(job);
	m_pending.clear();
}

void Shim::OnPluginUnloaded(IPlugin *plugin)
{
	IPluginContext *context = plugin->GetBaseContext();
	for (auto it = m_pending.begin(); it != m_pending.end();) {
		if (it->second.context != context) {
			++it;
			continue;
		}
		m_archive->Cancel(it->first);
		it = m_pending.erase(it);
	}
}

cell_t Shim::Queue(IPluginContext *context, const cell_t *params, smarchive_api::CodecFileRequest request)
{
	std::string input, inputResolved, output, outputResolved;
	if (!ReadGamePath(context, params[1], "inFile", input, inputResolved) ||
	    !ReadGamePath(context, params[2], "outFile", output, outputResolved))
		return 0;
	if (inputResolved == outputResolved)
		return context->ThrowNativeError("inFile \"%s\" and outFile \"%s\" are the same file", input.c_str(),
		                                 output.c_str());
	IPluginFunction *function;
	if (!context->GetFunctionByIdOrNull(static_cast<funcid_t>(params[3]), &function) || !function)
		return context->ThrowNativeError("Invalid callback function %x", params[3]);

	std::string tempPath = outputResolved + ".tmp";
	request.inputPath = inputResolved.c_str();
	request.outputPath = outputResolved.c_str();
	request.tempPath = tempPath.c_str();
	uint64_t job = m_archive->QueueCodecFile(request, m_lane, this);
	m_pending.emplace(job, ShimCallback{context, static_cast<funcid_t>(params[3]), params[4], input, output});
	return 0;
}

void Shim::OnJobFinished(uint64_t job, const smarchive_api::Outcome &outcome)
{
	auto found = m_pending.find(job);
	if (found == m_pending.end())
		return;
	ShimCallback callback = std::move(found->second);
	m_pending.erase(found);

	IPluginFunction *function;
	if (!callback.context->GetFunctionByIdOrNull(callback.function, &function) || !function)
		return;
	function->PushCell(OldError(outcome));
	function->PushString(callback.inputPath.c_str());
	function->PushString(callback.outputPath.c_str());
	function->PushCell(callback.data);
	function->Execute(nullptr);
}
