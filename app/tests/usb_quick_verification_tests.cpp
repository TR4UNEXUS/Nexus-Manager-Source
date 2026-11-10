// Platform/hash outcomes are controlled by the shared fixture. Header and
// exact-output digests use real OpenSSL SHA-256; verification routing is real.
void Fresh(Yati& yati){Reset();cancel_after_verify=false;corrupt_header_id=0;register_result=0;storage_has=true;storage_size=0;yati.usb_verified={};yati.quick_usb_final_verify=true;yati.usb_generation=1;}
void Written(Yati& yati,NcaCollection& file){
    file.placeholder_owned=true;
    file.usb_written.key=VerificationKey(file,yati.usb_generation);
    std::vector<u8> header(sizeof(nca::Header),file.content_id.bytes[0]);
    sha256CalculateHash(file.usb_written.header.data(),header.data(),header.size());
    file.usb_written.header_known=file.usb_written.source_matched=file.usb_written.complete=file.usb_written.flushed=true;
}
u64 BytesRead(){u64 bytes{};for(const auto& e:events)if(e.kind=="read")bytes+=e.total;return bytes;}
unsigned Hashes(){return std::count_if(events.begin(),events.end(),[](const auto& e){return e.kind=="hash";});}
int main(){
    ProgressBox box;Destination destination;Yati yati{&box,&destination};
    auto file=File(11,3*MiB);Fresh(yati);Written(yati,file);
    assert(!yati.PublishNca(file));assert(BytesRead()==sizeof(nca::Header)&&Hashes()==0&&read_calls==1);
    assert(yati.usb_verified.Find(VerificationKey(file,1)));file.skipped=true;
    Reset();assert(!yati.PublishNca(file));assert(BytesRead()==sizeof(nca::Header)&&Hashes()==0);

    // Missing or failed source/write/flush evidence cannot enable quick mode.
    for(unsigned missing=0;missing<4;++missing){Fresh(yati);file=File(12,2*MiB);Written(yati,file);
        if(missing==0)file.usb_written.header_known=false;if(missing==1)file.usb_written.source_matched=false;
        if(missing==2)file.usb_written.complete=false;if(missing==3)file.usb_written.flushed=false;
        assert(!file.usb_written.Matches(VerificationKey(file,1)));assert(!yati.PublishNca(file));assert(BytesRead()==file.expected_size&&Hashes()==1);
    }
    // Existing reused bytes get exactly one full pass per matching proof.
    Fresh(yati);file=File(13,3*MiB,true);assert(!yati.PublishNca(file));assert(BytesRead()==file.expected_size&&Hashes()==1);
    Reset();assert(!yati.PublishNca(file));assert(BytesRead()==sizeof(nca::Header)&&Hashes()==0);
    // Another install owns a separate cache; default/import mode never uses it.
    Reset();Yati another{&box,&destination};another.quick_usb_final_verify=true;assert(!another.PublishNca(file));assert(BytesRead()==file.expected_size&&Hashes()==1);
    for(unsigned repeat=0;repeat<2;++repeat){Reset();yati.quick_usb_final_verify=false;assert(!yati.PublishNca(file));assert(BytesRead()==file.expected_size&&Hashes()==1);}

    // Stronger/different CNMT expectations invalidate cached evidence.
    Fresh(yati);file=File(14,2*MiB,true);file.has_expected_hash=false;assert(!yati.PublishNca(file));
    Reset();file.has_expected_hash=true;assert(!yati.PublishNca(file));assert(BytesRead()==file.expected_size&&Hashes()==1);
    Reset();file.expected_hash[31]=1;assert(!yati.PublishNca(file));assert(BytesRead()==file.expected_size&&Hashes()==1);
    Reset();file.verified_alias=true;assert(!yati.PublishNca(file));assert(BytesRead()==file.expected_size&&Hashes()==1);

    // A generation change cannot reuse previous proof; a live generation
    // failure, header mismatch, size failure or cancellation aborts and erases it.
    Fresh(yati);file=File(15,2*MiB,true);assert(!yati.PublishNca(file));
    Reset();yati.usb_generation=2;assert(!yati.PublishNca(file));assert(BytesRead()==file.expected_size&&Hashes()==1);
    for(unsigned failure=0;failure<6;++failure){Fresh(yati);file=File(16,2*MiB,true);assert(!yati.PublishNca(file));Reset();
        if(failure==0)corrupt_header_id=16;if(failure==1)validate_fail_at=1;if(failure==2)verify_fail_id=16;
        if(failure==3)cancel_after_verify=true;if(failure==4)read_fail_at=1;if(failure==5)++file.header.size;
        assert(yati.PublishNca(file));assert(!yati.usb_verified.Find(VerificationKey(file,1)));
        assert(std::none_of(events.begin(),events.end(),[](const auto& e){return e.kind=="UpdateTransfer"&&e.total&&e.done==e.total;}));
    }
    // Hash failure after a full reused read must not produce cache evidence.
    Fresh(yati);file=File(17,2*MiB,true);hash_fail_id=17;assert(yati.PublishNca(file)==HashFailure);assert(!yati.usb_verified.Find(VerificationKey(file,1)));

    // A conflicting registration cannot use a valid new-write proof. It needs
    // one physical pass and must match both exact output and original identity.
    Fresh(yati);file=File(18,2*MiB);Written(yati,file);register_result=nexus::registration::ContentAlreadyExists;storage_size=file.expected_size;
    std::vector<u8> bytes(file.expected_size,18);sha256CalculateHash(file.hash,bytes.data(),bytes.size());
    assert(!yati.PublishNca(file));assert(BytesRead()==file.expected_size&&Hashes()==1);
    Reset();file.hash[0]^=1;assert(yati.PublishNca(file)==nexus::registration::ContentAlreadyExists);assert(BytesRead()==file.expected_size&&!yati.usb_verified.Find(VerificationKey(file,1)));
    Fresh(yati);file=File(19,2*MiB);Written(yati,file);register_result=PublishFailure;
    assert(yati.PublishNca(file)==PublishFailure&&BytesRead()==0&&!yati.usb_verified.Find(VerificationKey(file,1)));

    // Every exact key component participates in matching; cache exhaustion
    // restores full checking and does not fail a legitimate content request.
    Fresh(yati);file=File(20,2*MiB);Written(yati,file);const auto key=VerificationKey(file,1);
    for(unsigned field=0;field<7;++field){auto changed=key;if(field==0)++changed.content[0];if(field==1)++changed.expected[31];if(field==2)++changed.generation;if(field==3)++changed.size;if(field==4)changed.expected_bytes=16;if(field==5)changed.plaintext_alias=true;if(field==6)changed.generation=0;assert(!file.usb_written.Matches(changed));}
    nexus::verification::Cache bounded;for(unsigned i=0;i<4097;++i){auto next=key;memcpy(next.content.data(),&i,sizeof(i));bounded.Remember(next,file.usb_written.header);if(i==4096)assert(!bounded.Find(next));}
    std::cout<<"Actual USB quick verification: streamed-write evidence, full reused hash once, exact expectation binding, default isolation, header/size/generation/cancel faults, collision and bounded fallback passed\n";
}
