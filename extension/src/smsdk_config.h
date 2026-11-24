/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The extension's description for SourceMod's smsdk_ext. */
#ifndef _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_
#define _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_

#include "version.h"

#define SMEXT_CONF_NAME        "SourceMod compression and archives"
#define SMEXT_CONF_DESCRIPTION "Compresses, decompresses, lists, extracts and creates archives in the background"
#define SMEXT_CONF_VERSION     SMARCHIVE_VERSION_STRING
#define SMEXT_CONF_AUTHOR      "ETF2L Contributors, Dave"
#define SMEXT_CONF_URL         "https://github.com/etf2l/sm-archive"
#define SMEXT_CONF_LOGTAG      "SMARCHIVE"
#define SMEXT_CONF_LICENSE     "GPL"
#define SMEXT_CONF_DATESTRING  __DATE__

#define SMEXT_LINK(name) SDKExtension *g_pExtensionIface = name;

#define SMEXT_ENABLE_HANDLESYS
#define SMEXT_ENABLE_PLUGINSYS

#endif // _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_
