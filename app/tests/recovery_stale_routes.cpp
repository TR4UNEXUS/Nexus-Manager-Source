// Production scan/apply with stale routing and complete/incomplete USB files.
using namespace sphaira::nexusapp;
static nexus::repair::Snapshot Snapshot(){
    nexus::repair::Snapshot s;
    for(size_t i=0;i<3;++i)if(disk.contains(nexus::repair::Live(i)))s[i]={true,disk.at(nexus::repair::Live(i))};
    return s;
}
static nexus::repair::Indexes Indexes(){nexus::repair::Indexes m;assert(nexus::repair::Decode(Snapshot(),m));return m;}
static void Save(const nexus::repair::Snapshot& s){dirs.insert(live);for(size_t i=0;i<3;++i)disk[nexus::repair::Live(i)]=s[i].data;}
int main(){
    sphaira::ui::ProgressBox p;RecoveryPlan plan;
    constexpr u64 first=0x0100123400000000ULL;
    // An old registered title whose USB files are absent must not prevent a
    // different complete title from recovering. No missing bytes are invented.
    Reset();AddRecord(first);assert(!ScanUsbRecovery(&p,plan)&&!ApplyUsbRecovery(&p,plan));
    const auto original=Indexes();for(u8 id:{1,2,3})usb_disk.erase(NcaPath(id));
    AddRecord(first+0x10000,0,10);routed_path_result=0xA05;
    const auto before_scan=disk;
    assert(!ScanUsbRecovery(&p,plan)&&plan.ready&&plan.repair_records==1&&disk==before_scan);
    assert(!ApplyUsbRecovery(&p,plan)&&plan.repaired_records==1&&routed_path_queries==0);
    auto repaired=Indexes();assert(repaired.titles.size()==2&&repaired.content.size()==6);
    assert(repaired.mappings.at(first)==original.mappings.at(first));
    // Mirror the supplied mapping shape: nine retained title rows, with all
    // 27 meta/program/control routes absent. Physical complete files are
    // required before any of those missing routes may be restored.
    for(int mode=0;mode<5;++mode){
        Reset();for(u8 row=0;row<9;++row)AddRecord(first+u64(row)*0x10000,0,row*3);
        assert(!ScanUsbRecovery(&p,plan)&&!ApplyUsbRecovery(&p,plan));
        auto maps=Indexes();maps.content.clear();maps.titles[first]=0xabcdef;
        Save(nexus::repair::Encode(maps));auto before=Snapshot();routed_path_result=0xA05;
        if(mode==1)usb_disk.clear();
        if(mode==2)usb_disk.erase(NcaPath(2));
        if(mode==3)usb_disk[NcaPath(2)][0]^=0x80;
        if(mode==4)duplicate=true; // preserve a native SD program copy
        assert(!ScanUsbRecovery(&p,plan)&&Snapshot()==before);
        if(mode==1||mode==4){assert(!plan.ready);continue;}
        assert(plan.ready);assert(!ApplyUsbRecovery(&p,plan)&&plan.repair_complete);
        repaired=Indexes();assert(repaired.titles==maps.titles&&repaired.mappings==maps.mappings);
        assert(Snapshot()[0]==before[0]&&Snapshot()[1]==before[1]);
        assert(repaired.content.size()==(mode==0?27:24));
        if(mode==2||mode==3)for(u8 id:{1,2,3})assert(!repaired.content.contains(nexus::repair::Id{id}));
        assert(routed_path_queries==0);
        const auto written=writes;auto after=disk;
        assert(!ScanUsbRecovery(&p,plan)&&disk==after&&writes==written);
        if(mode!=3)assert(!plan.ready); // bad hash remains eligible for recheck
    }
    // A connection/stat error is never treated as "file absent".
    Reset();AddRecord();fault=20;routed_path_result=0xA05;
    assert(ScanUsbRecovery(&p,plan)==Fault&&!plan.ready&&writes==0&&routed_path_queries==0);
    assert(handles==0);
    puts("PASS: stale routes, 9-title/27-entry repair, physical absence, bad hash, SD duplicate protection, no-op rescan, and USB errors.");
}
