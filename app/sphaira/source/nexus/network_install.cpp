#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/network_install.hpp"
#include "nexus/network_source.hpp"
#include "nexus/nx_Client.hpp"
#include "nexus/lifecycle.hpp"
#include "nexus/transfer.hpp"
#include "nexus/package.hpp"
#include "utils/devoptab.hpp"
#include "yati/yati.hpp"
#include "app.hpp"
#include <atomic>
#include <mutex>
#include <sys/stat.h>

namespace sphaira::nexusapp {
namespace {
constexpr Result NetworkIo=MAKERESULT(Module_Libnx,LibnxError_IoError);
constexpr Result SourceChanged=MAKERESULT(505,903);
// Backends still clamp this to the NAS's negotiated read limit. Keep requests
// bounded for cancellation, without forcing four serialized reads per MiB.
constexpr size_t ReadChunk=1024*1024;
bool SessionMount(const std::string& mount){
    constexpr std::string_view prefix="nexusnet",suffix=":/";
    if(!mount.starts_with(prefix)||!mount.ends_with(suffix)||mount.size()<=prefix.size()+suffix.size()||mount.size()>32)return false;
    const auto number=std::string_view(mount).substr(prefix.size(),mount.size()-prefix.size()-suffix.size());
    return number.front()!='0'&&std::all_of(number.begin(),number.end(),[](char c){return c>='0'&&c<='9';});
}
class NetworkPackageSource final:public yati::source::Base {
    ui::ProgressBox* progress;const std::string mount,path,full_path;const u64 capacity,usb_generation;
    std::atomic_bool cancelled{};Result last_check{};struct stat original{};
    std::mutex read_lock;
    // All checks run under read_lock after construction. Reuse one reader IPC
    // session while still checking the exact destination generation each time.
    // The writer owns a separate session and can continue concurrently.
    nexus::Client usb;
    nexus::network::Source source;
    bool open{};
    Result Check(){
        if(cancelled.load())return nexus::Unavailable;
        R_TRY(progress->ShouldExitResult());
        R_UNLESS(nexus::lifecycle::CanPoll()&&nexus::transfer::Active()==nexus::transfer::Mode::Off,nexus::Unavailable);
        location::StdioEntries devices;R_TRY(devoptab::GetNetworkDevices(devices));
        const auto entry=std::find_if(devices.begin(),devices.end(),[&](const auto& e){return e.mount==mount;});
        R_UNLESS(entry!=devices.end()&&(entry->flags&location::FsEntryFlag::FsEntryFlag_ReadOnly)&&
            !(entry->flags&location::FsEntryFlag::FsEntryFlag_NoStatFile),nexus::Unavailable);
        if(usb_generation){R_TRY(usb.Check(usb_generation));}
        return 0;
    }
    Result Snapshot(bool initial){
        struct stat now{};
        if(lstat(full_path.c_str(),&now)){problem="The NAS file could not be read. Check the network connection and reconnect the share."_nx;return NetworkIo;}
        if(!S_ISREG(now.st_mode)||now.st_size<0||static_cast<u64>(now.st_size)!=capacity){problem="The selected game file is missing, is not an ordinary file, or its size changed. Browse the NAS again before installing."_nx;return SourceChanged;}
        if(initial)original=now;
        else if(now.st_size!=original.st_size||now.st_dev!=original.st_dev||now.st_ino!=original.st_ino||now.st_mtime!=original.st_mtime||now.st_ctime!=original.st_ctime){
            problem="The game file on the NAS changed while it was being read. Keep the source unchanged and select it again."_nx;return SourceChanged;
        }
        if(!initial){
            struct stat opened{};const auto error=source.StatOpen(opened);
            if(error){if(R_FAILED(last_check))return last_check;problem="The opened NAS file could no longer be checked. Reconnect the share and select the game file again."_nx;return NetworkIo;}
            if(!S_ISREG(opened.st_mode)||opened.st_size!=original.st_size||opened.st_dev!=original.st_dev||opened.st_ino!=original.st_ino||
                opened.st_mtime!=original.st_mtime||opened.st_ctime!=original.st_ctime){
                problem="The opened game file differs from the file selected on the NAS. Select the unchanged original game file again."_nx;return SourceChanged;
            }
        }
        return 0;
    }
public:
    std::string problem;
    NetworkPackageSource(ui::ProgressBox* p,const std::string& m,const std::string& f,u64 size,u64 generation)
        :progress(p),mount(m),path(f),full_path(m+f.substr(1)),capacity(size),usb_generation(generation),
         source(m,[this]{last_check=Check();return R_SUCCEEDED(last_check);}){
        progress->BeginNetworkTransfer();
        m_open_result=Check();if(R_SUCCEEDED(m_open_result))m_open_result=Snapshot(true);
        if(R_SUCCEEDED(m_open_result)){
            const auto error=source.BeginRead(path);
            if(error)m_open_result=R_FAILED(last_check)?last_check:NetworkIo;
            else m_open_result=Snapshot(false);
        }
        open=R_SUCCEEDED(m_open_result);
    }
    ~NetworkPackageSource() override {progress->EndNetworkTransfer();}
    void SignalCancel() override {cancelled.store(true);}
    Result Read(void* data,s64 offset,s64 requested,u64* actual) override {
        std::scoped_lock lock(read_lock);
        if(actual)*actual=0;
        R_UNLESS(actual&&open,nexus::Invalid);
        R_UNLESS(offset>=0&&requested>=0&&static_cast<u64>(offset)<=capacity&&
            static_cast<u64>(requested)<=capacity-static_cast<u64>(offset)&&(!requested||data),nexus::Invalid);
        R_TRY(Check());if(!requested)return 0;R_TRY(Snapshot(false));
        size_t done{};
        while(done<static_cast<u64>(requested)){
            R_TRY(Check());const auto count=std::min<u64>(ReadChunk,static_cast<u64>(requested)-done);u64 got{};
            const auto error=source.Read(path,static_cast<u64>(offset)+done,static_cast<u8*>(data)+done,count,got);
            // Count received payload even if a later cancellation or source
            // validation fails. Never count an impossible oversized reply.
            if(got&&got<=count)progress->AddNetworkBytes(got);
            if(error){
                if(R_FAILED(last_check))return last_check;
                problem="Reading the game file from the NAS stopped. Check the network connection and reconnect the share."_nx;return NetworkIo;
            }
            R_TRY(Check());
            if(!got||got>count){problem="The NAS returned an unexpected number of game-file bytes. Check that the source file is complete and unchanged."_nx;return NetworkIo;}
            done+=got;
        }
        R_TRY(Snapshot(false));R_TRY(Check());*actual=done;return 0;
    }
};
}
Result InstallNetworkPackage(ui::ProgressBox* progress,const std::string& mount,const std::string& path,
        u64 expected_size,nexus::Target target,u64 destination_generation,nexus::install::Diagnostic& diagnostic,
        NetworkInstallOutcome* package_outcome,bool reuse_existing_usb_only){
    if(package_outcome)*package_outcome={};
    diagnostic.Clear();std::string stage="Network install could not start."_nx;bool attempted{};yati::InstallOutcome outcome{};
    const Result result=[&]()->Result{
        R_UNLESS(!reuse_existing_usb_only || target==nexus::Target::Usb,nexus::Invalid);
        R_UNLESS(progress&&SessionMount(mount)&&nexus::files::Canonical(path)&&
            nexus::package::Supported(path)&&expected_size>=16&&expected_size<=nexus::files::MaxBytes&&
            (target==nexus::Target::Sd||target==nexus::Target::Usb)&&
            (target==nexus::Target::Usb?destination_generation!=0:destination_generation==0),nexus::Invalid);
        stage="Open Nexus Manager in full-memory mode or use its HOME app before installing from a NAS."_nx;
        R_UNLESS(!App::IsApplet(),nexus::Unavailable);R_TRY(progress->ShouldExitResult());
        stage="Finish other transfers, restore any pending recovery, and keep the physical SD card connected before installing."_nx;
        R_UNLESS(nexus::lifecycle::CanPoll()&&nexus::transfer::Active()==nexus::transfer::Mode::Off,nexus::Unavailable);
        R_TRY(nexus::CheckUsbMutationAllowed());nexus::Space space{};R_TRY(nexus::QueryPhysicalSd(space));
        stage="The selected installation destination is unavailable. Reconnect it and choose the destination again."_nx;
        if(target==nexus::Target::Usb){nexus::Client usb;R_TRY(usb.Check(destination_generation));}
        R_TRY(nexus::QueryDestination(target,space));
        stage="The selected NAS session or game file is no longer available. Reconnect the share and select the file again."_nx;
        NetworkPackageSource source(progress,mount,path,expected_size,destination_generation);
        if(R_FAILED(source.GetOpenResult())){if(!source.problem.empty())stage=source.problem;return source.GetOpenResult();}
        progress->SetProgressNote("Reading from NAS. Keep the network and storage connected."_nx);
        yati::ConfigOverride config{};config.nexus_target=target;config.nexus_destination_generation=destination_generation;config.diagnostic=&diagnostic;
        config.reuse_existing_usb_only=reuse_existing_usb_only;
        config.quick_usb_final_verify=target==nexus::Target::Usb;
        config.outcome=&outcome;
        // This action promises a full game/update/DLC install. Legacy saved
        // installer filters must not turn it into ticket-only or a no-op.
        config.ticket_only=false;config.skip_base=false;config.skip_patch=false;
        config.skip_addon=false;config.skip_data_patch=false;config.skip_ticket=false;
        config.skip_nca_hash_verify=false;config.convert_to_standard_crypto=false;config.lower_master_key=false;config.lower_system_version=false;config.ignore_distribution_bit=false;
        // A local InstallReceipt cannot represent a NAS source. Never encode
        // this path as an SD source or store credentials for automatic resume.
        config.receipt=nullptr;attempted=true;
        const auto installed=yati::InstallFromSource(progress,&source,fs::FsPath(path),config,expected_size);
        if(R_FAILED(installed)&&!source.problem.empty()){
            if(!diagnostic.text.empty())diagnostic.text+='\n';
            diagnostic.text+=source.problem;
        }
        return installed;
    }();
    if(package_outcome)*package_outcome={outcome.installed_records,outcome.skipped_records,attempted};
    if(R_FAILED(result)){
        if(diagnostic.text.empty())diagnostic.text=stage;
        if(attempted)diagnostic.text+="\nInstallation stopped; some content may already be installed. The original game file is still on the NAS."_nx;
        else diagnostic.text+="\nNo installation was started. The original game file is still on the NAS."_nx;
        diagnostic.text+="\nNetwork installs do not resume automatically. Reconnect this share and select the same game file again to retry. Existing USB content is verified before reuse."_nx;
        if(!diagnostic.completion.empty())diagnostic.text+='\n'+diagnostic.completion;
    }else{
        if(!diagnostic.completion.empty())diagnostic.completion+='\n';
        if(reuse_existing_usb_only){
            diagnostic.completion+="\nLinked entries: "_nx+std::to_string(outcome.installed_records)+"; skipped: "_nx+std::to_string(outcome.skipped_records);
        }else if(outcome.installed_records){
            diagnostic.completion+="Installed "_nx+std::to_string(outcome.installed_records)+" game/update/DLC entries directly from the NAS to the "_nx+(target==nexus::Target::Sd?"SD card."_nx:"USB drive."_nx);
            if(outcome.skipped_records)diagnostic.completion+=" Skipped "_nx+std::to_string(outcome.skipped_records)+" entries after installation checks; current installed versions were kept for those entries."_nx;
        }else{
            diagnostic.completion=outcome.skipped_records?
                "No content was installed. "_nx+std::to_string(outcome.skipped_records)+" entries were skipped by installation checks. Current installed versions were kept."_nx:
                "No content was installed. No installable game/update/DLC entries were found."_nx;
        }
        diagnostic.completion+=" The original game file was kept; no full package copy was staged on SD or USB."_nx;
    }
    return result;
}
Result InstallNetworkNsp(ui::ProgressBox* progress,const std::string& mount,const std::string& path,
        u64 expected_size,nexus::Target target,u64 destination_generation,nexus::install::Diagnostic& diagnostic){
    return InstallNetworkPackage(progress,mount,path,expected_size,target,destination_generation,diagnostic);
}
}
