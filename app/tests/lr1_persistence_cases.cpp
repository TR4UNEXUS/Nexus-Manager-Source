using namespace sphaira::nexusapp;
MoveCheckpoint Sample(){MoveCheckpoint c;c.application=0x0100000012345000;c.sequence=1;c.source=nexus::Target::Sd;c.destination=nexus::Target::Usb;NcmContentMetaKey key{};key.id=c.application;key.type=0x80;c.records.push_back({key,std::vector<u8>(8,7)});MoveCheckpointFile f{};f.info.content_id.data[0]=1;f.info.size[1]=16;f.info.content_type=1;f.hash.fill(9);c.files.push_back(f);return c;}
void Reset(){disk.clear();dirs.clear();calls=fail_at=0;short_read=change_after_save=wrong_archive=false;checkpoint_rc=archive_rc=0;}
int main(){
 size_t checks{};Reset();auto sample=Sample();Bytes encoded;assert(Encode(sample,encoded));MoveCheckpoint decoded;assert(Decode(encoded,decoded));++checks;
 for(size_t i=0;i<encoded.size();++i){auto bad=encoded;bad[i]^=1;assert(!Decode(bad,decoded));++checks;}
 for(size_t i=0;i<encoded.size();++i){assert(!Decode(Bytes(encoded.begin(),encoded.begin()+i),decoded));++checks;}
 auto duplicate=sample;duplicate.files.push_back(duplicate.files[0]);assert(Encode(duplicate,encoded));assert(!Decode(encoded,decoded));++checks;
 Reset();sample=Sample();assert(SaveMoveCheckpoint(sample)==0);const auto operations=calls;
 std::vector<MoveCheckpoint> pending;assert(!PendingMoves(pending)&&pending.size()==1);assert(!CompleteMoveCheckpoint(sample));assert(!PendingMoves(pending)&&pending.empty());checks+=3;
 for(unsigned fail=1;fail<=operations;++fail){Reset();auto c=Sample();fail_at=fail;const auto result=SaveMoveCheckpoint(c);assert(result!=0);fail_at=0;calls=0;assert(!PendingMoves(pending));assert(pending.size()<=1);if(!pending.empty()){Bytes actual;assert(Encode(pending[0],actual));assert(disk.at(Name(c))==actual);}++checks;}
 Reset();sample=Sample();assert(!SaveMoveCheckpoint(sample));const auto before=disk;assert(SaveMoveCheckpoint(sample)!=0);assert(disk==before);++checks;
 Reset();sample=Sample();assert(!SaveMoveCheckpoint(sample));short_read=true;assert(PendingMoves(pending)!=0);++checks;
 Reset();sample=Sample();assert(!SaveMoveCheckpoint(sample));calls=0;assert(!CompleteMoveCheckpoint(sample));const auto completion_ops=calls;
 for(unsigned fail=1;fail<=completion_ops;++fail){Reset();auto c=Sample();assert(!SaveMoveCheckpoint(c));calls=0;fail_at=fail;assert(CompleteMoveCheckpoint(c)!=0);assert(disk.contains(Name(c))||disk.contains(Name(c)+".complete"));++checks;}
 auto maps=[](){Reset();for(unsigned i=0;i<3;++i)disk[nexus::repair::Live(i)]=std::vector<u8>(16,1+i);};
 maps();u64 seq=99;assert(!ConfirmConsoleRoutingBackup(seq)&&seq==42);const auto backup_ops=calls;++checks;
 for(unsigned fail=1;fail<=backup_ops;++fail){maps();fail_at=fail;seq=99;assert(ConfirmConsoleRoutingBackup(seq)!=0&&seq==0);++checks;}
 for(unsigned fault=0;fault<5;++fault){maps();if(fault==0)checkpoint_rc=Fault;if(fault==1)archive_rc=Fault;if(fault==2)wrong_archive=true;if(fault==3)change_after_save=true;if(fault==4)short_read=true;seq=99;assert(ConfirmConsoleRoutingBackup(seq)!=0&&seq==0);++checks;}
 printf("PASS: %zu checkpoint persistence/corruption/fault and console-backup readback checks.\n",checks);
}
