#pragma once

// clang-format off
#define VERSION_MAJOR               1
#define VERSION_MINOR               1
#define VERSION_REVISION            11
#define VERSION_PATCH               1
#define VERSION_COMMIT_HASH         ""

#define STRINGIFY_(s)               #s
#define STRINGIFY(s)                STRINGIFY_(s)

#if VERSION_PATCH
	#define VERSION_PATCH_STR " (Dev)"
#else
	#define VERSION_PATCH_STR ""
#endif

#define VER_FILE_DESCRIPTION_STR    "Star Trek Fleet Command: Community Mod" VERSION_PATCH_STR

#define VER_FILE_VERSION            VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION, VERSION_PATCH
#define VER_FILE_VERSION_STR        STRINGIFY(VERSION_MAJOR) "." STRINGIFY(VERSION_MINOR) "." STRINGIFY(VERSION_REVISION) "." STRINGIFY(VERSION_PATCH)

#define STFC_DISTRIBUTION_ID        "netniv.stfc-community-mod"

#ifndef STFC_SOURCE_STATE_ID
#define STFC_SOURCE_STATE_ID        "unknown"
#endif

#ifndef STFC_BASE_COMMIT
#define STFC_BASE_COMMIT            "unknown"
#endif

#ifndef STFC_BUILD_INVOCATION_ID
#define STFC_BUILD_INVOCATION_ID    "zig-direct"
#endif

#ifndef STFC_BUILD_MODE
#define STFC_BUILD_MODE             "unknown"
#endif

#ifndef STFC_BUILD_CHANNEL
#define STFC_BUILD_CHANNEL          "local"
#endif

#define STFC_IDENTITY_COMMENT_STR   "stfc-identity-v1;distribution=" STFC_DISTRIBUTION_ID \
                                    ";source=" STFC_SOURCE_STATE_ID \
                                    ";base=" STFC_BASE_COMMIT \
                                    ";build=" STFC_BUILD_INVOCATION_ID \
                                    ";mode=" STFC_BUILD_MODE \
                                    ";channel=" STFC_BUILD_CHANNEL

#define VER_PRODUCTNAME_STR         "STFC: Community Mod"
#define VER_PRODUCT_VERSION         VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION
#define VER_PRODUCT_VERSION_STR     VER_FILE_VERSION_STR
#define VER_ORIGINAL_FILENAME_STR   "stfc-community-mod.dll"
#define VER_INTERNAL_NAME_STR       VER_ORIGINAL_FILENAME_STR
#define VER_COPYRIGHT_STR           "Copyright (C) 2026"

#ifdef DEBUG
#define VER_FILEFLAGS               VS_FF_DEBUG
#else
#define VER_FILEFLAGS               0
#endif

#ifndef VS_VERSION_INFO
#define VS_VERSION_INFO 1
#endif
// clang-format on
