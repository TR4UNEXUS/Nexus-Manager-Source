#include "nexus/localize.hpp"
#include <switch.h>
#include <memory>
#include "app.hpp"
#include "nexus/transfer.hpp"
#include "log.hpp"
#include "owo.hpp"
#include "ui/menus/main_menu.hpp"
#include "utils/core.hpp"
#include "nexus/startup_trace.hpp"
#include "nexus/watchdog.h"
#include "nexus/lifecycle.hpp"
#include "nexus/runtime_mode.hpp"
#include "nexus/update.hpp"

int main(int argc, char** argv) {
    nexus::startup::Event("main-entered");
    const char* executable = nexus::runtime::Executable(envIsNso(), argc, argv);
    if (!executable) {
        return 1;
    }
    nexus::startup::Event(envIsNso()?"runtime-native-application":"runtime-nro");
    u64 memory{};
    if(R_SUCCEEDED(svcGetInfo(&memory,InfoType_TotalMemorySize,CUR_PROCESS_HANDLE,0)))
        nexus::startup::Event("application-memory-budget","bytes",0,memory);

    if (R_FAILED(nexus::startup::Call("main-affinity", []{ return sphaira::utils::SetCurrentThreadSafeAffinity(); }))) {
        return 1;
    }

    nexus::startup::Event("app-constructor", "begin");
    auto app = std::make_unique<sphaira::App>(executable);
    nexus::startup::Event("app-constructor", "end");
    if (const u64 failure = nexus::startup::FailureInfo()) {
        char text[96]{};
        snprintf(text, sizeof(text), "Startup log unavailable: step %u, code 0x%08X"_nx,
            static_cast<unsigned>(failure >> 32), static_cast<unsigned>(failure));
        sphaira::App::Notify(text);
    }
    nexus::startup::Event("main-menu-constructor", "begin");
    app->Push<sphaira::ui::menu::main::MainMenu>();
    nexus::startup::Event("main-menu-constructor", "end");
    nexus::startup::Event("app-loop", "begin");
    nexusWatchdogDisarm(); // no automatic timeout while the user can start writes
    app->Loop();
    nexus::updates::StopStartupCheck();
    nexus::transfer::Stop();
    nexus::lifecycle::BeginShutdown();
    nexus::startup::BeginShutdown();
    nexusWatchdogArm(30ULL*1000*1000*1000); // protected operations inhibit exit
    nexus::startup::Event("app-loop", "end");
    nexus::startup::Event("app-destructor", "begin");
    app.reset();
    nexus::startup::Event("app-destructor", "end");
    return 0;
}

extern "C" {

void userAppInit(void) {
    if (R_FAILED(nexusWatchdogStart(30ULL*1000*1000*1000))) svcExitProcess();
    nexus::startup::Open();
    nexus::startup::Event("boost-mode", "begin");
    sphaira::App::SetBoostMode(true);
    nexus::startup::Event("boost-mode", "end");

    Result rc;
    if (R_FAILED(rc = nexus::startup::Call("applet-lock-exit", []{ return appletLockExit(); })))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = nexus::startup::Call("pl-initialize", []{ return plInitialize(PlServiceType_User); })))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = nexus::startup::Call("set-initialize", []{ return setInitialize(); })))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = nexus::startup::Call("hidsys-initialize", []{ return hidsysInitialize(); })))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = nexus::startup::Call("user-ncm-initialize", []{ return ncmInitialize(); })))
        diagAbortWithResult(rc);

    // it doesn't matter if this fails.
    nexus::startup::Call("screenshot-permission", []{ return appletSetScreenShotPermission(AppletScreenShotPermission_Enable); });
    nexus::startup::Event("user-app-init-complete");


}

void userAppExit(void) {
    nexus::lifecycle::BeginShutdown();
    nexus::startup::BeginShutdown();
    nexus::startup::Event("user-app-exit", "begin");

    nexus::startup::CallVoid("exit-ncm-close", [] { ncmExit(); });
    nexus::startup::CallVoid("exit-hidsys-close", [] { hidsysExit(); });
    nexus::startup::CallVoid("exit-set-close", [] { setExit(); });
    nexus::startup::CallVoid("exit-pl-close", [] { plExit(); });

    // NOTE (DMC): prevents exfat corruption.
    if (auto fs = fsdevGetDeviceFileSystem("sdmc:")) {
        // Never terminate a possible filesystem commit on the read watchdog.
        nexusWatchdogProtect();
        nexus::startup::Call("exit-sd-commit", [fs] { return fsFsCommit(fs); });
        nexusWatchdogUnprotect();
    }

    nexus::startup::CallVoid("exit-boost-reset", [] { sphaira::App::SetBoostMode(false, true); });
    // UnlockExit may terminate the process immediately when HOME requested
    // exit. Close diagnostic files BEFORE it, not after an unreachable marker.
    // This marker means cleanup finished, never proof that the process exited.
    nexus::startup::Event("exit-unlock-ready", "point");
    nexus::startup::Close();
    appletUnlockExit();
    nexusWatchdogStop(); // finish before returning/unmapping this NRO

    if (sphaira::core_launch_pending()) {
        svcExitProcess();
    }
}

} // extern "C"
