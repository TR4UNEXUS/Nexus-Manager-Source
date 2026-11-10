// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/nx_Policy.hpp"
#include "nexus/package.hpp"
#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace nexus::batch {
inline constexpr std::size_t MaxPackages=256;
struct Entry {std::string name,path;std::uint64_t size{};};
inline bool IsPackage(std::string_view path) {
    return package::Supported(path);
}
inline bool ValidEntry(const Entry& e) {
    return e.size>0 && e.size<=static_cast<std::uint64_t>(INT64_MAX) && VolumePathValid(e.path) &&
        IsPackage(e.path) && e.name==std::string_view(e.path).substr(e.path.find_last_of('/')+1);
}
class Selection {
    std::vector<Entry> entries;
public:
    const auto& Entries() const{return entries;}
    bool Empty() const{return entries.empty();}
    void Clear(){entries.clear();}
    std::size_t Index(std::string_view path) const {
        for(std::size_t i=0;i<entries.size();++i)if(entries[i].path==path)return i;
        return entries.size();
    }
    bool Contains(std::string_view path) const {return Index(path)<entries.size();}
    std::uint64_t Bytes() const {std::uint64_t bytes=0;for(const auto& e:entries)bytes+=e.size;return bytes;}
    bool Add(const Entry& e) {
        if(!ValidEntry(e))return false;
        if(Contains(e.path))return true;
        if(entries.size()==MaxPackages || e.size>UINT64_MAX-Bytes())return false;
        entries.push_back(e);return true;
    }
    void Remove(std::string_view path){std::erase_if(entries,[&](const auto& e){return e.path==path;});}
    bool AddAll(const std::vector<Entry>& candidates) {
        auto next=*this;for(const auto& e:candidates)if(!next.Add(e))return false;
        entries.swap(next.entries);return true;
    }
    bool Move(std::size_t index,bool later) {
        if(index>=entries.size() || (later?index+1==entries.size():index==0))return false;
        std::swap(entries[index],entries[later?index+1:index-1]);return true;
    }
};
enum class State {Pending,Installed,Failed,Cancelled};
inline const char* StateName(State state) {
    switch(state){case State::Installed:return "Installed";case State::Failed:return "Failed";
        case State::Cancelled:return "Cancelled";default:return "Not attempted";}
}
struct ItemResult {Entry entry;State state{State::Pending};std::uint32_t result{};std::string detail{};};
struct Report {
    std::vector<ItemResult> items;
    Target target{};
    std::string stage{"Not started"};
    std::uint32_t result{};
    explicit Report(const std::vector<Entry>& files,Target to):target(to){for(const auto& e:files)items.push_back({e});}
    std::size_t Count(State state) const {return std::count_if(items.begin(),items.end(),[&](const auto& i){return i.state==state;});}
};
// The same sequential engine runs in the app and in host fault-injection tests.
// Validate all files first, stop on any failure, and never roll back successes.
template<class Cancel,class Check,class Install,class Notify>
std::uint32_t Run(Report& report,Cancel cancel,Check check,Install install,Notify notify) {
    const auto fail=[&](std::size_t i,std::uint32_t rc,bool cancelled){
        report.result=rc;report.items[i].result=rc;
        report.items[i].state=cancelled?State::Cancelled:State::Failed;notify(i,false);return rc;
    };
    for(std::size_t i=0;i<report.items.size();++i){
        report.stage="Check source packages";
        if(auto rc=cancel())return fail(i,rc,true);
        if(auto rc=check(report.items[i].entry))return fail(i,rc,false);
    }
    for(std::size_t i=0;i<report.items.size();++i){
        report.stage="Install package";
        if(auto rc=cancel())return fail(i,rc,true);
        // Recheck after prior packages; the medium/file may have changed.
        if(auto rc=check(report.items[i].entry))return fail(i,rc,false);
        notify(i,true);
        if(auto rc=install(report.items[i].entry))return fail(i,rc,cancel()!=0);
        report.items[i].state=State::Installed;notify(i,false);
    }
    report.stage="Complete";report.result=0;return 0;
}
}
