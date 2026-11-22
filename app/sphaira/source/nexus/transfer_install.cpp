#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/transfer.hpp"
#include "nexus/transfer_activity.hpp"
#include "nexus/manager.hpp"
#include "nexus/lifecycle.hpp"
#include "app.hpp"
#include "ui/option_box.hpp"
#include "yati/yati.hpp"
#include "yati/source/stream.hpp"
#include <atomic>
namespace nexus::transfer {
namespace {
std::atomic<sphaira::ui::ProgressBox*> progress{};
class FtpSource final:public sphaira::yati::source::Stream {
    std::shared_ptr<FtpStream> pipe;sphaira::ui::ProgressBox* box;
    Result Check(){R_TRY(box->ShouldExitResult());R_UNLESS(lifecycle::CanPoll(),FtpStream::Aborted);return 0;}
public:
    FtpSource(std::shared_ptr<FtpStream> p,sphaira::ui::ProgressBox* b):pipe(std::move(p)),box(b){}
    void SignalCancel()override{pipe->Cancel();}
    Result ReadChunk(void* data,s64 size,u64* got)override{
        R_UNLESS(size>=0&&got,FtpStream::Invalid);
        const auto rc=Check();if(R_FAILED(rc)){pipe->Cancel(rc);return rc;}
        return pipe->Read(data,size,*got,[this]{return Check();});
    }
    Result FinishRead(s64 minimum)override{
        R_UNLESS(minimum>=0&&u64(minimum)<=FtpStream::Limit,FtpStream::Invalid);
        box->NewTransfer("Finishing incoming package"_nx);
        std::vector<u8> scratch(256*1024);u64 got{};
        // Skipped files and cartridge padding are still consumed. The reader
        // never rewinds and publication waits for a clean upload EOF.
        do {R_TRY(ReadChunk(scratch.data(),scratch.size(),&got));}while(got);
        R_UNLESS(pipe->Consumed()>=u64(minimum),FtpStream::Invalid);return 0;
    }
};
Result InstallStream(sphaira::ui::ProgressBox* p,const InstallJob& job,nexus::install::Diagnostic& diagnostic){
    FtpSource source(job.stream,p);sphaira::yati::ConfigOverride config{};
    config.nexus_target=job.usb?Target::Usb:Target::Sd;
    config.nexus_destination_generation=job.usb?job.generation:0;config.diagnostic=&diagnostic;
    config.quick_usb_final_verify=job.usb;
    config.ticket_only=false;config.skip_base=false;config.skip_patch=false;config.skip_addon=false;config.skip_data_patch=false;config.skip_ticket=false;
    config.skip_nca_hash_verify=false;config.convert_to_standard_crypto=false;config.lower_master_key=false;config.lower_system_version=false;config.ignore_distribution_bit=false;
    p->SetProgressNote("Streaming from PC. Keep the connection and storage connected."_nx);
    return sphaira::yati::InstallFromSource(p,&source,fs::FsPath(job.path),config,-1);
}
}
// UI-thread poll and destruction: never dereference a worker-owned/destroyed box.
void PollInstall(){if(!lifecycle::CanPoll())if(auto* p=progress.load())p->RequestExit();}
void ShowNextInstall(){
    InstallJob job;if(!TakeInstall(job))return;
    auto cleanup_warning=std::make_shared<bool>(false);
    sphaira::App::Push<sphaira::ui::ProgressBox>(0,job.usb?"Installing to USB"_nx:"Installing to SD"_nx,job.path,[job,cleanup_warning](sphaira::ui::ProgressBox* p)->Result{
        progress=p;struct Clear{~Clear(){progress=nullptr;}} clear;
        nexus::install::Diagnostic diagnostic{};sphaira::yati::InstallOutcome outcome{};
        const auto rc=[&]()->Result{
            R_TRY(p->ShouldExitResult());
            if(job.stream)return InstallStream(p,job,diagnostic);
            return sphaira::nexusapp::Install(p,job.usb,job.generation,job.path,job.usb?Target::Usb:Target::Sd,job.generation,&diagnostic,&outcome);
        }();
        // Release the FTP callback from the WORKER, never the UI completion
        // callback: Stop() can be joining that FTP worker on the UI thread.
        if(job.stream)job.stream->Complete(rc);
        if(!job.stream&&InstalledEntirePackage(rc,outcome.installed_records,outcome.skipped_records)){
            p->NewTransfer("Removing temporary install package"_nx);
            const auto removed=R_FAILED(p->ShouldExitResult())?FtpStream::Aborted:RemoveInstalledUpload(job);
            *cleanup_warning=R_FAILED(removed);
            nexus::Log("pc-install-temp-cleanup",removed,job.size);
            if(R_SUCCEEDED(removed))activity::Get().Event("Temporary install package removed: "_nx+activity::Name(job.path));
            else activity::Get().Failure("Installed, but temporary package cleanup could not be confirmed"_nx,removed);
        }else if(!job.stream){activity::Get().Event("Temporary package kept: installation failed, stopped or skipped content."_nx);}
        nexus::Log("pc-transfer-install",rc,job.size);return rc;
    },[streaming=bool(job.stream),cleanup_warning](Result rc){
        FinishInstall(rc);
        if(*cleanup_warning)sphaira::App::Notify("Game installed. Temporary package cleanup could not be confirmed; check transfers/install."_nx);
        if(R_FAILED(rc))sphaira::App::PushErrorBox(rc,streaming?"Stream install stopped. Disconnect and restart PC transfer, then resend the original package from your PC from the beginning. Some complete content may already be present; the next attempt verifies it before reuse. No full package copy was saved on the Switch."_nx:"Installation paused. The uploaded package is kept in transfers/install. Check space, keys and the connection, then retry from Install games. An upload completing on your PC does not mean the game installed successfully."_nx);
    },sphaira::ui::ProgressBoxOption::TransferLog);
}
}
