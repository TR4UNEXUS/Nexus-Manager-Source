// Included after the production recovery adapter and the common test helpers.
int main(){
 using namespace sphaira::nexusapp;
 Reset();backup_failure=0;backup_calls=0;AddRecord();sphaira::ui::ProgressBox p;RecoveryPlan plan;
 assert(!ScanUsbRecovery(&p,plan)&&plan.ready);assert(!ApplyUsbRecovery(&p,plan));
 assert(plan.backup_confirmed&&plan.backup_attempted&&plan.console_backup_sequence==7&&backup_calls==1);
 assert(plan.report_text.find("Backup updated and checked")!=std::string::npos);
 // The actual post-install readback checks the affected record, even when an
 // unrelated legacy title is incomplete. A missing or wrong affected link fails.
 std::vector<NcmContentInfo> infos(3);memcpy(infos.data(),records[0].data.data()+sizeof(NcmContentMetaHeader),3*sizeof(NcmContentInfo));
 assert(!CheckUsbRecordRouting(&p,generation,records[0].key,infos));
 const auto valid=disk;
 for(unsigned hole=0;hole<5;++hole){
   disk=valid;nexus::repair::Snapshot snapshot;for(unsigned i=0;i<3;++i)snapshot[i]={true,disk.at(nexus::repair::Live(i))};
   nexus::repair::Indexes maps;assert(nexus::repair::Decode(snapshot,maps));
   if(hole<3)maps.content.erase(nexus::repair::Id{u8(hole+1)});
   if(hole==3)maps.titles.erase(records[0].key.id);
   if(hole==4)maps.mappings[records[0].key.id][0]=nexus::repair::Id{99};
   snapshot=nexus::repair::Encode(maps);for(unsigned i=0;i<3;++i)disk[nexus::repair::Live(i)]=snapshot[i].data;
   assert(R_FAILED(CheckUsbRecordRouting(&p,generation,records[0].key,infos)));
 }
 disk=valid;assert(R_FAILED(CheckUsbRecordRouting(&p,generation+1,records[0].key,infos)));
 nexus::repair::Snapshot snapshot;for(unsigned i=0;i<3;++i)snapshot[i]={true,disk.at(nexus::repair::Live(i))};
 nexus::repair::Indexes maps;assert(nexus::repair::Decode(snapshot,maps));maps.titles[0x999]=0;maps.mappings[0x999]={nexus::repair::Id{90},nexus::repair::Id{91},nexus::repair::Id{92}};
 snapshot=nexus::repair::Encode(maps);for(unsigned i=0;i<3;++i)disk[nexus::repair::Live(i)]=snapshot[i].data;
 assert(!CheckUsbRecordRouting(&p,generation,records[0].key,infos));
 Reset();backup_failure=Fault;backup_calls=0;AddRecord();p={};plan={};
 assert(!ScanUsbRecovery(&p,plan)&&plan.ready);assert(!ApplyUsbRecovery(&p,plan));
 assert(plan.repair_complete&&!plan.backup_confirmed&&plan.backup_result==Fault&&backup_calls==1);
 assert(plan.report_text.find("previous valid backup was kept")!=std::string::npos);
 // Report six games, not 84 games, with base/update/DLC records nested below.
 RecoveryPlan grouped;grouped.repair_complete=grouped.verification_complete=true;
 const unsigned counts[]{3,7,6,47,19,2};
 for(unsigned game=0;game<6;++game)for(unsigned index=0;index<counts[game];++index){
   const u64 app=0x100+game;const auto type=u8(index==0?0x80:index==1?0x81:0x82);const u64 id=0x10000+game*100+index;
   grouped.scan_records.push_back({app,id,0,type,"Game "+std::to_string(game),"Waiting for verification"});
   RecoveryMeta m;m.key={id,0,type,0,{}};m.application_id=app;m.name="Game "+std::to_string(game);m.blocked=true;grouped.metadata.push_back(m);
 }
 auto report=BuildReport(grouped,0);assert(report.find("Games reviewed: 6")!=std::string::npos);
 assert(report.find("45 x DLC: Skipped")!=std::string::npos);
 assert(report.find("Games reviewed: 84")==std::string::npos);
 backup_failure=0;
 puts("PASS: LR1 repair success vs backup failure are independent; confirmed backup sequence and game-family reports.");
}
