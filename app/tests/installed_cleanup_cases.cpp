// SPDX-License-Identifier: GPL-3.0-or-later
using namespace sphaira::nexusapp;
static NcmContentInfo Content(u8 id){NcmContentInfo info{};info.content_id.c[0]=id;info.size_low=4096;info.content_type=1;return info;}
static Record Metadata(u64 app,std::initializer_list<u8> ids,bool legacy=false){
    Record r;r.key={app,1,NcmContentMetaType_Application,0,{0,0}};
    NcmContentMetaHeader h{16,u16(ids.size()),u16(legacy?1:0),0,0};r.bytes.resize(8+16+ids.size()*24);
    std::memcpy(r.bytes.data(),&h,8);size_t pos=24;for(auto id:ids){const auto i=Content(id);std::memcpy(r.bytes.data()+pos,&i,sizeof(i));pos+=sizeof(i);}return r;
}
static void Prepare(TransferSession& session){
    Reset();database.push_back(Metadata(0x100,{1,2}));database.push_back(Metadata(0x200,{2,3,4},true));
    CHECK(database.back().bytes.size()==96);sd_files={{1,4096},{2,4096},{3,4096},{4,4096}};usb_files={{1,4096},{2,4096}};
    CHECK(!session.source.Open(nexus::Target::Sd)&&!session.destination.Open(nexus::Target::Usb));
    Meta verified;CHECK(!LoadMeta(session.source.db,database.front().key,verified));
    session.verified_metas={verified};session.application=0x100;session.verified_bytes=8192;session.ready=true;
    db_reads=checks=db_max_request=0;
}
static Result Cleanup(TransferSession& session,RemovalReport& report){sphaira::ui::ProgressBox progress;
    const auto rc=RemoveVerifiedSourceInSession(&progress,0x100,nexus::Target::Sd,nexus::Target::Usb,report,&session);
    CHECK(ns_refs==0);return rc;
}
int main(){
    {TransferSession session;Prepare(session);RemovalReport report;const auto rc=Cleanup(session,report);
#ifdef EXPECT_PRE_FIX_FAILURE
        CHECK(rc==::Invalid&&!report.removal_started&&delete_calls==0&&sd_files.size()==4);
        CHECK(report.stage=="Check another record for shared files");
        std::cout<<"PASS pre-fix production cleanup reproduces legacy-forwarder failure ("<<asserts<<" checks)\n";return 0;
#else
        CHECK(!rc&&report.removal_started&&report.removed==1&&report.shared==1);CHECK(deleted==std::vector<u8>{1});
        CHECK(!sd_files.contains(1)&&sd_files.contains(2)&&sd_files.contains(3)&&sd_files.contains(4));
        CHECK(usb_files.size()==2&&usb_files.contains(1)&&usb_files.contains(2));
        CHECK(!session.ready&&registration_calls==2);CHECK(db_max_request==72);
#endif
    }
    {TransferSession session;Prepare(session);database.back().bytes.resize(95);RemovalReport report;
        CHECK(Cleanup(session,report)==::Invalid&&!delete_calls&&!report.removal_started&&sd_files.size()==4);}
    {TransferSession session;Prepare(session);cancel_stage="remove-check-reference";RemovalReport report;
        CHECK(Cleanup(session,report)==Cancel&&!delete_calls&&!report.removal_started);}
    {TransferSession session;Prepare(session);read_error_stage="remove-check-reference";RemovalReport report;
        CHECK(Cleanup(session,report)==Fault&&!delete_calls&&!report.removal_started);}
    {TransferSession session;Prepare(session);fail_stage="remove-list-references";RemovalReport report;
        CHECK(Cleanup(session,report)==Fault&&!delete_calls&&!report.removal_started);}
    {TransferSession session;Prepare(session);session.ready=false;RemovalReport report;
        CHECK(Cleanup(session,report)==::Invalid&&!delete_calls&&!report.removal_started);}
    {TransferSession session;Prepare(session);missing_destination=true;RemovalReport report;
        CHECK(Cleanup(session,report)==::Invalid&&!delete_calls&&!report.removal_started);}
    {TransferSession session;Prepare(session);usb_files[1]=4095;RemovalReport report;
        CHECK(Cleanup(session,report)==::Invalid&&!delete_calls&&!report.removal_started);}
    {TransferSession session;Prepare(session);session.verified_metas[0].raw[8]^=1;RemovalReport report;
        CHECK(Cleanup(session,report)==::Invalid&&!delete_calls&&!report.removal_started);}
    {TransferSession session;Prepare(session);session.destination.generation++;RemovalReport report;
        CHECK(Cleanup(session,report)==nexus::Unavailable&&!delete_calls&&!report.removal_started);}
    {TransferSession session;Prepare(session);disconnect_stage="remove-recheck-metadata";RemovalReport report;
        CHECK(Cleanup(session,report)==nexus::Unavailable&&!delete_calls&&!report.removal_started);}
    {TransferSession session;Prepare(session);cancel_stage="remove-preflight-paths";RemovalReport report;
        CHECK(Cleanup(session,report)==Cancel&&!delete_calls&&!report.removal_started);}
    {TransferSession session;Prepare(session);fail_stage="remove-ensure-registration-P2";RemovalReport report;
        CHECK(Cleanup(session,report)==Fault&&!delete_calls&&!report.removal_started);}
    std::cout<<"PASS production installed cleanup: legacy references, shared-file retention and deletion gates ("<<asserts<<" checks)\n";
}
