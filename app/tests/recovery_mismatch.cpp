#include <fstream>
using namespace sphaira::nexusapp;
constexpr u64 A=0x0100123400000000ULL,B=0x0100432100000000ULL,C=0x0100777700000000ULL;
void Add(u64 id,u8 offset,u8 type=0x80,u64 parent=0,u32 version=0){
    const size_t ext=type==0x84?sizeof(NcmDataPatchMetaExtendedHeader):type==0x80?0:24;
    Record r{{id,version,type,0,{}},{}};NcmContentMetaHeader h{static_cast<u16>(ext),3,0,0,0};
    r.data.resize(sizeof(h)+ext+3*sizeof(NcmContentInfo));memcpy(r.data.data(),&h,sizeof(h));
    if(ext)memcpy(r.data.data()+sizeof(h)+(type==0x84?8:0),&parent,sizeof(parent));
    for(u8 i=0;i<3;++i){NcmContentInfo c{};c.content_id.c[0]=offset+i+1;c.size_low=0x1000;c.content_type=i==0?0:i==1?1:3;
        memcpy(r.data.data()+sizeof(h)+ext+i*sizeof(c),&c,sizeof(c));Bytes bytes(0x1000);bytes[0]=offset+i+1;usb_disk[NcaPath(offset+i+1)]=bytes;
    }records.push_back(r);
}
nexus::repair::Indexes Live(){
    nexus::repair::Snapshot s;for(size_t i=0;i<3;++i)if(disk.contains(nexus::repair::Live(i)))s[i]={true,disk.at(nexus::repair::Live(i))};
    nexus::repair::Indexes m;assert(nexus::repair::Decode(s,m));return m;
}
std::string Saved(const RecoveryPlan& p){assert(p.report_path.starts_with("SD:"));const auto& b=disk.at(p.report_path.substr(3));return {b.begin(),b.end()};}
int main(int argc,char** argv){
    sphaira::ui::ProgressBox p;RecoveryPlan plan;
    // Bad first, middle or last file, with absent or existing folder.
    // All other files still get read; no part of the bad game is newly indexed.
    for(bool existing:{false,true})for(int bad=1;bad<=3;++bad){
        Reset();Add(A,0);Add(B,10);if(existing)dirs.insert(live);
        title_names[A]="Example game A";title_names[B]="Example game B";
        usb_disk[NcaPath(bad)][0]^=0x80;auto original_usb=usb_disk;
        assert(!ScanUsbRecovery(&p,plan) && plan.ready && writes==0);
        assert(!ApplyUsbRecovery(&p,plan) && plan.repair_complete && reads==6);
        assert(plan.repaired_records==1 && plan.blocked_records==1 && plan.mismatch_files==1 && plan.issues.size()==1);
        auto m=Live();assert(!m.titles.contains(A) && m.titles.contains(B) && m.content.size()==3);
        for(int i=1;i<=3;++i)assert(!m.content.contains(nexus::repair::Id{static_cast<u8>(i)}));
        const auto& issue=plan.issues[0];assert(issue.name=="Example game A" && issue.application_id==A && issue.record_id==A);
        assert(issue.path==NcaPath(bad) && issue.nca.size()==36 && issue.actual.size()==64 && issue.expected.size()==32);
        auto report=Saved(plan);assert(report==plan.report_text && report.find(issue.nca)!=std::string::npos && report.find(issue.path)!=std::string::npos);
        assert(report.find("Example game A")!=std::string::npos && report.find("0100123400000000")!=std::string::npos);
        if(argc>1 && !existing && bad==1){
            std::ofstream(std::string(argv[1])+"/summary.txt")<<plan.detail;
            std::ofstream(std::string(argv[1])+"/issue.txt")<<RecoveryIssuePage(plan,0);
        }
        assert(usb_disk==original_usb && handles==0);
    }
    // One failing update/DLC/data patch excludes the base and all other planned
    // records in that family. No title-ID guessing; data-patch parent is at +8.
    for(u8 bad_type:{0x80,0x81,0x82,0x84}){
        Reset();Add(A,0);Add(A+0x800,10,0x81,A);Add(A+0x1001,20,0x82,A);Add(A+0x1801,30,0x84,A);Add(B,40);
        int offset=bad_type==0x80?0:bad_type==0x81?10:bad_type==0x82?20:30;
        usb_disk[NcaPath(offset+2)][0]^=0x80;
        assert(!ScanUsbRecovery(&p,plan) && !ApplyUsbRecovery(&p,plan));
        assert(plan.blocked_records==4 && plan.repaired_records==1 && plan.issues.size()==1 && plan.issues[0].application_id==A);
        auto m=Live();assert(m.titles.size()==1 && m.titles.contains(B) && m.content.size()==3 && reads==15);
    }
    // Shared bad content reports BOTH owners, without duplicate USB reads.
    Reset();Add(A,0);Add(B,0);Add(C,10);usb_disk[NcaPath(1)][0]^=0x80;
    assert(!ScanUsbRecovery(&p,plan) && !ApplyUsbRecovery(&p,plan));
    assert(plan.issues.size()==2 && plan.mismatch_files==1 && plan.blocked_records==2 && plan.repaired_records==1 && reads==6);
    assert(plan.issues[0].record_id==A && plan.issues[1].record_id==B && Live().titles.contains(C));
    // Several failures in a single game are ALL reported. All-bad is a clean
    // no-op for routing, including absent folder and existing alias/settings.
    for(bool existing:{false,true}){
        Reset();Add(A,0);if(existing){dirs.insert(live);nexus::repair::Indexes old;old.titles[A]=0xabcdef;auto s=nexus::repair::Encode(old);for(int i=0;i<3;++i)disk[nexus::repair::Live(i)]=s[i].data;disk[std::string(live)+"/user-setting"]={1,2,3};}
        auto before=disk;fault=18;
        assert(!ScanUsbRecovery(&p,plan) && !ApplyUsbRecovery(&p,plan));
        assert(!plan.published && !plan.repair_complete && plan.repaired_records==0 && plan.issues.size()==3 && reads==3);
        for(const auto& [path,bytes]:before)assert(disk.at(path)==bytes);
        for(const auto& [path,bytes]:disk)if(path.starts_with(std::string(live)+"/"))assert(before.contains(path));
        assert(!Saved(plan).empty() && handles==0);
    }
    // Original partial entries/aliases are preserved; a mismatch does not
    // delete or disable a game which was already routed.
    Reset();Add(A,0);Add(B,10);dirs.insert(live);
    nexus::repair::Indexes old;old.titles[A]=0xfedcba;old.content.insert(nexus::repair::Id{2});auto s=nexus::repair::Encode(old);
    for(int i=0;i<3;++i)disk[nexus::repair::Live(i)]=s[i].data;
    usb_disk[NcaPath(2)][0]^=0x80;assert(!ScanUsbRecovery(&p,plan) && !ApplyUsbRecovery(&p,plan));
    assert(Live().titles.at(A)==0xfedcba && Live().content.contains(nexus::repair::Id{2}) && !Live().mappings.contains(A));
    // All good is unchanged; re-run is a no-op and does not rehash intact rows.
    Reset();Add(A,0);Add(B,10);assert(!ScanUsbRecovery(&p,plan) && !ApplyUsbRecovery(&p,plan));
    assert(plan.repaired_records==2 && plan.issues.empty() && plan.report_text.find("No mismatches observed")!=std::string::npos);
    auto before=disk;assert(!ScanUsbRecovery(&p,plan) && !plan.ready && plan.intact==2 && disk==before);
    // Cancellation, disconnect and short read after a mismatch are fatal, never
    // publish partially verified good records. The partial report survives.
    for(int failure=0;failure<3;++failure){
        Reset();Add(A,0);Add(B,10);usb_disk[NcaPath(1)][0]^=0x80;
        assert(!ScanUsbRecovery(&p,plan));
        if(failure==0)cancel_after=2;else if(failure==1)disconnect_after=2;else short_after=2;
        assert(R_FAILED(ApplyUsbRecovery(&p,plan)) && !plan.published && !plan.verification_complete && !plan.repaired_records);
        assert(plan.issues.size()==1 && Saved(plan).find("NO - interrupted")!=std::string::npos);
        for(int i=0;i<3;++i)assert(!disk.contains(nexus::repair::Live(i)));
        assert(handles==0);
    }
    // Report storage failure cannot mislabel completed repairs as failed, and
    // leaves all on-screen issue details available for photographs.
    Reset();Add(A,0);Add(B,10);usb_disk[NcaPath(1)][0]^=0x80;report_fault=true;
    assert(!ScanUsbRecovery(&p,plan) && !ApplyUsbRecovery(&p,plan) && plan.repair_complete);
    assert(plan.report_path.empty() && plan.issues.size()==1 && plan.detail.find("could not be saved")!=std::string::npos);
    assert(RecoveryIssuePage(plan,0).find(plan.issues[0].nca)!=std::string::npos);
    // Unknown names/parents are explicit, control bytes sanitized, and names
    // remain untruncated in the saved report (bounded only in screen labels).
    Reset();Add(A,0);Add(A+0x800,10,0x81,0);fault=18;title_names[A]=std::string(140,'x')+"\r\nINJECT";
    assert(!ScanUsbRecovery(&p,plan) && !ApplyUsbRecovery(&p,plan));
    assert(plan.issues[0].name.find('\n')==std::string::npos && plan.report_text.find(std::string(140,'x'))!=std::string::npos);
    assert(plan.report_text.find("unavailable - not guessed")!=std::string::npos);
    assert(RecoveryIssuePage(plan,0).find("...")!=std::string::npos && RecoveryIssuePage(plan,plan.issues.size()).find("No mismatch")!=std::string::npos);
    if(argc>1)std::ofstream(std::string(argv[1])+"/long-issue.txt")<<RecoveryIssuePage(plan,0);
    // Unknown parent with truncated data-patch extended header must not read
    // an out-of-bounds application ID or borrow the DLC ID as the game ID.
    Reset();Add(A,0,0x81,0);records[0].key.type=0x84;fault=18;dirs.insert(live);
    assert(!ScanUsbRecovery(&p,plan) && !ApplyUsbRecovery(&p,plan));assert(plan.issues[0].application_id==0);
    // Delete/reinstall is never blocked by a previous mismatch. Retain the old
    // diagnostic files, reuse the SAME plan, app ID and content IDs, and prove
    // scan resets every transient failure before verifying the new bytes.
    for(bool existing:{false,true}){
        Reset();Add(A,0);Add(B,10);if(existing)dirs.insert(live);usb_disk[NcaPath(2)][0]^=0x80;
        assert(!ScanUsbRecovery(&p,plan) && !ApplyUsbRecovery(&p,plan) && plan.blocked_records==1);
        const auto old_report=plan.report_path.substr(3);const auto old_bytes=disk.at(old_report);
        auto good_index=Live();
        records.erase(records.begin());for(int i=1;i<=3;++i)usb_disk.erase(NcaPath(i)); // simulate user uninstall
        assert(!ScanUsbRecovery(&p,plan) && !plan.ready && plan.issues.empty() && !plan.mismatch_files);
        Add(A,0); // simulate reinstall of the same title with intact bytes
        assert(!ScanUsbRecovery(&p,plan) && plan.ready && plan.issues.empty() && !plan.blocked_records && !plan.published);
        assert(!ApplyUsbRecovery(&p,plan) && plan.repair_complete && plan.repaired_records==1 && plan.issues.empty());
        assert(Live().titles.contains(A) && Live().titles.contains(B) && Live().content.size()==6);
        assert(Live().mappings.at(B)==good_index.mappings.at(B) && disk.at(old_report)==old_bytes);
        assert(Saved(plan).find("Unique mismatched NCAs: 0")!=std::string::npos);
    }
    std::cout<<"PASS: R3 per-game filtering, shared ownership, all mismatch positions, all-bad no-op, base/update/DLC/data-patch families, aliases, complete reports, cache-name fallback, interruptions, report-write failure and same-ID delete/reinstall recovery\n";
}
