// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/localize.hpp"
#include "nexus/network_rate.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::transfer::activity {
using Id=std::uint64_t;
inline Id Now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
// This is a user-facing session journal, not a raw protocol/credential log.
inline std::string Clean(std::string_view text,std::size_t limit=220){
    std::string out;out.reserve(std::min(text.size(),limit));
    for(unsigned char c:text){if(out.size()==limit)break;out+=c<32||c==127?' ':char(c);}
    if(text.size()>limit){
        while(!out.empty()&&(static_cast<unsigned char>(text[out.size()])&0xc0)==0x80)out.pop_back();
        out+="...";
    }
    return out;
}
inline std::string Name(std::string_view path){const auto at=path.find_last_of("/\\");return Clean(at==path.npos?path:path.substr(at+1),140);}
inline std::string Error(std::uint32_t rc){char text[32]{};std::snprintf(text,sizeof(text),"0x%08X",rc);return text;}
struct Line {Id id{},seconds{};std::string text;};
struct Upload {Id id{},bytes{};std::int64_t expected{-1};std::string name,target;bool active{};};
inline std::string SizeText(Id bytes){char out[64]{};std::snprintf(out,sizeof(out),"%.2f MiB",double(bytes)/1048576.0);return out;}
struct Content {
    std::string name,kind,phase;
    Id bytes{},size{},packed{};
    bool measuring{};
    network::RateSample rate;
};
struct Snapshot {
    std::string connection,install_name,install_target,stage;
    std::vector<Line> lines;
    Upload upload;
    network::RateSample rate;
    Id uploaded{},installed{},queued{},errors{},active_uploads{};
    bool installing{},stopped{};
    Content content;
    bool streaming{};
};
class Journal {
    std::mutex mutex;
    std::deque<Line> lines;
    std::vector<Upload> uploads;
    Upload latest;
    network::RateMeter rate,content_rate;
    Content content;bool streaming{};
    Id origin{},sequence{},next_upload{},uploaded{},installed{},queued{},errors{};
    std::string connection,install_name,install_target,stage;
    bool installing{},stopped{},enabled{};
    void Add(std::string_view text,Id now){
        if(!enabled||text.empty())return;
        if(lines.size()==128)lines.pop_front();
        lines.push_back({++sequence,now>=origin?(now-origin)/1'000'000'000:0,Clean(text)});
    }
public:
    void Start(std::string_view label,Id now=Now()){
        std::scoped_lock lock(mutex);
        lines.clear();uploads.clear();latest={};content={};content_rate.End();streaming=false;uploaded=installed=queued=errors=0;
        // IDs never repeat across sessions: a stale UI scroll anchor cannot
        // point at a different message after reconnection.
        origin=now;connection=Clean(label);install_name.clear();install_target.clear();stage.clear();
        installing=stopped=false;enabled=true;rate.Begin(now);Add("Preparing connection"_nx,now);
    }
    void Connect(std::string_view label,Id now=Now()){
        std::scoped_lock lock(mutex);connection=Clean(label);Add("Ready. Drop a package into Install to SD or Install to USB."_nx,now);
    }
    void Event(std::string_view message,Id now=Now()){std::scoped_lock lock(mutex);Add(message,now);}
    void Failure(std::string_view message,std::uint32_t rc,Id now=Now()){
        std::scoped_lock lock(mutex);if(!enabled)return;++errors;Add(std::string(message)+" ["+Error(rc)+"]",now);
    }
    Id Open(std::string_view path,std::string_view target,std::int64_t expected,Id now=Now()){
        std::scoped_lock lock(mutex);if(!enabled)return 0;
        const auto id=++next_upload;latest={id,0,expected,Name(path),Clean(target,40),true};
        // Telemetry exhaustion never rejects an otherwise valid transfer.
        if(uploads.size()<32)uploads.push_back(latest);
        Add("Receiving to "_nx+latest.target+": "+latest.name,now);return id;
    }
    void Bytes(Id id,Id bytes,std::int64_t expected){
        std::scoped_lock lock(mutex);if(!id)return;
        const auto it=std::find_if(uploads.begin(),uploads.end(),[&](const auto& u){return u.id==id;});
        if(it==uploads.end())return;
        rate.Add(bytes);it->bytes+=bytes;it->expected=expected;latest=*it;
    }
    void Saving(Id id,Id now=Now()){
        std::scoped_lock lock(mutex);const auto it=std::find_if(uploads.begin(),uploads.end(),[&](const auto& u){return u.id==id;});
        if(it!=uploads.end())Add("Finishing upload to "_nx+it->target+": "+it->name,now);
    }
    void Close(Id id,bool complete,bool for_install,std::uint32_t rc,Id now=Now()){
        std::scoped_lock lock(mutex);const auto it=std::find_if(uploads.begin(),uploads.end(),[&](const auto& u){return u.id==id;});
        if(it==uploads.end())return;
        latest=*it;latest.active=false;uploads.erase(it);
        if(complete&&!rc){++uploaded;if(for_install)++queued;Add((for_install?"Upload complete; queued for install: "_nx:"Upload complete: "_nx)+latest.name,now);}
        else{++errors;Add((rc?"Upload failed ["_nx+Error(rc)+"]: ":std::string("Upload interrupted: "_nx))+latest.name,now);Add("Unfinished upload kept. It will not be installed."_nx,now);}
        if(!uploads.empty())latest=uploads.back();
    }
    void CloseStream(Id id,bool complete,std::uint32_t rc,Id now=Now()){
        std::scoped_lock lock(mutex);const auto it=std::find_if(uploads.begin(),uploads.end(),[&](const auto& u){return u.id==id;});
        if(it==uploads.end())return;
        latest=*it;latest.active=false;uploads.erase(it);
        if(complete&&!rc){++uploaded;Add("FTP stream finished: "_nx+latest.name,now);}
        else{++errors;Add("FTP stream stopped ["_nx+Error(rc)+"]. Resend the original package from your PC."_nx,now);}
    }
    void Streaming(){std::scoped_lock lock(mutex);streaming=true;}
    void ContentFile(std::string_view name,std::string_view kind,std::string_view phase,Id size,Id packed=0,Id now=Now()){
        std::scoped_lock lock(mutex);if(!installing)return;
        content={Name(name),Clean(kind,32),Clean(phase,64),0,size,packed,true,{}};content_rate.Begin(now);
        Add(content.phase+" | "+content.name+" | "+SizeText(size),now);
    }
    void ContentBytes(Id offset,Id size){
        std::scoped_lock lock(mutex);if(!installing||content.name.empty()||offset>size)return;
        if(offset>=content.bytes)content_rate.Add(offset-content.bytes);
        content.bytes=offset;content.size=size;
    }
    void ContentPhase(std::string_view value,Id now=Now()){
        std::scoped_lock lock(mutex);if(!installing||content.name.empty())return;
        content.phase=Clean(value,64);content.measuring=false;content_rate.End();
        Add(content.phase+" | "+content.name+" | "+SizeText(content.size),now);
    }
    void BeginInstall(std::string_view path,std::string_view target,Id now=Now()){
        std::scoped_lock lock(mutex);if(!enabled)return;
        if(queued)--queued;
        installing=true;content={};content_rate.End();streaming=false;install_name=Name(path);install_target=Clean(target,40);stage.clear();
        Add("Installing to "_nx+install_target+": "+install_name,now);
    }
    void Stage(std::string_view value,Id now=Now()){
        std::scoped_lock lock(mutex);if(!installing||value.empty())return;
        auto next=Clean(value);if(next==stage)return;stage=next;Add("Step: "_nx+stage,now);
    }
    void FinishInstall(std::uint32_t rc,Id now=Now()){
        std::scoped_lock lock(mutex);if(!installing)return;installing=false;
        content.measuring=false;content_rate.End();
        if(!rc){++installed;Add("Installed successfully: "_nx+install_name,now);stage="Installation complete"_nx;}
        else{++errors;Add("Installation stopped ["_nx+Error(rc)+"]: "+install_name,now);Add(streaming?"Queue paused. Resend the original package from your PC."_nx:"Queue paused. Package kept; retry from Install Games."_nx,now);stage="Installation stopped"_nx;}
    }
    void Stop(Id now=Now()){
        std::scoped_lock lock(mutex);if(!enabled||stopped)return;stopped=true;
        queued=0;
        Add("Connection stopped. Uploaded packages have been kept."_nx,now);
    }
    Snapshot Read(Id now=Now()){
        std::scoped_lock lock(mutex);
        content.rate=content_rate.Sample(now);
        return {connection,install_name,install_target,stage,{lines.begin(),lines.end()},latest,rate.Sample(now),uploaded,installed,queued,errors,uploads.size(),installing,stopped,content,streaming};
    }
};
inline Journal& Get(){static Journal journal;return journal;}
// Zero follows new messages. A nonzero message ID stays anchored while workers
// append lines; eviction clamps the view to the oldest retained message.
struct Cursor {
    Id anchor{};
    static constexpr std::size_t Rows=7;
    std::size_t End(const Snapshot& s)const{
        if(!anchor)return s.lines.size();
        const auto it=std::upper_bound(s.lines.begin(),s.lines.end(),anchor,[](Id id,const Line& line){return id<line.id;});
        return std::min(s.lines.size(),std::max(Rows,std::size_t(it-s.lines.begin())));
    }
    void Scroll(const Snapshot& s,int direction){
        if(s.lines.empty())return;
        auto end=End(s);
        if(direction<0){if(end>Rows)--end;anchor=s.lines[end-1].id;}
        else{if(end<s.lines.size())++end;anchor=end==s.lines.size()?0:s.lines[end-1].id;}
    }
    void Follow(){anchor=0;}
};
}
