// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
using Result=unsigned;using u32=std::uint32_t;using u64=std::uint64_t;
extern "C" {u32 __nx_applet_exit_mode=1;}
constexpr int AppletType_Application=0,AppletType_LibraryApplet=2,AppletType_SystemApplication=4;
constexpr int InfoType_ProgramId=18,CUR_PROCESS_HANDLE=42;
#define R_UNLESS(condition,result) do{if(!(condition))return result;}while(false)
#define R_TRY(expression) do{const auto restart_test_result=(expression);if(restart_test_result)return restart_test_result;}while(false)
#define R_SUCCEED() return 0
namespace nexus {constexpr Result Invalid=55,Unavailable=56;}
namespace mock {
bool native{},exited{};int applet=AppletType_Application;
u64 program_id=0x05004e584d470000ULL;
Result info_result{},launch_result{};
std::string executable="/switch/nexusmanager/NexusManager.nro";
std::vector<std::string> events;
}
bool envIsNso(){return mock::native;}
int appletGetAppletType(){return mock::applet;}
Result svcGetInfo(u64* out,int type,int handle,u64 index){
    assert(mock::native&&type==InfoType_ProgramId&&handle==CUR_PROCESS_HANDLE&&!index);
    mock::events.push_back("identity");*out=mock::program_id;return mock::info_result;
}
Result appletRequestLaunchApplication(u64 id,void* storage){
    assert(mock::native&&!id&&!storage&&__nx_applet_exit_mode==1);mock::events.push_back("native-launch");return mock::launch_result;
}
namespace sphaira {
struct App {
    struct Path {const char* s;};
    static Path GetExePath(){assert(!mock::native);return {mock::executable.c_str()};}
    static void Exit(){assert(!mock::exited&&__nx_applet_exit_mode==(mock::native?1U:0U));mock::exited=true;mock::events.push_back("exit");}
};
Result nro_launch(std::string path){
    assert(!mock::native&&path=="/switch/nexusmanager/NexusManager.nro"&&__nx_applet_exit_mode==1);
    mock::events.push_back("nro-launch");return mock::launch_result;
}
}
// RESTART_PRODUCTION
int main(int argc,char** argv){
    assert(argc==2);const std::string scenario=argv[1];mock::native=scenario.starts_with("native");
    if(scenario.ends_with("launch-failure"))mock::launch_result=77;
    if(scenario=="native-wrong-id")mock::program_id=0x0100123400000000ULL;
    if(scenario=="native-info-failure")mock::info_result=78;
    if(scenario.ends_with("-applet"))mock::applet=AppletType_LibraryApplet;
    if(scenario=="native-system-app")mock::applet=AppletType_SystemApplication;
    if(scenario=="nro-wrong-path")mock::executable="/switch/other/Other.nro";
    const std::filesystem::path file="sdmc:/switch/nexusmanager/NexusManager.nro";
    std::filesystem::create_directories(file.parent_path());
    if(scenario=="nro-directory")std::filesystem::create_directory(file);
    else if(scenario!="nro-missing"){
        std::string bytes(scenario=="nro-short"?8:0x20,'\0');
        if(bytes.size()>0x14&&scenario!="nro-corrupt")bytes.replace(0x10,4,"NRO0");
        std::ofstream output(file,std::ios::binary);output.write(bytes.data(),bytes.size());
    }
    const bool expected=scenario=="native"||scenario=="nro";
    std::string detail="old detail";
    const auto result=nexus::updates::RestartUpdatedManager(detail);
    assert((result==0)==expected&&mock::exited==expected);
    // Failure preserves exit mode, NRO success resets it before Exit, and
    // native relaunch never changes it.
    assert(__nx_applet_exit_mode==(!mock::native&&expected?0U:1U));
    if(expected){
        const std::vector<std::string> events=mock::native?
            std::vector<std::string>{"identity","native-launch","exit"}:std::vector<std::string>{"nro-launch","exit"};
        assert(mock::events==events&&detail.find("Restarting")!=std::string::npos);
    }else{
        assert(detail.find("update is installed")!=std::string::npos&&detail.find("Close and reopen")!=std::string::npos);
        if(mock::launch_result)assert(result==mock::launch_result);
        else if(mock::info_result)assert(result==mock::info_result);
        for(const auto& event:mock::events){assert(event!="exit");if(!mock::launch_result)assert(event!="native-launch"&&event!="nro-launch");}
    }
    std::cout<<scenario<<" passed\n";
}
