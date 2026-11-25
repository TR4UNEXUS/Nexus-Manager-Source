// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/diagnostics.hpp"
#include "nexus/nx_Client.hpp"
#include "nexus/update.hpp"
#include "nexus/bootstrap.h"
#include "nexus/routing_backup.hpp"
#include "nexus/localize.hpp"
#include "ui/progress_box.hpp"
#include "minizip_helper.hpp"
#include "defines.hpp"
#include <minizip/zip.h>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <sys/stat.h>
#include <algorithm>
namespace nexus {
namespace {
std::mutex log_mutex;
std::atomic<u64> next_operation{1};
std::atomic<u64> current_game{};
thread_local u64 operation{};
thread_local bool operation_failed{};
thread_local const char* operation_name="background";
bool first_saved{},first_rotated{};
// Fixed phase labels and numeric context only. Credentials, packages, tickets
// and server configuration are never read by the diagnostic exporter.
bool Rotate(const char* current,const char* prior){
    struct stat st{};if(stat(current,&st)==0){remove(prior);return rename(current,prior)==0;}return true;
}
}
namespace diagnostics {
u64 CurrentGame(){return current_game.load();}
u64 Session(){static const u64 session=armGetSystemTick();return session;}
Operation::Operation(const char* name):previous(operation),previous_name(operation_name),previous_failure(operation_failed){operation_failed=false;operation=next_operation.fetch_add(1);operation_name=name;Log("operation-begin",0,operation);}
Operation::~Operation(){Log("operation-ended",0,operation);operation=previous;operation_name=previous_name;operation_failed=previous_failure;}
}
void Log(const char* phase,Result result,u64 value){
    if(phase&&!result){
        if(!strcmp(phase,"operation-begin"))current_game=0;
        if(!strcmp(phase,"install-content-program-id")||!strcmp(phase,"install-package-title-id")||!strcmp(phase,"move-begin-M4"))current_game=value;
    }
    std::scoped_lock lock(log_mutex);
    mkdir("sdmc:/switch/nexusmanager",0777);
    constexpr const char* path="sdmc:/switch/nexusmanager/nexus.log";
    struct stat st{};if(stat(path,&st)==0&&st.st_size>1024*1024)Rotate(path,"sdmc:/switch/nexusmanager/nexus.previous.log");
    const auto now=time(nullptr);struct tm utc{};gmtime_r(&now,&utc);char timestamp[32]{};strftime(timestamp,sizeof(timestamp),"%Y-%m-%dT%H:%M:%SZ",&utc);
    char safe[193]{};for(size_t i=0;phase&&phase[i]&&i<sizeof(safe)-1;++i){const auto c=static_cast<unsigned char>(phase[i]);safe[i]=c<32||c==127?'_':char(c);}
    char line[640]{};snprintf(line,sizeof(line),"NM2 %s version=%s session=%016llx uptime_ms=%llu operation=%llu task=%s event=%s rc=0x%08X value=%llu\n",
        timestamp,updates::Version,(unsigned long long)diagnostics::Session(),(unsigned long long)(armTicksToNs(armGetSystemTick())/1000000),
        (unsigned long long)operation,operation_name,safe,(unsigned)result,(unsigned long long)value);
    if(FILE* f=fopen(path,"a")){fputs(line,f);fclose(f);}
    if(result && operation && !operation_failed){
        constexpr const char* path="sdmc:/switch/nexusmanager/operation-failures.log";
        struct stat size{};if(stat(path,&size)==0&&size.st_size>256*1024)Rotate(path,"sdmc:/switch/nexusmanager/operation-failures.previous.log");
        if(FILE* f=fopen(path,"a")){const bool ok=fputs(line,f)>=0;operation_failed=fclose(f)==0&&ok;}
    }
    if(result && !first_saved){
        constexpr const char* first="sdmc:/switch/nexusmanager/first-result.log";
        if(!first_rotated){if(!Rotate(first,"sdmc:/switch/nexusmanager/first-result.previous.log"))return;first_rotated=true;}
        if(FILE* f=fopen(first,"w")){const bool ok=fputs("First nonzero result in this Manager session; expected probe/fallback results are possible.\n",f)>=0&&fputs(line,f)>=0;first_saved=fclose(f)==0&&ok;}
    }
}
namespace diagnostics {
namespace {
constexpr Result Missing=MAKERESULT(2,1);
struct Path{char text[FS_MAX_PATH]{};explicit Path(const std::string& s){snprintf(text,sizeof(text),"%s",s.c_str());}};
struct Input{std::string path;u64 size{};};
bool ReportName(const std::string& name){return name.size()==27&&name.starts_with("repair-")&&name.ends_with(".txt")&&std::all_of(name.begin()+7,name.end()-4,[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
}
Result Export(sphaira::ui::ProgressBox* box,std::string& output){
    Operation scope("export-diagnostics");output.clear();
    if(!nexusHasSdBootstrap(envGetLoaderInfo(),envGetLoaderInfoSize())){Client c;NexusVsdStatus v{};R_TRY(c.VirtualSdStatus(v));R_UNLESS(!(v.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED)),Unavailable);}
    FsFileSystem sd{};R_TRY(fsOpenSdCardFileSystem(&sd));ON_SCOPE_EXIT(fsFsClose(&sd));
    std::vector<Input> files;std::string manifest=std::string("Nexus Manager ")+updates::Version+"\nSession: "+std::to_string(Session())+
        "\nContains selected diagnostic text only. No keys, tickets, passwords, game files or saves.\nFirmware snapshots may be from an earlier boot; check their build IDs and counters.\n";
    u64 total{};
    auto consider=[&](const std::string& name)->Result{
        Path path(name);FsFile f{};auto rc=fsFsOpenFile(&sd,path.text,FsOpenMode_Read,&f);
        if(rc==Missing){manifest+="Unavailable: "+name+"\n";return 0;}R_TRY(rc);ON_SCOPE_EXIT(fsFileClose(&f));s64 size{};R_TRY(fsFileGetSize(&f,&size));
        R_UNLESS(size>=0,Invalid);if(size>2*1024*1024 || total+size>12*1024*1024){manifest+="Skipped (size limit): "+name+"\n";return 0;}
        files.push_back({name,u64(size)});total+=size;return 0;
    };
    for(const char* name:{
        "/switch/nexusmanager/nexus.log","/switch/nexusmanager/nexus.previous.log",
        "/switch/nexusmanager/first-result.log","/switch/nexusmanager/first-result.previous.log",
        "/switch/nexusmanager/operation-failures.log","/switch/nexusmanager/operation-failures.previous.log",
        "/atmosphere/logs/usb_storage_status.txt","/atmosphere/logs/usb_storage_last_ready.txt",
        "/atmosphere/logs/usb_loader_trace.txt","/atmosphere/logs/usb_compatibility.txt",
        "/atmosphere/logs/usb_sleep_last_wake.txt","/atmosphere/logs/usb_ncm_launch_trace.txt"})R_TRY(consider(name));
    {
        Path root("/switch/nexusmanager/recovery-reports");FsDir dir{};auto rc=fsFsOpenDirectory(&sd,root.text,FsDirOpenMode_ReadFiles,&dir);
        if(rc!=Missing){R_TRY(rc);ON_SCOPE_EXIT(fsDirClose(&dir));std::vector<std::pair<u64,std::string>> reports;
            for(;;){std::array<FsDirectoryEntry,8> entries{};s64 count{};R_TRY(fsDirRead(&dir,&count,entries.size(),entries.data()));R_UNLESS(count>=0&&count<=s64(entries.size()),Invalid);if(!count)break;
                for(s64 i=0;i<count;++i){R_UNLESS(memchr(entries[i].name,0,sizeof(entries[i].name)),Invalid);std::string name=entries[i].name;if(!ReportName(name))continue;
                    Path path(std::string(root.text)+"/"+name);FsTimeStampRaw stamp{};R_TRY(fsFsGetFileTimeStampRaw(&sd,path.text,&stamp));
                    reports.emplace_back(stamp.modified,path.text);R_UNLESS(reports.size()<=4096,Invalid);}}
            std::sort(reports.rbegin(),reports.rend());if(reports.size()>8)reports.resize(8);for(const auto& r:reports)R_TRY(consider(r.second));
        }
    }
    routing_backup::Status backup{};const auto backup_result=sphaira::nexusapp::QueryConsoleRoutingBackup(backup);
    char state[200]{};snprintf(state,sizeof(state),"Console backup query: 0x%08X; sequence=%llu; flags=%u; last_result=0x%08X\n",backup_result,(unsigned long long)backup.sequence,backup.flags,backup.last_result);manifest+=state;
    sphaira::mz::MzMem memory{};zlib_filefunc64_def funcs{};sphaira::mz::FileFuncMem(&memory,&funcs);
    auto zip=zipOpen2_64("diagnostics",APPEND_STATUS_CREATE,nullptr,&funcs);R_UNLESS(zip,Invalid);bool closed=false;ON_SCOPE_EXIT(if(!closed)zipClose(zip,nullptr));
    auto begin=[&](const char* name)->Result{return zipOpenNewFileInZip(zip,name,nullptr,nullptr,0,nullptr,0,nullptr,Z_DEFLATED,Z_DEFAULT_COMPRESSION)==ZIP_OK?0:Invalid;};
    R_TRY(begin("Read me.txt"));R_UNLESS(zipWriteInFileInZip(zip,manifest.data(),manifest.size())==ZIP_OK&&zipCloseFileInZip(zip)==ZIP_OK,Invalid);
    u64 done{};std::vector<u8> buffer(65536);
    for(const auto& input:files){
        R_TRY(box->ShouldExitResult());Path path(input.path);FsFile file{};R_TRY(fsFsOpenFile(&sd,path.text,FsOpenMode_Read,&file));ON_SCOPE_EXIT(fsFileClose(&file));
        s64 size{};R_TRY(fsFileGetSize(&file,&size));R_UNLESS(size>=s64(input.size),Invalid);
        R_TRY(begin(input.path.c_str()+1));
        for(u64 off=0;off<input.size;){R_TRY(box->ShouldExitResult());const auto count=std::min<u64>(buffer.size(),input.size-off);u64 got{};
            R_TRY(fsFileRead(&file,off,buffer.data(),count,FsReadOption_None,&got));R_UNLESS(got==count,Invalid);
            R_UNLESS(zipWriteInFileInZip(zip,buffer.data(),count)==ZIP_OK,Invalid);off+=count;done+=count;box->SetOverallTransfer(done,total+65536,"Collecting diagnostic logs"_nx,true);}
        R_UNLESS(zipCloseFileInZip(zip)==ZIP_OK,Invalid);
    }
    const auto close=zipClose(zip,nullptr);closed=true;R_UNLESS(close==ZIP_OK && memory.buf.size()<=13*1024*1024,Invalid);
    for(const char* name:{"/switch","/switch/nexusmanager","/switch/nexusmanager/diagnostics"}){Path path(name);FsDirEntryType type{};auto rc=fsFsGetEntryType(&sd,path.text,&type);if(rc==Missing){R_TRY(fsFsCreateDirectory(&sd,path.text));}else {R_TRY(rc);R_UNLESS(type==FsDirEntryType_Dir,Invalid);}}
    char name[160]{};snprintf(name,sizeof(name),"/switch/nexusmanager/diagnostics/Nexus-%016llx-%016llx.zip",(unsigned long long)Session(),(unsigned long long)armGetSystemTick());
    Path temp(std::string(name)+".tmp"),final(name);R_TRY(fsFsCreateFile(&sd,temp.text,memory.buf.size(),0));FsFile file{};R_TRY(fsFsOpenFile(&sd,temp.text,FsOpenMode_Write,&file));
    auto rc=fsFileWrite(&file,0,memory.buf.data(),memory.buf.size(),FsWriteOption_Flush);fsFileClose(&file);R_TRY(rc);R_TRY(fsFsCommit(&sd));
    R_TRY(fsFsOpenFile(&sd,temp.text,FsOpenMode_Read,&file));bool reader_open=true;ON_SCOPE_EXIT(if(reader_open)fsFileClose(&file));
    for(size_t off=0;off<memory.buf.size();){const auto count=std::min(buffer.size(),memory.buf.size()-off);u64 got{};R_TRY(fsFileRead(&file,off,buffer.data(),count,FsReadOption_None,&got));R_UNLESS(got==count&&!memcmp(buffer.data(),memory.buf.data()+off,count),Invalid);off+=count;}
    fsFileClose(&file);reader_open=false;
    R_TRY(fsFsRenameFile(&sd,temp.text,final.text));R_TRY(fsFsCommit(&sd));output="SD:"+std::string(name);
    box->SetOverallTransfer(total+65536,total+65536,"Diagnostics saved"_nx,true);return 0;
}
}
}
