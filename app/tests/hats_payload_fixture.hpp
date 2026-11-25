// Runs the real payload/source/fs.c against an in-memory FatFS implementation.
#include <libs/fatfs/ff.h>
extern "C" {
#include "../extras/nexus-hats-payload/source/fs.h"
#include "../extras/nexus-hats-payload/source/boot_config.h"
}
#include "../extras/shared/install_protection.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

struct Node { bool directory = false; std::string data; };
struct FileHandle { std::string path; size_t offset = 0; };
struct DirHandle { std::vector<std::string> children; size_t index = 0; };
static std::map<std::string, Node> nodes;
static std::vector<std::string> traversed, modified;
static std::string fail_stat;
static bool fail_sync=false;

static std::string path(const char* value) {
    std::string s = mm_sd_relative_path(value);
    for (char& c : s) c = c == '\\' ? '/' : mm_path_lower(c);
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}
static std::string parent(const std::string& s) {
    auto pos = s.find_last_of('/');
    return pos == std::string::npos ? "" : s.substr(0, pos);
}
static void mkdirs(const std::string& s) {
    if (nodes.contains(s)) return;
    if (!s.empty()) mkdirs(parent(s));
    nodes[s] = {true, {}};
}
static void put(const std::string& s, const std::string& data) {
    mkdirs(parent(s));
    nodes[s] = {false, data};
}
extern "C" {
int f_open(FIL* file, const char* name, u8 mode) {
    auto s = path(name);
    if (mode & FA_CREATE_ALWAYS) {
        if (!nodes.contains(parent(s))) return FR_NO_PATH;
        nodes[s] = {false, {}};
        modified.push_back(s);
    }
    if (!nodes.contains(s) || nodes[s].directory) return FR_NO_FILE;
    file->handle = new FileHandle{s};
    return FR_OK;
}
int f_close(FIL* file) { delete static_cast<FileHandle*>(file->handle); return FR_OK; }
u64 f_size(FIL* file) { return nodes.at(static_cast<FileHandle*>(file->handle)->path).data.size(); }
int f_read(FIL* file, void* buffer, UINT length, UINT* read) {
    auto* h = static_cast<FileHandle*>(file->handle);
    auto& data = nodes.at(h->path).data;
    *read = static_cast<UINT>(std::min<size_t>(length, data.size() - h->offset));
    std::memcpy(buffer, data.data() + h->offset, *read);
    h->offset += *read;
    return FR_OK;
}
int f_write(FIL* file, const void* buffer, UINT length, UINT* written) {
    auto* h = static_cast<FileHandle*>(file->handle);
    auto& data = nodes.at(h->path).data;
    data.resize(std::max(data.size(), h->offset + length));
    std::memcpy(data.data() + h->offset, buffer, length);
    h->offset += length;
    *written = length;
    return FR_OK;
}
int f_sync(FIL*) { return fail_sync ? FR_DISK_ERR : FR_OK; }
int f_stat(const char* name, FILINFO* info) {
    const auto s = path(name);
    if (s == fail_stat) return FR_DISK_ERR;
    const auto it = nodes.find(s);
    if (it == nodes.end()) return FR_NO_FILE;
    info->fattrib = it->second.directory ? AM_DIR : 0;
    return FR_OK;
}
int f_opendir(DIR* dir, const char* name) {
    auto s = path(name);
    if (!nodes.contains(s) || !nodes[s].directory) return FR_NO_PATH;
    traversed.push_back(s);
    auto* h = new DirHandle;
    for (const auto& [p, node] : nodes)
        if (p != s && parent(p) == s) h->children.push_back(p);
    dir->handle = h;
    return FR_OK;
}
int f_readdir(DIR* dir, FILINFO* info) {
    auto* h = static_cast<DirHandle*>(dir->handle);
    if (h->index == h->children.size()) { info->fname[0] = 0; return FR_OK; }
    const auto& s = h->children[h->index++];
    std::strcpy(info->fname, s.substr(s.find_last_of('/') + 1).c_str());
    info->fattrib = nodes.at(s).directory ? AM_DIR : 0;
    return FR_OK;
}
int f_closedir(DIR* dir) { delete static_cast<DirHandle*>(dir->handle); return FR_OK; }
int f_unlink(const char* name) {
    auto s = path(name);
    if (!nodes.contains(s)) return FR_NO_FILE;
    for (const auto& [p, node] : nodes) if (p != s && parent(p) == s) return FR_DENIED;
    nodes.erase(s);
    modified.push_back(s);
    return FR_OK;
}
int f_mkdir(const char* name) {
    auto s = path(name);
    if (nodes.contains(s)) return FR_EXIST;
    if (!nodes.contains(parent(s))) return FR_NO_PATH;
    nodes[s] = {true, {}};
    modified.push_back(s);
    return FR_OK;
}
int f_chmod(const char* name, u8, u8) { modified.push_back(path(name)); return FR_OK; }
}

