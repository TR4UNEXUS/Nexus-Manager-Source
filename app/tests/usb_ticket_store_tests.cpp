int main(){
    using namespace sphaira::nexusapp;
    FsRightsId rights{};rights.c[0]=123;rights.c[15]=8;
    std::vector<u8> ticket(32,9),cert(64,7);memcpy(ticket.data(),&rights,sizeof(rights));
    UsbTicketPair out;
    const auto before_scan=writes;
    assert(LoadUsbTicketPair(42,rights,out)==MissingUsbTicket && writes==before_scan);
    assert(SaveUsbTicketPair(42,rights,ticket,cert)==0);
    assert(files.size()==1);
    const auto path=files.begin()->first;const auto saved=files.begin()->second;
    assert(path.ends_with(".bin") && path.starts_with("/nexus/tickets/v1/"));
    const auto after_save=writes;
    assert(LoadUsbTicketPair(42,rights,out)==0 && out.ticket==ticket && out.certificate==cert && writes==after_save);
    // Re-installing must preserve a validated earlier original.
    auto alternate=ticket;alternate.back()^=1;
    assert(SaveUsbTicketPair(42,rights,alternate,cert)==0 && files[path]==saved && writes==after_save);
    // A complete bundle is scoped to its source console and exact rights ID.
    ++current_console;assert(LoadUsbTicketPair(42,rights,out)==MissingUsbTicket);--current_console;
    auto other=rights;other.c[1]=1;assert(LoadUsbTicketPair(42,other,out)==MissingUsbTicket);
    auto invalid_ticket=ticket;invalid_ticket[0]^=1;
    assert(SaveUsbTicketPair(42,rights,invalid_ticket,cert)==InvalidUsbTicket);
    // Every region is checksum covered, and a partial pair is never accepted.
    for(size_t offset:{size_t(64),size_t(96),saved.size()-1}){
        files[path]=saved;files[path][offset]^=1;
        assert(LoadUsbTicketPair(42,rights,out)==InvalidUsbTicket && out.ticket.empty());
    }
    files[path]=saved;files[path].pop_back();assert(LoadUsbTicketPair(42,rights,out)==InvalidUsbTicket);
    files[path]=saved;
    // Connection errors are not downgraded into a missing-ticket diagnosis.
    transport_error=12345;assert(LoadUsbTicketPair(42,rights,out)==12345);transport_error=0;
    assert(LoadUsbTicketPair(43,rights,out)==77);
    assert(ImportUsbTicketPair(42,rights)==0 && imports==1);
    import_error=4321;assert(ImportUsbTicketPair(42,rights)==4321);import_error=0;
    readback_registered=false;assert(ImportUsbTicketPair(42,rights)==InvalidUsbTicket);readback_registered=true;
    // A failed publication leaves only an ignored partial bundle, never a
    // canonical pair which preview could mistakenly consider available.
    auto second=rights;second.c[3]=2;memcpy(ticket.data(),&second,sizeof(second));
    rename_error=9876;assert(SaveUsbTicketPair(42,second,ticket,cert)==9876);rename_error=0;
    assert(LoadUsbTicketPair(42,second,out)==MissingUsbTicket);
    assert(files[path]==saved);
    assert(SaveUsbTicketPair(42,second,ticket,cert)==0);
    assert(LoadUsbTicketPair(42,second,out)==0 && out.ticket==ticket);
    write_error=4567;auto third=second;third.c[3]=3;memcpy(ticket.data(),&third,sizeof(third));
    assert(SaveUsbTicketPair(42,third,ticket,cert)==4567);write_error=0;
    assert(LoadUsbTicketPair(42,third,out)==MissingUsbTicket);
    files.erase(path);
    Header h;h.magic=0x3150434e;h.rights=rights;
    std::memcpy(ticket.data(),&rights,sizeof(rights));
    h.ticket_size=ticket.size();h.certificate_size=cert.size();Checksum(h,ticket,cert);
    std::vector<u8> pc(sizeof(h));std::memcpy(pc.data(),&h,sizeof(h));
    pc.insert(pc.end(),ticket.begin(),ticket.end());pc.insert(pc.end(),cert.begin(),cert.end());
    const auto handoff="/nexus/pc-import/v1/tickets/"+Hex(&rights,sizeof(rights))+".bin";
    // A connected drive's session belongs to its client. A fresh lookup must
    // open its own session even if the same-console archive lookup just succeeded.
    nexus::Client unopened;nexus::Info info{};
    assert(unopened.Stat(42,handoff,info)==0x1759);
    const auto publication_writes=writes;
    const auto no_pair=LoadUsbImportTicketPair(42,rights,out);
    if(no_pair!=MissingUsbTicket)std::cerr<<"Missing PC ticket lookup returned "<<std::hex<<no_pair<<"\n";
    assert(no_pair==MissingUsbTicket&&out.ticket.empty()&&writes==publication_writes);
    files[handoff]=pc;
    assert(LoadUsbTicketPair(42,rights,out)==MissingUsbTicket);
    assert(LoadUsbImportTicketPair(42,rights,out)==0 && out.ticket==ticket && out.certificate==cert);
    assert(ImportUsbTicketPair(42,rights)==0);
    assert(LoadUsbImportTicketPair(43,rights,out)==77 && out.ticket.empty());
    nexus::hats::supported=false;assert(LoadUsbImportTicketPair(42,rights,out)==InvalidUsbTicket);nexus::hats::supported=true;
    import_error=123;assert(ImportUsbTicketPair(42,rights)==123);import_error=0;
    files[handoff].back()^=1;assert(LoadUsbImportTicketPair(42,rights,out)==InvalidUsbTicket && out.ticket.empty());
    files[path]=saved;assert(LoadUsbImportTicketPair(42,rights,out)==0);
    files[handoff]=pc;files[path].back()^=1;assert(LoadUsbImportTicketPair(42,rights,out)==InvalidUsbTicket);
    std::cout<<"USB ticket archive and PC handoff fault tests passed\n";
}
