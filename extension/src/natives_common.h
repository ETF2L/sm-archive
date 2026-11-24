/* Copyright 2026 ETF2L Contributors, Dave
 *
 * What the natives share: reading handles and paths a plugin passes (every
 * value is checked here, so the core only sees valid ones), the spans of
 * plugin memory a native reads or writes (checked to lie inside it), and the
 * 64-bit values SourcePawn sees as saturated ints or int[2] pairs. Each
 * reader throws a native error and returns false/nullptr on a bad value. */
#ifndef SMARCHIVE_NATIVES_COMMON_H
#define SMARCHIVE_NATIVES_COMMON_H

#include "extension.h"

#include <cstdint>
#include <string>

namespace natives {

/* The object of a handle of type, or nullptr after a native error. */
template <typename T>
T *ReadObject(IPluginContext *context, cell_t handle, HandleType_t type, const char *typeName)
{
	HandleSecurity security(context->GetIdentity(), myself->GetIdentity());
	void *object = nullptr;
	HandleError error = handlesys->ReadHandle(static_cast<Handle_t>(handle), type, &security, &object);
	if (error != HandleError_None) {
		context->ReportError("Invalid %s handle %x (error %d)", typeName, handle, error);
		return nullptr;
	}
	return static_cast<T *>(object);
}

/* A new handle owned by the calling plugin; 0 after a native error. */
cell_t CreateHandle(IPluginContext *context, HandleType_t type, void *object);

/* A path a plugin passes, as written and resolved below the game
 * directory. */
struct GamePath {
	std::string relative;
	std::string resolved;
};

/* Refused, besides by the rule of smarchive::IsGameRelativePath(), when it
 * does not fit PLATFORM_MAX_PATH below the game directory. */
bool ReadGamePath(IPluginContext *context, cell_t address, const char *argumentName, GamePath &path);

/* An optional path: an empty string is none. */
bool ReadOptionalGamePath(IPluginContext *context, cell_t address, const char *argumentName, GamePath &path,
                          bool &isGiven);

/* The physical address of size bytes of plugin memory at address, or
 * nullptr after a native error when they do not all lie in it. A plugin's
 * memory is its data and heap from 0 up to its heap pointer, a free gap,
 * and its stack up to the end; SourcePawn only checks the first address of
 * an array. params, the native's arguments, lie on top of the plugin's
 * stack and tell where it begins, so a span that would cross the gap is
 * refused too. */
void *ResolveSpan(IPluginContext *context, const cell_t *params, cell_t address, uint64_t size,
                  const char *argumentName);

/* The bytes of count cells, computed without wrapping on 32-bit servers. */
constexpr uint64_t CellBytes(cell_t count)
{
	return static_cast<uint64_t>(count) * sizeof(cell_t);
}

/* The value clamped to SourcePawn's int. */
cell_t Saturated(int64_t value);

/* An int[2] (low, high); false after a native error. */
bool ReadInt64(IPluginContext *context, const cell_t *params, cell_t address, int64_t &value);
bool WriteInt64(IPluginContext *context, const cell_t *params, cell_t address, int64_t value);

/* Copies text into a plugin's buffer of maxlength bytes; returns the bytes
 * written, 0 after a native error. */
cell_t WriteString(IPluginContext *context, const cell_t *params, cell_t address, cell_t maxlength,
                   const std::string &text);

/* A function argument that may be INVALID_FUNCTION. */
bool ReadOptionalFunction(IPluginContext *context, cell_t value, bool &isGiven, funcid_t &function);

} // namespace natives

#endif // SMARCHIVE_NATIVES_COMMON_H
