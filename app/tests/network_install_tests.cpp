int main(){
    using namespace sphaira;using namespace sphaira::nexusapp;
    const auto size=[](){return u64(state.bytes.size());};
    ui::ProgressBox p;nexus::install::Diagnostic detail;
    constexpr size_t MiB=1024*1024;
    Reset();
    {const auto inherited=yati::ResolveFilters({});assert(inherited.ticket_only&&inherited.skip_base&&inherited.skip_patch&&inherited.skip_addon&&inherited.skip_data_patch&&inherited.skip_ticket);
     yati::ConfigOverride override;override.skip_base=false;const auto partial=yati::ResolveFilters(override);assert(!partial.skip_base&&partial.ticket_only&&partial.skip_patch&&partial.skip_addon&&partial.skip_data_patch&&partial.skip_ticket);}
    // Ordinary and short reads remain bounded and random access works; the
    // same open handle serves every call and is released only on destruction.
    {
        NetworkPackageSource source(&p,"nexusnet1:/","/Game.NSP",size(),42);assert(!source.GetOpenResult());
        state.short_read=100003;std::vector<u8> data(600000);u64 got{};
        assert(!source.Read(data.data(),17,data.size(),&got)&&got==data.size());assert(!memcmp(data.data(),state.bytes.data()+17,data.size()));
        assert(state.max_request<=MiB&&state.max_request>256*1024&&state.source_reads>3&&state.opened==1&&state.closed==0);
        assert(!source.Read(data.data(),3,99,&got)&&got==99&&!memcmp(data.data(),state.bytes.data()+3,99));
        assert(p.network_active&&p.network_received==600099);
        const auto reads=state.source_reads;
        assert(source.Read(data.data(),-1,8,&got)==nexus::Invalid&&got==0);
        assert(source.Read(data.data(),size()-1,2,&got)==nexus::Invalid&&got==0);
        assert(source.Read(nullptr,0,1,&got)==nexus::Invalid&&got==0);
        assert(source.Read(data.data(),0,1,nullptr)==nexus::Invalid);
        assert(!source.Read(nullptr,size(),0,&got)&&got==0&&state.source_reads==reads);
    }assert(state.opened==1&&state.closed==1&&!p.network_active);
    // The production adapter submits one bounded MiB at a time rather than
    // sixteen serialized quarter-MiB calls for a four-MiB installer read.
    // This is the adapter boundary, not a measured network throughput claim.
    for(size_t requested:{MiB-1,MiB,MiB+1,4*MiB}){
        Reset(4*MiB+64);
        {
            NetworkPackageSource source(&p,"nexusnet1:/","/game.nsp",size(),42);assert(!source.GetOpenResult());
            assert(state.usb_constructions==1&&state.usb_destructions==0);
            const auto initial_checks=state.usb_checks.size();std::vector<u8> data(requested);u64 got{};
            assert(!source.Read(data.data(),17,requested,&got)&&got==requested&&!memcmp(data.data(),state.bytes.data()+17,requested));
            const auto expected_calls=(requested+MiB-1)/MiB;assert(state.source_reads==expected_calls&&state.read_requests.size()==expected_calls);
            for(size_t i=0;i<expected_calls;++i){assert(state.read_requests[i].first==17+i*MiB);assert(state.read_requests[i].second==std::min(MiB,requested-i*MiB));}
            assert(state.max_request<=MiB&&p.network_received==requested&&state.opened==1&&state.closed==0);
            assert(state.usb_constructions==1&&state.usb_checks.size()>=initial_checks+3*expected_calls);
            for(const auto& [client,generation]:state.usb_checks)assert(client==1&&generation==42);
        }
        assert(state.usb_destructions==1&&state.opened==1&&state.closed==1);
    }
    // Reusing the client must not cache a successful generation check. A
    // replacement drive or cancellation after the first MiB stops before
    // reading any payload from the following chunk, on the original handle.
    for(int fault=0;fault<4;++fault){
        Reset(4*MiB);NetworkPackageSource source(&p,"nexusnet1:/","/game.nsp",size(),42);
        state.after_read=[&,fault]{if(fault==0)++state.generation;if(fault==1)p.cancel=777;if(fault==2)state.read_error=EIO;if(fault==3)state.usb_error=778;};
        std::vector<u8> data(4*MiB);u64 got{99};const auto result=source.Read(data.data(),0,data.size(),&got);
        assert(result&&got==0&&p.network_received==MiB&&state.source_reads==(fault==2?2:1)&&state.opened==1&&state.closed==0);
        if(fault==0)assert(result==nexus::Unavailable);if(fault==1)assert(result==777);if(fault==3)assert(result==778);
        assert(state.usb_constructions==1);for(const auto& [client,generation]:state.usb_checks)assert(client==1&&generation==42);
        p.cancel=0;
    }
    Reset(2*MiB);{NetworkPackageSource source(&p,"nexusnet1:/","/game.nsp",size(),0);std::vector<u8> data(2*MiB);u64 got{};
        assert(!source.Read(data.data(),0,data.size(),&got)&&got==2*MiB&&state.source_reads==2&&state.usb_checks.empty());}
    // A changed path, reopened-file race, or changed opened handle cannot be
    // mistaken for the originally reviewed package, even at the same length.
    for(int change=0;change<8;++change){Reset();NetworkPackageSource source(&p,"nexusnet1:/","/game.nsp",size(),0);assert(!source.GetOpenResult());
        auto mutate=[&]{switch(change){case 0:++state.path_info.st_ino;break;case 1:++state.path_info.st_dev;break;case 2:++state.path_info.st_mtime;break;case 3:++state.path_info.st_ctime;break;case 4:++state.handle_info.st_ino;break;case 5:++state.handle_info.st_dev;break;case 6:++state.handle_info.st_size;break;case 7:state.handle_error=EIO;break;}};
        state.after_read=mutate;u8 data[17];u64 got{};assert(source.Read(data,0,sizeof(data),&got)!=0&&got==0&&state.opened==1);
    }
    Reset();state.begin_replace=true;{NetworkPackageSource source(&p,"nexusnet1:/","/game.nsp",size(),0);assert(source.GetOpenResult()==SourceChanged);}assert(state.opened==state.closed);
    // EOF, invalid oversized reply and network I/O are failures, without an
    // automatic reopen. Cancellation and destination loss keep their result.
    for(int fault=0;fault<6;++fault){Reset();NetworkPackageSource source(&p,"nexusnet1:/","/game.nsp",size(),42);u8 data[32];u64 got{};
        if(fault==0)state.zero=true;if(fault==1)state.oversize=true;if(fault==2)state.read_error=EIO;if(fault==3)state.after_read=[&]{p.cancel=777;};if(fault==4)state.after_read=[]{state.mounted=false;};if(fault==5)state.after_read=[]{state.usb_error=778;};
        const auto rc=source.Read(data,0,sizeof(data),&got);assert(rc&&got==0&&state.opened==1);assert(p.network_received==(fault>=3?sizeof(data):0));if(fault==3)assert(rc==777);if(fault==5)assert(rc==778);p.cancel=0;
    }
    Reset();{NetworkPackageSource source(&p,"nexusnet1:/","/game.nsp",size(),0);source.SignalCancel();u8 data{};u64 got{};assert(source.Read(&data,0,1,&got)==nexus::Unavailable&&state.source_reads==0);}
    Reset();{NetworkPackageSource source(&p,"nexusnet1:/","/game.nsp",size(),0);state.data_error=EIO;u8 data[32];u64 got{};assert(source.Read(data,0,sizeof(data),&got)!=0&&got==0&&p.network_received==sizeof(data));}assert(!p.network_active);
    // Every supported filename, including mixed case, reaches the existing
    // Yati dispatcher unchanged. No forced NSP container or forward-only mode.
    for(const auto* extension:{"nsp","NSP","nsz","NSz","xci","XCI","xcz","XCz"})for(auto target:{nexus::Target::Sd,nexus::Target::Usb}){
        Reset();const auto path=std::string("/folder/Game.")+extension;
        assert(!InstallNetworkPackage(&p,"nexusnet1:/",path,size(),target,target==nexus::Target::Usb?42:0,detail));
        assert(yati::observed_source_path==path&&!p.network_active);
    }
    // The backend selects exactly the requested physical destination, forces
    // source hashing and uses no SD/USB source receipt or staging copy.
    for(auto target:{nexus::Target::Sd,nexus::Target::Usb}){Reset();
        yati::install=[&](auto& source,auto*){std::vector<u8> data(300000);u64 got{};return source.Read(data.data(),0,data.size(),&got);};
        assert(!InstallNetworkPackage(&p,"nexusnet1:/","/game.NSP",size(),target,target==nexus::Target::Usb?42:0,detail));
        const auto& c=yati::observed_config;assert(c.nexus_target==target&&c.nexus_destination_generation==(target==nexus::Target::Usb?42:0));
        assert(c.quick_usb_final_verify==(target==nexus::Target::Usb));
        assert(c.skip_nca_hash_verify==false&&c.convert_to_standard_crypto==false&&c.lower_master_key==false&&c.lower_system_version==false&&c.ignore_distribution_bit==false&&c.receipt==nullptr);
        assert(c.ticket_only==false&&c.skip_base==false&&c.skip_patch==false&&c.skip_addon==false&&c.skip_data_patch==false&&c.skip_ticket==false);
        assert(state.installs==1&&state.destination==target&&state.opened==1&&state.closed==1);assert(p.network_received==300000&&!p.network_active);
        assert(detail.completion.find(target==nexus::Target::Sd?"SD card":"USB drive")!=std::string::npos);
    }
    for(auto target:{nexus::Target::Sd,nexus::Target::Usb})for(bool partial:{false,true}){Reset();state.published=partial?1:0;state.skipped=1;
        assert(!InstallNetworkPackage(&p,"nexusnet1:/","/game.nsp",size(),target,target==nexus::Target::Usb?42:0,detail));
        assert(detail.completion.find(partial?"Installed 1 game/update/DLC entries":"No content was installed")!=std::string::npos);
        assert(detail.completion.find(partial?"Skipped 1 entries":"1 entries were skipped")!=std::string::npos);
        if(!partial)assert(detail.completion.find("directly from the NAS to")==std::string::npos);
    }
    Reset();state.published=0;assert(!InstallNetworkPackage(&p,"nexusnet1:/","/game.nsp",size(),nexus::Target::Sd,0,detail));assert(detail.completion.find("No installable game/update/DLC entries")!=std::string::npos);
    // A queue consumes structured per-package publication counts, including
    // partial success before a failure; it must never infer these from prose.
    NetworkInstallOutcome outcome{99,99,true};
    Reset();state.published=2;state.skipped=1;
    assert(!InstallNetworkPackage(&p,"nexusnet1:/","/game.nsp",size(),nexus::Target::Usb,42,detail,&outcome));
    assert(outcome.attempted&&outcome.installed_records==2&&outcome.skipped_records==1);
    Reset();state.published=0;state.skipped=3;
    assert(!InstallNetworkPackage(&p,"nexusnet1:/","/game.nsp",size(),nexus::Target::Sd,0,detail,&outcome));
    assert(outcome.attempted&&!outcome.installed_records&&outcome.skipped_records==3);
    Reset();state.published=0;
    assert(!InstallNetworkPackage(&p,"nexusnet1:/","/game.nsp",size(),nexus::Target::Sd,0,detail,&outcome));
    assert(outcome.attempted&&!outcome.installed_records&&!outcome.skipped_records);
    Reset();outcome={99,99,true};
    assert(InstallNetworkPackage(&p,"sdmc:/","/game.nsp",size(),nexus::Target::Sd,0,detail,&outcome));
    assert(!outcome.attempted&&!outcome.installed_records&&!outcome.skipped_records);
    Reset();state.lstat_error=EIO;outcome={99,99,true};
    assert(InstallNetworkPackage(&p,"nexusnet1:/","/game.nsp",size(),nexus::Target::Sd,0,detail,&outcome));
    assert(!outcome.attempted&&!outcome.installed_records&&!outcome.skipped_records);
    for(bool cancel:{false,true}){
        Reset();yati::install=[&](auto&,auto* progress){
            assert(yati::observed_config.outcome);
            *yati::observed_config.outcome={1,2};
            if(cancel)progress->cancel=912;
            return Result(912);
        };
        assert(InstallNetworkPackage(&p,"nexusnet1:/","/game.nsp",size(),nexus::Target::Usb,42,detail,&outcome)==912);
        assert(outcome.attempted&&outcome.installed_records==1&&outcome.skipped_records==2);
        assert(detail.text.find("some content may already be installed")!=std::string::npos);
        p.cancel=0;
    }
    // Explicit link-only reaches the same NAS adapter, never physical SD.
    Reset();assert(!InstallNetworkPackage(&p,"nexusnet1:/","/game.nsp",size(),nexus::Target::Usb,42,detail,&outcome,true));
    assert(yati::observed_config.reuse_existing_usb_only&&!yati::observed_config.receipt);
    Reset();assert(InstallNetworkPackage(&p,"nexusnet1:/","/game.nsp",size(),nexus::Target::Sd,0,detail,&outcome,true));assert(!state.installs);
    // Refuse unsafe/unreviewed selections before invoking the installer.
    for(int fault=0;fault<12;++fault){Reset();auto mount=std::string("nexusnet1:/"),path=std::string("/game.nsp");auto target=nexus::Target::Sd;u64 gen{};auto expected=size();
        switch(fault){case 0:mount="sdmc:/";break;case 1:path="/../game.nsp";break;case 2:path="/game.zip";break;case 3:expected++;break;case 4:target=nexus::Target::Usb;break;case 5:target=nexus::Target::Internal;break;case 6:state.applet=true;break;case 7:state.physical_error=802;break;case 8:state.pending_error=803;break;case 9:state.readonly=false;break;case 10:state.nostat=true;break;case 11:state.foreground=false;break;}
        assert(InstallNetworkPackage(&p,mount,path,expected,target,gen,detail)!=0&&state.installs==0);assert(detail.text.find("No installation was started")!=std::string::npos);
    }
    // A NAS failure does not claim success, discard the install diagnostics,
    // retarget to SD, or manufacture a resumable local-source receipt.
    Reset();yati::install=[&](auto& source,auto*){state.read_error=EIO;u8 data[16];u64 got{};return source.Read(data,0,sizeof(data),&got);};
    assert(InstallNetworkPackage(&p,"nexusnet1:/","/game.nsp",size(),nexus::Target::Usb,42,detail)!=0);
    assert(state.destination==nexus::Target::Usb&&state.opened==1&&state.closed==1);
    assert(detail.text.find("do not resume automatically")!=std::string::npos&&detail.text.find("same game file")!=std::string::npos&&detail.text.find("Reading the game file")!=std::string::npos);
    std::cout<<"NAS direct-install adapter tests passed: one-MiB boundaries, four reads per four MiB, reused client with live generation checks, short reads, cancellation and errors\n";
}
