// SPDX-License-Identifier: GPL-3.0-or-later
// Included by the NAS UI harness after the actual production queue functions.
void MarkNetworkFile(const std::string& name){
    assert(App::browser);auto& model=*App::browser;
    const auto row=std::find_if(model.rows.begin(),model.rows.end(),[&](const auto& value){return value.name==name;});
    assert(row!=model.rows.end());model.selected=static_cast<size_t>(row-model.rows.begin());assert(model.Toggle());
}
void QueuePreflight(const std::string& destination){
    assert(App::browser&&!App::browser->marked.empty());App::browser->InstallSelected();
    assert(App::sidebar->title=="Install selected files"&&App::sidebar->entries.size()==3&&App::Has("Tickets + link existing files"));
    for(const auto& row:App::sidebar->entries)assert(dynamic_cast<sphaira::ui::SidebarEntryCallback*>(row.get())->pop_on_click);
    App::Select(destination);assert(App::progress&&App::progress->operation==nexus::safety::Operation::ReadOnly);
    App::Run();
}
void SelectThree(){
    state::files.emplace("/third.XCI",nexus::files::Entry{"/third.XCI",1024,false,{}});
    OpenBrowse();MarkNetworkFile("game.nsp");MarkNetworkFile("compressed.nsz");MarkNetworkFile("third.XCI");
}
void RunQueueUiTests(){
    using Record=NetworkQueueRecord;using Status=Record::State;
    // Both protocols share the same four-format queue flow, one destination
    // choice and one confirmation. Each install receives the same live mount.
    for(const auto& protocol:{"SMB","NFS"})for(const auto& destination:{"SD Card","USB Drive"}){
        Reset();state::files.emplace("/cart.XCI",nexus::files::Entry{"/cart.XCI",1024,false,{}});
        state::files.emplace("/small.XcZ",nexus::files::Entry{"/small.XcZ",1024,false,{}});
        OpenBrowse(protocol);MarkNetworkFile("game.nsp");MarkNetworkFile("compressed.nsz");MarkNetworkFile("cart.XCI");MarkNetworkFile("small.XcZ");
        QueuePreflight(destination);assert(!state::installs&&App::option->right=="Install all"&&App::option->selected==0);
        App::Press(1);assert(App::progress->operation==nexus::safety::Operation::MayWrite);App::Run();
        assert((state::installed_paths==std::vector<std::string>{"/game.nsp","/compressed.nsz","/cart.XCI","/small.XcZ"}));
        assert(state::installs==4&&state::mutations==4&&!state::copies&&state::installed_mount=="nexusnet1:/");
        assert(state::target==(std::string(destination)=="SD Card"?nexus::Target::Sd:nexus::Target::Usb));
        assert(state::selected_generation==(std::string(destination)=="SD Card"?0:77));
        for(size_t i=0;i<4;++i){assert(state::queue_actions[i]=="Install "+std::to_string(i+1)+" / 4 from "+protocol);assert(state::queue_images[i]==0);}
        assert(App::browser->marked.empty()&&App::option->message.find("queue finished")!=std::string::npos&&App::option->message.find("Installed: 4 files")!=std::string::npos);
    }
    // Marking files in different folders does not change the mounted session
    // or lose the first folder's reviewed selection/order.
    Reset();state::files.emplace("/folder/nested.XCZ",nexus::files::Entry{"/folder/nested.XCZ",1024,false,{}});OpenBrowse();
    MarkNetworkFile("game.nsp");App::Select("folder");App::Run();MarkNetworkFile("nested.XCZ");App::browser->back();App::Run();
    assert(App::browser->marked.size()==2);QueuePreflight("SD Card");App::Press(1);App::Run();
    assert((state::installed_paths==std::vector<std::string>{"/game.nsp","/folder/nested.XCZ"}));
    // Use structured outcomes, including successful mixed installed/skipped
    // content, rather than deriving success from translated completion text.
    Reset();OpenBrowse();MarkNetworkFile("game.nsp");MarkNetworkFile("compressed.nsz");
    state::outcomes={{"/game.nsp",{2,1}},{"/compressed.nsz",{0,3}}};state::completion="Exact installer completion: current newer versions were kept.";
    QueuePreflight("USB Drive");App::Press(1);App::Run();
    assert(state::installs==2&&state::mutations==1&&App::browser->marked.empty());
    assert(App::option->message.find("Installed: 1 files")!=std::string::npos&&App::option->message.find("Skipped: 1 files")!=std::string::npos&&App::option->message.find(state::completion)!=std::string::npos);
    // On a failed second file, the completed first selection is removed and
    // the failed and never-started entries remain available for explicit retry.
    Reset();SelectThree();state::partial_records=2;state::after_install=[](auto*){state::install_error=Fault;};
    QueuePreflight("USB Drive");App::Press(1);App::Run();
    assert(state::installs==2&&App::browser->marked.size()==2&&App::browser->marked[0].path=="/compressed.nsz");
    assert(App::option->message.find("Failed: 1 files")!=std::string::npos&&App::option->message.find("Not started: 1 files")!=std::string::npos);
    assert(App::option->message.find("Completed entries before stopping: 2 installed")!=std::string::npos);
    // Changes between files must never start another install on a new drive,
    // another mount, a backgrounded app, or after a cancellation request.
    for(int fault=0;fault<4;++fault){
        Reset();SelectThree();state::after_install=[fault](auto* progress){if(fault==0)++state::generation;if(fault==1)state::mounted=false;if(fault==2)state::foreground=false;if(fault==3)progress->cancelled=true;};
        QueuePreflight("USB Drive");App::Press(1);App::Run();assert(state::installs==1&&App::browser->marked.size()==2);
        assert(App::option->message.find("queue stopped")!=std::string::npos&&App::option->message.find(fault==3?"Cancelled: 1 files":"Failed: 1 files")!=std::string::npos);
    }
    // Cancellation before work leaves every file selected and never invokes
    // the installer. Declining confirmation does not create a worker.
    Reset();SelectThree();QueuePreflight("SD Card");App::Press(1);App::Run(true);
    assert(!state::installs&&App::browser->marked.size()==3&&App::option->message.find("Cancelled: 1 files")!=std::string::npos);
    for(auto choice:{std::optional<s64>{},std::optional<s64>{0}}){Reset();SelectThree();QueuePreflight("SD Card");App::Press(choice);assert(!App::progress&&!state::installs&&App::browser->marked.size()==3);}
    // A worker/thread startup failure still gives a stopped report, never a
    // false successful empty queue, and it does not discard the selection.
    Reset();SelectThree();QueuePreflight("SD Card");App::Press(1);
    {auto worker=std::move(App::progress);worker->done(Fault);}
    assert(!state::installs&&App::browser->marked.size()==3&&App::option->message.find("queue stopped")!=std::string::npos&&App::option->message.find("Failed: 1 files")!=std::string::npos);
    // Refresh preserves reviewed size: replaced files require explicit
    // unmark/reselection before preflight can accept the changed package.
    Reset();OpenBrowse();MarkNetworkFile("game.nsp");state::files.at("/game.nsp").size=2048;
    App::browser->refresh();App::Run();assert(App::browser->marked.front().size==1024);
    QueuePreflight("SD Card");assert(!state::installs&&!App::progress&&App::browser->marked.size()==1&&App::option->message.find("Nothing was installed")!=std::string::npos);
    // Destination generation is captured before confirmation, not looked up
    // again and silently rebound when the user confirms later.
    Reset();SelectThree();QueuePreflight("USB Drive");++state::generation;App::Press(1);App::Run();assert(!state::installs&&App::browser->marked.size()==3);
    // The worker and its completion retain the mount even when the browser
    // and destination menu disappear; the weak model callback creates no cycle.
    Reset();SelectThree();QueuePreflight("SD Card");App::sidebar.reset();App::browser.reset();assert(!state::session.expired());App::Press(1);assert(!state::session.expired());App::Run();
    assert(state::installs==3&&state::session.expired()&&state::unmounts==1&&state::closes==1);
    // Invalid direct helper calls do no work and preserve a failed report.
    Reset();auto session=std::make_shared<Session>();session->mount="nexusnet1:/";session->protocol="SMB";state::session=session;
    sphaira::ui::ProgressBox progress(0,"queue","",[](auto*)->Result{return 0;},[](auto){});NetworkQueueReport report;
    std::vector<SelectedNetworkFile> duplicate{{"/game.nsp",1024},{"/game.nsp",1024}};
    assert(RunInstallQueue(session,duplicate,nexus::Target::Sd,0,&progress,report)==nexus::Invalid&&!state::installs&&report[1].state==Status::Failed);
    std::vector<SelectedNetworkFile> too_many(65,{"/game.nsp",1024});assert(RunInstallQueue(session,too_many,nexus::Target::Sd,0,&progress,report)==nexus::Invalid&&report[0].state==Status::Failed);
    assert(CheckInstallQueue({},&progress,nexus::Target::Sd,0)==nexus::Invalid&&CheckInstallQueue(session,nullptr,nexus::Target::Sd,0)==nexus::Invalid);
    assert(RunInstallQueue({},duplicate,nexus::Target::Sd,0,&progress,report)==nexus::Invalid&&!state::installs);
    std::cout<<"Production NAS queue UI: SMB/NFS four formats, cross-folder marking, one destination/confirmation, exact outcomes, sequential stop/cancel, drive/session pinning, changed files, startup failure and lifetime cleanup passed\n";
}
