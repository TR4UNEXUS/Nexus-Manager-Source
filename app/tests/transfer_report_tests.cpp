// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/transfer_report.hpp"
#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace sphaira::nexusapp;
static unsigned assertions{};
static void Check(bool value) {
    ++assertions;
    if(!value){std::cerr<<"Failed assertion "<<assertions<<'\n';std::exit(1);}
}
static bool Has(std::string_view message,std::string_view text){return message.find(text)!=message.npos;}

int main(){
    // Includes cancellation before the first copy request, read/write errors,
    // failed readback, publication failure and cancellation between phases.
    for(const char* stage:{"Preparing the copy","Checking for cancellation","Reading the source file",
            "Writing the destination file","Comparing the copied bytes and destination",
            "Publishing the verified game to HOME"}){
        TransferReport r;r.move=true;r.copy.At(stage,"test-event");
        const auto message=r.Message();
        Check(Has(message,std::string("Stopped during: ")+stage));
        Check(Has(message,"The original was not removed"));
        Check(Has(message,"destination may be incomplete"));
        Check(!Has(message,"Use the destination copy"));
        Check(!Has(message,"Move complete"));
        Check(Has(message,"switch/nexusmanager/nexus.log"));
    }
    {TransferReport r;r.copy.published=true;r.copy.At("Refreshing the HOME icon","move-home-refresh");
        const auto message=r.Message();
        Check(Has(message,"Files copied and verified"));
        Check(Has(message,"Refreshing the HOME icon"));
        Check(Has(message,"The original was not removed"));
        Check(Has(message,"verified destination was kept"));
        Check(!Has(message,"destination may be incomplete"));
        Check(!Has(message,"Move complete"));}
    for(const char* stage:{"Preparing source cleanup","Check another record for shared files",
            "Confirm destination launch records","Validate physical source deletion paths"}){
        TransferReport r;r.move=true;r.phase=TransferPhase::Remove;r.removal.stage=stage;
        const auto message=r.Message();
        Check(Has(message,std::string("Stopped during: ")+stage));
        Check(Has(message,"destination copy was verified and kept"));
        Check(Has(message,"The original was not removed. Both copies were kept"));
        Check(!Has(message,"original may now be incomplete"));
        Check(!Has(message,"trying again"));}
    // Zero removed files does not establish that source cleanup made no change:
    // internal metadata removal may precede the first file, or an IPC may fail
    // after a mutation. The explicit mutation-started flag controls the message.
    for(std::size_t count:{0U,1U,10U}){
        TransferReport r;r.move=true;r.phase=TransferPhase::Remove;
        r.removal.removal_started=true;r.removal.removed=count;
        r.removal.stage=count?"Remove verified source file":"Remove original internal metadata";
        const auto message=r.Message();
        Check(Has(message,r.removal.stage));
        Check(Has(message,"original may now be incomplete"));
        Check(Has(message,"Use the destination copy"));
        Check(Has(message,"Do not delete leftover files manually or start another move"));
        Check(!Has(message,"original was not removed"));
        Check(!Has(message,"Both copies were kept"));
        Check(!Has(message,"Move complete"));
        Check(Has(message,"save data is unchanged"));
    }
    {TransferReport r;r.phase=TransferPhase::Complete;
        const auto message=r.Message();
        Check(Has(message,"Copy complete. Both copies were kept"));
        Check(!Has(message,"original game files were removed"));
        Check(!Has(message,"Stopped"));}
    for(bool shared:{false,true}){
        TransferReport r;r.move=true;r.phase=TransferPhase::Complete;r.removal.shared=shared;
        const auto message=r.Message();
        Check(Has(message,"Move complete. The game is now on the destination"));
        Check(Has(message,"save data is unchanged"));
        Check(Has(message,"Files also used by other games were kept")==shared);
        Check(Has(message,"original game files were removed")==!shared);
        Check(!Has(message,"Stopped"));
    }
    std::cout<<"Transfer report: "<<assertions<<" safe-conclusion and stage checks passed\n";
}
