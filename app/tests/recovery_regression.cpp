void AddRecord(u64 title=0x0100123400000000ULL,u32 version=0,u8 offset=0){
    Record r{{title,version,0x80,0,{}},{}};NcmContentMetaHeader h{0,3,0,0,0};r.data.resize(sizeof(h)+3*sizeof(NcmContentInfo));memcpy(r.data.data(),&h,sizeof(h));
    for(u8 i=0;i<3;++i){NcmContentInfo c{};c.content_id.c[0]=offset+i+1;c.size_low=0x1000;c.content_type=i==0?0:i==1?1:3;
        memcpy(r.data.data()+sizeof(h)+i*sizeof(c),&c,sizeof(c));Bytes bytes(0x1000);bytes[0]=offset+i+1;usb_disk[NcaPath(offset+i+1)]=bytes;
    }records.push_back(r);
}
int main(){
    using namespace sphaira::nexusapp;sphaira::ui::ProgressBox p;RecoveryPlan plan;
    Reset();AddRecord();assert(ScanUsbRecovery(&p,plan)==0 && plan.ready && writes==0 && disk.empty());
    assert(ApplyUsbRecovery(&p,plan)==0 && plan.published && handles==0 && disk.size()==4 && commits==4);
    assert(!R_SUCCEEDED(ApplyUsbRecovery(&p,plan)));
    auto saved=disk;assert(ScanUsbRecovery(&p,plan)==0 && plan.folder_exists && !plan.ready && disk==saved);
    for(int f=1;f<=21;++f){
        Reset();AddRecord();assert(ScanUsbRecovery(&p,plan)==0 && plan.ready);fault=f;
        // List count fault belongs to scanning, not applying an existing plan.
        if(f==17){assert(R_FAILED(ScanUsbRecovery(&p,plan)));continue;}
        auto rc=ApplyUsbRecovery(&p,plan);assert(f==18?R_SUCCEEDED(rc):R_FAILED(rc));assert(handles==0);
        if(f==12)assert(plan.published);else assert(!plan.published);
        if(f!=12)for(const auto& [path,data]:disk)assert(!path.starts_with(std::string(live)+"/"));
    }
    Reset();AddRecord();duplicate=true;assert(!ScanUsbRecovery(&p,plan) && !plan.ready && plan.maps.titles.empty());
    Reset();AddRecord();usb_disk.erase(NcaPath(2));assert(!ScanUsbRecovery(&p,plan) && !plan.ready);
    Reset();AddRecord();usb_disk[NcaPath(1)].resize(8);assert(!ScanUsbRecovery(&p,plan) && !plan.ready);
    Reset();AddRecord();boot=false;vsd=true;assert(R_FAILED(ScanUsbRecovery(&p,plan)) && disk.empty());
    Reset();AddRecord();assert(!ScanUsbRecovery(&p,plan));++generation;assert(R_FAILED(ApplyUsbRecovery(&p,plan)) && disk.empty());
    Reset();AddRecord();assert(!ScanUsbRecovery(&p,plan));cancelled=true;assert(R_FAILED(ApplyUsbRecovery(&p,plan)) && disk.empty());
    Reset();AddRecord();assert(!ScanUsbRecovery(&p,plan));records[0].data.back()^=1;assert(R_FAILED(ApplyUsbRecovery(&p,plan)) && disk.empty());
    Reset();AddRecord();AddRecord(records[0].key.id,1);records[1].data.resize(8);assert(!ScanUsbRecovery(&p,plan) && !plan.ready); // no fallback to older version
    Reset();AddRecord();records[0].data[0]=0xff;records[0].data[1]=0xff;assert(!ScanUsbRecovery(&p,plan) && !plan.ready);
    Reset();AddRecord();records[0].data[4]=0xff;assert(!ScanUsbRecovery(&p,plan) && !plan.ready);
    // Existing folder: fill missing files AND individual records, retaining flags.
    for(int mask=0;mask<8;++mask){
        Reset();AddRecord();dirs.insert(live);
        nexus::repair::Indexes old;assert(nexus::repair::Merge(old,0x1234,true,{{nexus::repair::Id{9},4096,0,0},{nexus::repair::Id{10},4096,1,0},{nexus::repair::Id{11},4096,3,0}})==nexus::repair::MergeResult::Changed);
        old.titles[0x1234]=0xfedcba98;auto before=nexus::repair::Encode(old);
        for(int i=0;i<3;++i)if(mask&(1<<i))disk[nexus::repair::Live(i)]=before[i].data;
        const std::string flag=std::string(live)+"/allow_unsigned_nca.flag";disk[flag]={8,7,6};
        auto untouched=disk;assert(!ScanUsbRecovery(&p,plan) && plan.ready && plan.repair_records==1 && writes==0 && disk==untouched);
        assert(!ApplyUsbRecovery(&p,plan) && plan.published && !disk.contains(nexus::repair::Pending));
        assert(disk[flag]==untouched[flag]);nexus::repair::Snapshot current;
        for(int i=0;i<3;++i)current[i]={true,disk.at(nexus::repair::Live(i))};
        nexus::repair::Indexes actual;assert(nexus::repair::Decode(current,actual));
        assert(actual.titles.contains(records[0].key.id));if(mask&1)assert(actual.titles.at(0x1234)==0xfedcba98);
        untouched=disk;auto count=writes;assert(!ScanUsbRecovery(&p,plan) && !plan.ready && plan.intact==1 && disk==untouched && writes==count);
    }
    // All hashes mismatched: a diagnostic report is allowed, live indexes are not.
    Reset();AddRecord();dirs.insert(live);assert(!ScanUsbRecovery(&p,plan));fault=18;
    assert(!ApplyUsbRecovery(&p,plan) && !plan.published && plan.issues.size()==3 && !plan.report_path.empty());
    for(int i=0;i<3;++i)assert(!disk.contains(nexus::repair::Live(i)));
    // Existing index changes during confirmation must abort instead of clobber.
    Reset();AddRecord();dirs.insert(live);assert(!ScanUsbRecovery(&p,plan));disk[nexus::repair::Live(0)]={1,2,3};auto racing=disk;
    assert(R_FAILED(ApplyUsbRecovery(&p,plan)) && disk==racing && writes==0);
    assert(R_FAILED(ScanUsbRecovery(&p,plan)) && disk==racing && writes==0);
    // Interrupted publication with missing original indexes: next scan offers restore.
    Reset();AddRecord();dirs.insert(live);assert(!ScanUsbRecovery(&p,plan));fail_rename=2;
    assert(R_FAILED(ApplyUsbRecovery(&p,plan)) && plan.published && disk.contains(nexus::repair::Pending));
    fail_rename=-1;assert(!ScanUsbRecovery(&p,plan) && plan.pending && plan.ready);
    assert(!ApplyUsbRecovery(&p,plan) && plan.published && !disk.contains(nexus::repair::Pending));
    for(int i=0;i<3;++i)assert(!disk.contains(nexus::repair::Live(i)));
    assert(!ScanUsbRecovery(&p,plan) && !plan.pending && plan.ready);assert(!ApplyUsbRecovery(&p,plan));
    // A single missing entry/field in existing valid indexes (not missing files).
    for(int hole=0;hole<5;++hole){
        Reset();AddRecord();assert(!ScanUsbRecovery(&p,plan));assert(!ApplyUsbRecovery(&p,plan));
        nexus::repair::Snapshot full;for(int i=0;i<3;++i)full[i]={true,disk.at(nexus::repair::Live(i))};
        nexus::repair::Indexes m;assert(nexus::repair::Decode(full,m));const auto id=records[0].key.id;
        m.titles[id]=0xabcdef;
        if(hole==0)m.titles.erase(id);
        if(hole==1)m.mappings.erase(id);
        if(hole==2)m.mappings[id][0]={};
        if(hole==3)m.mappings[id][1]={};
        if(hole==4)m.content.erase(nexus::repair::Id{2});
        const auto partial=nexus::repair::Encode(m);
        for(int i=0;i<3;++i)disk[nexus::repair::Live(i)]=partial[i].data;
        auto old=disk;assert(!ScanUsbRecovery(&p,plan) && plan.ready && plan.repair_records==1 && disk==old);
        assert(!ApplyUsbRecovery(&p,plan));nexus::repair::Snapshot repaired;
        for(int i=0;i<3;++i)repaired[i]={true,disk.at(nexus::repair::Live(i))};
        assert(nexus::repair::Decode(repaired,m));assert(m.titles.at(id)==(hole==0?0:0xabcdef));assert(m.mappings.at(id)[0]==nexus::repair::Id{1});assert(m.content.contains(nexus::repair::Id{2}));
        // Any unchanged index retains its original byte ordering/representation.
        if(hole!=0)assert(repaired[0]==partial[0]);
        if(hole==0 || hole==4)assert(repaired[1]==partial[1]);
        if(hole!=4)assert(repaired[2]==partial[2]);
    }
    // Already-indexed base plus new patch and DLC; no extra base-title entries.
    Reset();AddRecord();assert(!ScanUsbRecovery(&p,plan));assert(!ApplyUsbRecovery(&p,plan));
    auto base_title=disk.at(nexus::repair::Live(0)),base_mapping=disk.at(nexus::repair::Live(1));
    AddRecord(records[0].key.id+0x800,0,10);records.back().key.type=0x81;
    AddRecord(records[0].key.id+0x1001,0,20);records.back().key.type=0x82;
    assert(!ScanUsbRecovery(&p,plan) && plan.ready && plan.intact==1 && plan.repair_records==2);
    assert(!ApplyUsbRecovery(&p,plan));assert(disk.at(nexus::repair::Live(0))==base_title && disk.at(nexus::repair::Live(1))==base_mapping);
    assert(nexus::repair::Get32(disk.at(nexus::repair::Live(2)),8)==9);
    // MP1: the collection stays ONE title, but secondary programs and their
    // updates must all be repaired even when the parent indexes already exist.
    Reset();AddRecord(0x0100E40022CE2000ULL);assert(!ScanUsbRecovery(&p,plan));assert(!ApplyUsbRecovery(&p,plan));
    base_title=disk.at(nexus::repair::Live(0));base_mapping=disk.at(nexus::repair::Live(1));
    auto child=[](Record& r,u8 id,u8 offset){
        NcmContentMetaHeader h{};memcpy(&h,r.data.data(),sizeof(h));++h.content_count;memcpy(r.data.data(),&h,sizeof(h));
        NcmContentInfo c{};c.content_id.c[0]=id;c.size_low=0x1000;c.content_type=1;c.id_offset=offset;
        const auto n=r.data.size();r.data.resize(n+sizeof(c));memcpy(r.data.data()+n,&c,sizeof(c));
        Bytes bytes(0x1000);bytes[0]=id;usb_disk[NcaPath(id)]=bytes;
    };
    child(records[0],31,1);child(records[0],32,2);child(records[0],33,255);
    AddRecord(0x0100E40022CE2800ULL,327680,10);records.back().key.type=0x81;
    child(records.back(),41,1);child(records.back(),42,2);child(records.back(),43,255);
    assert(!ScanUsbRecovery(&p,plan) && plan.ready && plan.folder_exists && plan.repair_records==2 && plan.files.size()==12);
    assert(!ApplyUsbRecovery(&p,plan) && plan.published);
    assert(disk.at(nexus::repair::Live(0))==base_title && disk.at(nexus::repair::Live(1))==base_mapping);
    nexus::repair::Snapshot multi_snapshot;for(int i=0;i<3;++i)multi_snapshot[i]={true,disk.at(nexus::repair::Live(i))};
    nexus::repair::Indexes multi_indexes;assert(nexus::repair::Decode(multi_snapshot,multi_indexes));
    assert(multi_indexes.titles.size()==1 && multi_indexes.mappings.size()==1 && multi_indexes.content.size()==12);
    for(u8 id:{31,32,33,41,42,43})assert(multi_indexes.content.contains(nexus::repair::Id{id}));
    auto multi_saved=disk;auto multi_writes=writes;
    assert(!ScanUsbRecovery(&p,plan) && !plan.ready && plan.intact==2 && disk==multi_saved && writes==multi_writes);
    assert(plan.detail.find("secondary Program NCAs")!=std::string::npos);
    // Production adapter rollback with actual prior nonempty indexes, at each rename.
    for(int cut=0;cut<8;++cut){
        Reset();AddRecord();assert(!ScanUsbRecovery(&p,plan));assert(!ApplyUsbRecovery(&p,plan));
        nexus::repair::Snapshot old;for(int i=0;i<3;++i)old[i]={true,disk.at(nexus::repair::Live(i))};
        AddRecord(records[0].key.id+0x10000,0,10);assert(!ScanUsbRecovery(&p,plan) && plan.ready);
        renames=0;fail_rename=cut;assert(R_FAILED(ApplyUsbRecovery(&p,plan)));fail_rename=-1;
        if(disk.contains(nexus::repair::Pending)){assert(!ScanUsbRecovery(&p,plan) && plan.pending);assert(!ApplyUsbRecovery(&p,plan));}
        for(int i=0;i<3;++i)assert(disk.at(nexus::repair::Live(i))==old[i].data);
    }
    assert(handles==0);
    std::cout<<"PASS: production scan/apply, multi-program collection/patch recovery, existing-folder record gaps, base/update/DLC merge, aliases/settings, no-op rescan, every rename failure/restore, native-SD protection, metadata bounds/changes, latest-version selection, virtual SD refusal, hash/short-read failure, SD faults, cancellation and generation change\n";
}
