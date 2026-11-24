/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The shim's description for SourceMod's smsdk_ext: the old extension's
 * name, SMArchive's version. */
#ifndef _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_
#define _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_

#include "../../src/version.h"

#define SMEXT_CONF_NAME        "SMbrotli"
#define SMEXT_CONF_DESCRIPTION "Compresses and decompresses brotli files in the background (on SMArchive)"
#define SMEXT_CONF_VERSION     SMARCHIVE_VERSION_STRING
#define SMEXT_CONF_AUTHOR      "ETF2L Contributors, Dave"
#define SMEXT_CONF_URL         "https://github.com/etf2l/sm-archive"
#define SMEXT_CONF_LOGTAG      "SMBROTLI"
#define SMEXT_CONF_LICENSE     "GPL"
#define SMEXT_CONF_DATESTRING  __DATE__

#define SMEXT_LINK(name) SDKExtension *g_pExtensionIface = name;

#define SMEXT_ENABLE_PLUGINSYS

#endif // _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_
