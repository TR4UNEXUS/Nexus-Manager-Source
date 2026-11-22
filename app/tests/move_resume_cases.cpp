using namespace sphaira;using namespace sphaira::nexusapp;
static MoveCheckpoint Setup(Target source,Target dest){
 databases.clear();for(auto& f:files)f.clear();home.clear();saved.clear();generation=1;deletes=fail_delete=hashes=fail_read=queries=fail_query=route_checks=route_removes=complete_receipts=0;cancelled=read_disconnect=metadata_changed=false;
 MoveCheckpoint c;c.application=0x100;c.sequence=1;c.source=source;c.destination=dest;
 for(u32 record=0;record<3;++record){
  TestMeta m;m.key={0x100+record,0,u8(0x80+record),0,{}};
  for(u8 j=1;j<=2;++j){NcmContentInfo info{};info.content_id.value[0]=record*2+j;info.size_low=64;info.content_type=j;
   Bytes data(64,info.content_id.value[0]);files[int(source)][info.content_id.value[0]]=data;files[int(dest)][info.content_id.value[0]]=data;
   std::array<u8,32> digest{};sha256CalculateHash(digest.data(),data.data(),data.size());c.files.push_back({info,digest});m.infos.push_back(info);}
  NcmContentMetaHeader h{};h.content_count=m.infos.size();m.raw.resize(sizeof(h)+m.infos.size()*sizeof(NcmContentInfo));memcpy(m.raw.data(),&h,sizeof(h));memcpy(m.raw.data()+sizeof(h),m.infos.data(),m.infos.size()*sizeof(NcmContentInfo));
  databases[nexus::LogicalStorage(source)][{m.key.id,m.key.version}]=m;databases[nexus::LogicalStorage(dest)][{m.key.id,m.key.version}]=m;c.records.push_back({m.key,m.raw});home.push_back({m.key,nexus::LogicalStorage(dest)});
 }
 saved.push_back(c);return c;
}
int main(){
 unsigned tests{};
 for(auto source:{Target::Sd,Target::Internal,Target::Usb})for(auto dest:{Target::Sd,Target::Internal,Target::Usb})if(source!=dest){
  auto c=Setup(source,dest);ui::ProgressBox p;TransferReport r;assert(!ResumeMove(&p,c,r));assert(deletes==6&&complete_receipts==1&&files[int(source)].empty()&&files[int(dest)].size()==6&&hashes==12);++tests;
  // Restart after some files, and possibly the original internal metadata,
  // were already removed. The remaining copy is freshly rehashed.
  c=Setup(source,dest);files[int(source)].erase(1);files[int(source)].erase(4);if(source==Target::Internal)databases[1].clear();p={};r={};assert(!ResumeMove(&p,c,r));assert(deletes==4&&hashes==10&&files[int(source)].empty());++tests;
  for(int failure=0;failure<8;++failure){c=Setup(source,dest);p={};r={};
   switch(failure){case 0:files[int(dest)][3][0]^=1;break;case 1:files[int(source)][4][0]^=1;break;case 2:files[int(dest)].erase(1);break;case 3:saved[0].records[0].raw[0]^=1;break;case 4:cancelled=true;break;case 5:home[0].storage_id=99;break;case 6:fail_read=1;break;case 7:saved[0].files.pop_back();break;}
   assert(ResumeMove(&p,c,r));assert(!deletes&&!complete_receipts&&!saved.empty());++tests;
  }
  c=Setup(source,dest);fail_delete=3;p={};r={};assert(ResumeMove(&p,c,r));assert(deletes==2&&!saved.empty());fail_delete=0;p={};r={};assert(!ResumeMove(&p,c,r));assert(deletes==6&&complete_receipts==1);++tests;
  // Another title shares one source file: it must survive source cleanup.
  c=Setup(source,dest);auto shared=databases[nexus::LogicalStorage(source)].begin()->second;shared.key.id=0x900;shared.infos.resize(1);NcmContentMetaHeader h{};h.content_count=1;shared.raw.resize(8+sizeof(NcmContentInfo));memcpy(shared.raw.data(),&h,8);memcpy(shared.raw.data()+8,shared.infos.data(),sizeof(NcmContentInfo));databases[nexus::LogicalStorage(source)][{0x900,0}]=shared;p={};r={};assert(!ResumeMove(&p,c,r));assert(deletes==5&&r.removal.shared==1&&files[int(source)].contains(1));++tests;
 }
 // Every metadata/IPC failure before first deletion must leave originals alone.
 auto c=Setup(Target::Sd,Target::Usb);ui::ProgressBox p;TransferReport r;assert(!ResumeMove(&p,c,r));const auto calls=queries;
 for(int fail=1;fail<=calls;++fail){c=Setup(Target::Sd,Target::Usb);p={};r={};fail_query=fail;const auto rc=ResumeMove(&p,c,r);assert(rc);assert(!complete_receipts);assert(files[2].size()==6);if(!deletes)assert(files[0].size()==6);++tests;}
 c=Setup(Target::Usb,Target::Sd);read_disconnect=true;p={};r={};assert(ResumeMove(&p,c,r)&&deletes==0);++tests;
 printf("PASS: %u actual resume/removal scenarios, 6 storage directions, partial deletion, changed hashes/metadata, shared files, cancellation, disconnection and injected IPC failures.\n",tests);
}
