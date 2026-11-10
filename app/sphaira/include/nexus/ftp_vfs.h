// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>
struct FtpVfsFile {void* impl;};
struct FtpVfsDir {void* impl;};
struct FtpVfsDirEntry {FsDirectoryEntry entry;};
#ifdef __cplusplus
extern "C" {
#endif
int nexus_ftp_finish(struct FtpVfsFile*);
#ifdef __cplusplus
}
#endif
