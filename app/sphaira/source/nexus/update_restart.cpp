#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/update_restart.hpp"
#include "nexus/nx_Client.hpp"
#include "app.hpp"
#include "nro.hpp"
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>

extern "C" u32 __nx_applet_exit_mode;

namespace nexus::updates {
Result RestartUpdatedManager(std::string& detail){
    detail="The update is installed, but Nexus Manager could not restart. Close and reopen Nexus Manager to use the update."_nx;
    // An Album or system-applet host is not the installed Manager application.
    R_UNLESS(appletGetAppletType()==AppletType_Application,Invalid);
    if(envIsNso()){
        u64 program_id{};
        R_TRY(svcGetInfo(&program_id,InfoType_ProgramId,CUR_PROCESS_HANDLE,0));
        R_UNLESS(program_id==0x05004e584d470000ULL,Invalid);
        R_TRY(appletRequestLaunchApplication(0,nullptr));
        // Preserve the normal libnx NSO exit mode. Main performs service and
        // filesystem cleanup before appletExit sends SelfExit for this process.
    }else{
        constexpr const char* path="/switch/nexusmanager/NexusManager.nro";
        R_UNLESS(!strcasecmp(sphaira::App::GetExePath().s,path),Invalid);
        constexpr const char* device_path="sdmc:/switch/nexusmanager/NexusManager.nro";
        struct stat info{};
        unsigned char header[0x20]{};
        R_UNLESS(stat(device_path,&info)==0 && S_ISREG(info.st_mode) &&
            info.st_size>=static_cast<decltype(info.st_size)>(sizeof(header)),Unavailable);
        FILE* file=std::fopen(device_path,"rb");
        R_UNLESS(file,Unavailable);
        const auto read=std::fread(header,1,sizeof(header),file);
        const bool failed=std::ferror(file)!=0;
        const auto closed=std::fclose(file);
        R_UNLESS(read==sizeof(header) && !failed && !closed,Unavailable);
        R_UNLESS(!std::memcmp(header+0x10,"NRO0",4),Invalid);
        // nro_launch checks envSetNextLoad and queues the normal launch event.
        // Its result must succeed before the current application is exited.
        R_TRY(sphaira::nro_launch(path));
        // Exiting now bypasses App::Loop's queued LaunchNroEventData handler.
        // Match its exit mode so libnx returns to hbloader's requested NRO
        // instead of issuing SelfExit for the host application.
        __nx_applet_exit_mode=0;
    }
    detail="The update is installed. Restarting Nexus Manager..."_nx;
    sphaira::App::Exit();
    R_SUCCEED();
}
}
