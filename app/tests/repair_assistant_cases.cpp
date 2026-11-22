using sphaira::App;using namespace sphaira::nexusapp;
void Reset(){App::option.reset();App::progress.reset();App::sidebar.reset();App::applet=false;App::workers=0;scans=applies=imports=backups=0;scan_rc=apply_rc=0;nexus::catalog::load_rc=0;}
int main(){
 Reset();App::applet=true;DisplayGuidedUsbRepair();assert(App::option&&!App::progress&&!scans&&!applies);
 Reset();DisplayGuidedUsbRepair();assert(App::progress&&App::progress->op==nexus::safety::Operation::ReadOnly&&!scans&&!applies);App::Run();assert(scans==1&&!applies&&App::sidebar->width==1280&&App::Has("Verify and repair missing links"));
 App::Select("Verify and repair missing links");assert(App::option&&!App::progress&&!applies);auto cancelled=App::option->callback;App::Answer(0);cancelled(1);assert(!App::progress&&!applies);
 App::Select("Verify and repair missing links");auto duplicate=App::option->callback;App::Answer(1);auto workers=App::workers;duplicate(1);assert(App::workers==workers&&App::progress&&!applies);App::Run();assert(applies==1&&App::sidebar->title=="USB repair result");
 for(bool cancelled_scan:{false,true}){Reset();scan_rc=cancelled_scan?0:123;DisplayGuidedUsbRepair();App::Run(cancelled_scan);assert(!App::Has("Verify and repair missing links")&&!applies&&App::Has("Scan summary"));}
 Reset();nexus::catalog::load_rc=123;DisplayGuidedUsbRepair();App::Run();assert(scans==1&&App::Has("Verify and repair missing links")); // local checks remain usable offline
 App::Select("Game");assert(App::option);App::Answer(1);assert(App::progress&&!applies);App::Run();assert(last_app==1&&scans==2&&!applies);
 App::Select("Games missing from HOME");App::Select("USB library backup");App::Select("Report a problem");assert(imports==1&&backups==1&&nexus::reports::opens==1&&!applies);
 App::Select("Update repair guidance");assert(App::progress&&nexus::reports::stops==1);App::Run();assert(nexus::catalog::updates==1&&!applies);
 std::cout<<"PASS: production guided repair preview, explicit confirmation, duplicate/cancel guard, failed scan, offline fallback and per-game recheck.\n";
}
