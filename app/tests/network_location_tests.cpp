// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/network_location_core.hpp"
#include <cassert>
#include <iostream>
using nexus::network::Connection;
using nexus::network::Valid;
using nexus::network::Url;
int main(){
    Connection smb{"Office NAS","SMB","192.168.1.12","Games","player","WORKGROUP"};
    assert(Valid(smb));assert(Url(smb)=="smb://192.168.1.12/Games");
    assert(Url(smb).find(smb.user)==std::string::npos);assert(Url(smb).find(smb.domain)==std::string::npos);
    Connection nfs{"Server","NFS","nas.local","/exports/games","",""};
    assert(Valid(nfs));assert(Url(nfs)=="nfs://nas.local/exports/games");
    for(const auto& host:{"smb://server","user:password@server","server/path","server\npassword=value",""}){auto c=smb;c.host=host;assert(!Valid(c));assert(Url(c).empty());}
    for(const auto& name:{"x]\n[other","a=b","[section]","bad;section",""}){auto c=smb;c.name=name;assert(!Valid(c));}
    for(const auto& share:{"../games","Games/child",".","..","/Games","Games?password=secret","Games\\child"}){auto c=smb;c.share=share;assert(!Valid(c));}
    for(const auto& share:{"exports/games","/exports/../games","/exports//games","/exports/games/","/exports/games?version=3"}){auto c=nfs;c.share=share;assert(!Valid(c));}
    auto c=smb;c.user="user\npass=secret";assert(!Valid(c));c=smb;c.domain="domain\rvalue";assert(!Valid(c));
    c=smb;c.protocol="FTP";assert(!Valid(c));c=smb;c.share="Games and ports";assert(Valid(c));
    c=nfs;c.share="/";assert(Valid(c));c=smb;c.host=std::string(254,'a');assert(!Valid(c));
    std::cout<<"Network connection validation passed\n";
}
