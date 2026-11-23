using namespace nexus::reports;
size_t Queued(){return std::count_if(disk.begin(),disk.end(),[](auto& e){return e.first.ends_with(".json");});}
void Reset(){Stop();disk.clear();trace.clear();seen.clear();preference=-2;offered=false;started=false;finished=false;cancel=false;last_receipt.clear();next_attempt={};starts=sends=notifications=0;send_rc=0;bad_receipt=write_receipt_failure=remove_failure=sd_failure=false;nexus::lifecycle::foreground=true;}
int main(){
 Reset();assert(OfferChoice()&&!OfferChoice());QueueFailure("install",1);assert(!Queued());
 assert(!SetPreference(false));QueueFailure("repair",2);assert(!Queued());
 assert(!SetPreference(true));QueueFailure("install",1);QueueFailure("install",1);QueueFailure("install",0);assert(Queued()==1);
 for(const auto& [name,bytes]:disk)if(name.ends_with(".json")){
  const std::string body(bytes.begin(),bytes.end());
  assert(body.find("report-failure-install rc=0x00000001")!=std::string::npos);
 }
 Poll(false);assert(!starts&&!sends);nexus::lifecycle::foreground=false;Poll(true);assert(!starts);nexus::lifecycle::foreground=true;
 send_rc=Fault;Poll(true);assert(starts==1&&sends==1&&Queued()==1);Poll(false);assert(!started);Poll(true);assert(starts==1); // retry delay
 cancel=false;send_rc=0;bad_receipt=true;assert(UploadOne()==nexus::Invalid&&Queued()==1);assert(!disk.contains(Receipt));
 bad_receipt=false;write_receipt_failure=true;assert(UploadOne()==Fault&&Queued()==1);write_receipt_failure=false;remove_failure=true;assert(UploadOne()==Fault&&Queued()==1&&disk.contains(Receipt));
 remove_failure=false;trace.clear();assert(!UploadOne()&&!Queued());assert(trace.size()==2&&trace[0]==std::string("write:")+Receipt&&trace[1].starts_with("remove:"));
 Reset();assert(!SetPreference(true));QueueFailure("repair",1);assert(Queued()==1);assert(!SetPreference(false));cancel=false;assert(!UploadOne()&&sends==0&&Queued()==1);
 std::string preview;assert(!QueueManual("Game did not start",preview));assert(Queued()==2);cancel=false;assert(!UploadOne()&&sends==1&&Queued()==1); // manual is explicitly authorised
 assert(QueueManual("password: private",preview)==nexus::Invalid);assert(Queued()==1);
 disk[std::string(Root)+"/other.txt"]={'a'};assert(!ClearQueued()&&!Queued()&&disk.contains(std::string(Root)+"/other.txt"));
 Reset();assert(!SetPreference(true));for(unsigned i=1;i<=20;++i)QueueFailure("install",i);assert(Queued()==MaxQueued);assert(QueueManual("overflow",preview)==nexus::Unavailable);
 Reset();sd_failure=true;assert(SetPreference(true)==Fault&&Preference()==0);QueueFailure("repair",1);assert(!Queued());
 Reset();const std::string status="usb_query=0x00000000 mounted=1 ready=1 state=0 usb_result=0x00000000 backup_query=0x00000000 backup_flags=1 backup_result=0x00000000";
 const auto valid=Payload("0.2.28","manual",0,"user description","","",status);bool manual{};assert(QueuedPayload(valid,manual)&&manual);
 auto bad=valid;bad.replace(bad.find(status),status.size(),"private details");assert(!QueuedPayload(bad,manual));
 bad=valid;bad.replace(bad.find("manual"),6,"install");assert(!QueuedPayload(bad,manual));
 bad=valid;bad.replace(bad.find("manual"),6,"manual\\u0000secret");assert(!QueuedPayload(bad,manual));
 bad=valid;bad.insert(1,"\"schema\":1,");assert(!QueuedPayload(bad,manual));
 bad=valid;bad.replace(bad.find("schema\":1")+8,1,"true");assert(!QueuedPayload(bad,manual));
 const auto id=Digest(bad);disk[std::string(Root)+"/"+id+".json"]={bad.begin(),bad.end()};assert(!UploadOne()&&!sends&&Queued()==1);
 std::cout<<"PASS: production report queue, consent, idle/retry gating, receipt persistence, deduplication, limits and private-field rejection.\n";
}
