// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/network_source.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
int main(){
    char pattern[]="/tmp/nexus-network-source-XXXXXX";auto* created=mkdtemp(pattern);assert(created);
    const std::filesystem::path root(created);assert(root.string().starts_with("/tmp/nexus-network-source-"));
    struct Cleanup {std::filesystem::path root;~Cleanup(){std::filesystem::remove_all(root);}} cleanup{root};
    std::filesystem::create_directory(root/"folder");std::ofstream(root/"folder"/"game.nsp",std::ios::binary)<<"0123456789";
    std::filesystem::create_symlink("folder/game.nsp",root/"link");
    std::filesystem::create_directory_symlink("folder",root/"folder-link");
    bool active=true;nexus::network::Source source(root.string()+"/",[&]{return active;});
    nexus::files::Entry entry;bool exists{};std::vector<std::string> names;
    assert(!source.List("/",names));assert(names.size()==3);
    assert(!source.Stat("/folder/game.nsp",entry,exists));assert(exists&&!entry.directory&&entry.size==10);
    assert(!source.Stat("/missing",entry,exists)&&!exists);
    assert(source.Stat("/link",entry,exists)==ENOTSUP);assert(source.List("/folder-link",names)==ENOTSUP);
    assert(source.List("/missing",names)==ENOENT);assert(source.List("/folder/game.nsp",names)==ENOTDIR);
    assert(source.List("/../",names)==EINVAL);
    char data[20]{};std::uint64_t actual{};
    assert(!source.Read("/folder/game.nsp",2,data,4,actual));assert(actual==4&&std::string(data,4)=="2345");
    assert(!source.Read("/folder/game.nsp",8,data,8,actual));assert(actual==2&&std::string(data,2)=="89");
    source.EndRead();
    assert(source.Read("/missing",0,data,1,actual)==ENOENT&&actual==0);
    assert(source.Read("/link",0,data,1,actual)==ENOTSUP);
    assert(source.Read("/folder",0,data,1,actual)==EISDIR);
    assert(source.Read("/folder/game.nsp",nexus::files::MaxBytes, data,1,actual)==EINVAL);
    active=false;assert(source.Read("/folder/game.nsp",0,data,1,actual)==ECANCELED);assert(source.List("/",names)==ECANCELED);
    active=true;std::filesystem::remove(root/"folder"/"game.nsp");
    assert(source.Read("/folder/game.nsp",0,data,1,actual)==ENOENT);
    assert(std::distance(std::filesystem::directory_iterator(root),std::filesystem::directory_iterator{})==3);
    std::cout<<"Network source: listing, short reads, missing/disappeared files, links, traversal and cancellation passed\n";
}
