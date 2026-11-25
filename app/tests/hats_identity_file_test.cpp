// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/hats_identity.hpp"
#include <cassert>
#include <fstream>
#include <cstdio>
int main(int argc,char** argv){
    assert(argc==3);std::ifstream f(argv[1]);assert(f.good());std::string text((std::istreambuf_iterator<char>(f)),{});
    nexus::hats::Identity id;assert(nexus::hats::ReadIdentity(text,id)&&nexus::hats::MatchesPack(id,id.tag,argv[2]));
    assert(id.sequence==1&&id.version=="NX1");printf("PASS: %s: %s / %s\n",argv[2],id.version.c_str(),id.edition.c_str());
}
