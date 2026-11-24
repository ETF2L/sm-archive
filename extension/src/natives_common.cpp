/* Copyright 2026 ETF2L Contributors, Dave */
#include "natives_common.h"
#include "paths.h"

#include <limits>

namespace natives {

namespace {

bool IsAddressValid(IPluginContext *context, int64_t address)
{
	return address >= 0 && address <= std::numeric_limits<cell_t>::max() &&
	       context->LocalToPhysAddr(static_cast<cell_t>(address), nullptr) == SP_ERROR_NONE;
}

/* Where the plugin's stack begins: the offset of params in its memory, or
 * -1 when params lie outside it (a native called by other code than the
 * VM). */
int64_t StackPointerOf(IPluginContext *context, const cell_t *params)
{
	cell_t *base = nullptr;
	if (context->LocalToPhysAddr(0, &base) != SP_ERROR_NONE)
		return -1;
	int64_t offset = reinterpret_cast<const uint8_t *>(params) - reinterpret_cast<const uint8_t *>(base);
	return IsAddressValid(context, offset) ? offset : -1;
}

} // namespace

void *ResolveSpan(IPluginContext *context, const cell_t *params, cell_t address, uint64_t size,
                  const char *argumentName)
{
	cell_t *start = nullptr;
	if (context->LocalToPhysAddr(address, &start) != SP_ERROR_NONE) {
		context->ReportError("%s: invalid address %x", argumentName, address);
		return nullptr;
	}
	if (size == 0)
		return start;
	/* No plugin's memory holds more bytes than a cell can address. */
	bool isTooLarge = size > static_cast<uint64_t>(std::numeric_limits<cell_t>::max());
	int64_t last = isTooLarge ? -1 : static_cast<int64_t>(address) + static_cast<int64_t>(size) - 1;
	int64_t stack = StackPointerOf(context, params);
	bool isCrossingTheGap = stack >= 0 && address < stack && last >= stack;
	if (isTooLarge || !IsAddressValid(context, last) || isCrossingTheGap) {
		context->ReportError("%s: %llu bytes do not fit into the plugin's memory", argumentName,
		                     static_cast<unsigned long long>(size));
		return nullptr;
	}
	return start;
}

cell_t CreateHandle(IPluginContext *context, HandleType_t type, void *object)
{
	HandleError error;
	Handle_t handle = handlesys->CreateHandle(type, object, context->GetIdentity(), myself->GetIdentity(), &error);
	if (handle == BAD_HANDLE) {
		g_SMArchive.OnHandleDestroy(type, object);
		context->ReportError("Cannot create a handle (error %d)", error);
		return 0;
	}
	return static_cast<cell_t>(handle);
}

bool ReadGamePath(IPluginContext *context, cell_t address, const char *argumentName, GamePath &path)
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
	path = GamePath{text, buffer};
	return true;
}

bool ReadOptionalGamePath(IPluginContext *context, cell_t address, const char *argumentName, GamePath &path,
                          bool &isGiven)
{
	char *text;
	context->LocalToString(address, &text);
	isGiven = text[0] != '\0';
	return !isGiven || ReadGamePath(context, address, argumentName, path);
}

cell_t Saturated(int64_t value)
{
	constexpr int64_t kMax = 0x7fffffff;
	constexpr int64_t kMin = -kMax - 1;
	return static_cast<cell_t>(value > kMax ? kMax : value < kMin ? kMin : value);
}

bool ReadInt64(IPluginContext *context, const cell_t *params, cell_t address, int64_t &value)
{
	auto *pair = static_cast<cell_t *>(ResolveSpan(context, params, address, CellBytes(2), "int[2]"));
	if (!pair)
		return false;
	value = static_cast<int64_t>(static_cast<uint64_t>(static_cast<uint32_t>(pair[0])) |
	                             (static_cast<uint64_t>(static_cast<uint32_t>(pair[1])) << 32));
	return true;
}

bool WriteInt64(IPluginContext *context, const cell_t *params, cell_t address, int64_t value)
{
	auto *pair = static_cast<cell_t *>(ResolveSpan(context, params, address, CellBytes(2), "int[2]"));
	if (!pair)
		return false;
	uint64_t bits = static_cast<uint64_t>(value);
	pair[0] = static_cast<cell_t>(static_cast<uint32_t>(bits & 0xffffffffu));
	pair[1] = static_cast<cell_t>(static_cast<uint32_t>(bits >> 32));
	return true;
}

cell_t WriteString(IPluginContext *context, const cell_t *params, cell_t address, cell_t maxlength,
                   const std::string &text)
{
	if (maxlength < 0) {
		context->ReportError("Invalid maxlength %d", maxlength);
		return 0;
	}
	if (!ResolveSpan(context, params, address, static_cast<uint64_t>(maxlength), "buffer"))
		return 0;
	size_t written = 0;
	context->StringToLocalUTF8(address, static_cast<size_t>(maxlength), text.c_str(), &written);
	return static_cast<cell_t>(written);
}

bool ReadOptionalFunction(IPluginContext *context, cell_t value, bool &isGiven, funcid_t &function)
{
	/* spcomp passes the INVALID_FUNCTION default of a typed function
	 * argument as 0, which is never a function's id; SourcePawn's own null
	 * value is told by GetFunctionByIdOrNull(). */
	function = static_cast<funcid_t>(value);
	if (value == 0) {
		isGiven = false;
		return true;
	}
	IPluginFunction *found = nullptr;
	if (!context->GetFunctionByIdOrNull(function, &found)) {
		context->ReportError("Invalid function %x", value);
		return false;
	}
	isGiven = found != nullptr;
	return true;
}

} // namespace natives
