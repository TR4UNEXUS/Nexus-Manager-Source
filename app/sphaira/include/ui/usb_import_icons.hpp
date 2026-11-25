// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app.hpp"
#include "nexus/usb_import_scan.hpp"
#include "utils/thread.hpp"
#include <map>
#include <algorithm>

namespace sphaira::ui {
class UsbImportIcons {
    struct Entry {int image{};size_t used{};std::string name;};
    std::shared_ptr<const nexusapp::UsbImportScan> scan;
    std::map<u64,nexusapp::UsbImportFamily> families;
    std::map<u64,Entry> cache;
    size_t clock{};
    Thread thread{};
    std::atomic_bool done{},cancel{};
    u64 reading{};
    nexusapp::UsbImportFamily work;
    nexusapp::UsbImportArtwork loaded;

    static void Load(void* opaque){
        auto& self=*static_cast<UsbImportIcons*>(opaque);
        self.loaded=nexusapp::ReadUsbImportArtwork(self.scan->generation,self.work,&self.cancel);
        self.done.store(true);
    }
    void Join(){
        if(R_FAILED(threadWaitForExit(&thread)))fatalThrow(nexus::Invalid);
        threadClose(&thread);
        reading=0;
    }
public:
    explicit UsbImportIcons(std::shared_ptr<const nexusapp::UsbImportScan> source):scan(std::move(source)){
        for(const auto& row:scan->titles)if(row.record.application_id)families[row.record.application_id].push_back(&row);
    }
    ~UsbImportIcons(){
        Stop();
        for(auto& [id,entry]:cache)if(entry.image>0)nvgDeleteImage(App::GetVg(),entry.image);
    }
    void Stop(){
        cancel.store(true);
        if(reading)Join();
    }
    // Called on the render thread. USB reads and image normalization happen on
    // one worker; a page change can never attach an image to a different game.
    const std::string& Name(u64 id,const std::string& fallback) const {
        const auto it=cache.find(id);
        return it!=cache.end() && !it->second.name.empty()?it->second.name:fallback;
    }
    int Get(NVGcontext* vg,u64 id){
        if(reading && done.load()){
            const auto key=reading;Join();
            auto& entry=cache.at(key);
            entry.name=std::move(loaded.name);
            if(!loaded.icon.empty())entry.image=nvgCreateImageMem(vg,0,loaded.icon.data(),loaded.icon.size());
            loaded={};
        }
        if(auto it=cache.find(id);it!=cache.end()){
            it->second.used=++clock;
            return it->second.image>0?it->second.image:App::GetDefaultImage();
        }
        auto it=families.find(id);
        if(!reading && !cancel.load() && it!=families.end()){
            if(cache.size()>=24){
                auto oldest=std::min_element(cache.begin(),cache.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
                if(oldest->second.image>0)nvgDeleteImage(vg,oldest->second.image);
                cache.erase(oldest);
            }
            cache[id].used=++clock;
            work=it->second;
            done.store(false);
            if(R_SUCCEEDED(utils::CreateThread(&thread,Load,this,256*1024))){
                if(R_SUCCEEDED(threadStart(&thread)))reading=id;
                else threadClose(&thread);
            }
        }
        return App::GetDefaultImage();
    }
};
}
