// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace nexus::mods {
using Bytes=std::vector<uint8_t>;
struct Entry {std::string name;uint64_t size{};bool directory{};};
struct Info {bool exists{},directory{};uint64_t size{};};
// USB paths are absolute, SD paths relative to the verified physical SD root.
// Implementations must not follow aliases/symlinks or replace on New/Rename.
struct IO {
    std::string error;
    virtual ~IO()=default;
    bool Fail(const std::string& e){error=e;return false;}
    virtual bool Check()=0;
    virtual bool Stat(bool usb,const std::string&,Info&)=0;
    virtual bool List(const std::string&,std::vector<Entry>&)=0;
    virtual bool Read(bool usb,const std::string&,size_t limit,Bytes&)=0;
    virtual bool New(bool usb,const std::string&,const Bytes&)=0;
    virtual bool Rename(const std::string& from,const std::string& to)=0;
    virtual bool Delete(const std::string&)=0;
    virtual std::string Hash(const Bytes&)=0;
    virtual void Random(void*,size_t)=0;
};
struct Candidate {std::string name,path,id;bool romfs{},exefs{},ips{},registered{};};
struct Cached {std::string path,sha;uint64_t size{};bool owned{};};
struct Change {std::string path,old_hash,new_hash;Bytes bytes;};
struct Plan {
    std::string key,title,root,marker_path,backup_dir;
    Bytes marker;
    bool new_marker{};
    uint64_t data_bytes{};size_t file_count{};
    std::vector<Change> changes;
};
bool Title(const std::string&,uint64_t* out=nullptr);
bool BuildIndex(IO&,const std::string& root,const std::string& title,const Bytes& token,Bytes&,uint64_t& bytes,size_t& files);
bool Scan(IO&,const std::string& atmosphere,std::vector<Candidate>&);
bool Prepare(IO&,const Candidate&,bool exefs,Plan&);
bool Apply(IO&,Plan&);
bool Recover(IO&); // Only our hash-checked pending SD transaction. USB data untouched.
bool HasPending(IO&,bool&);
}
