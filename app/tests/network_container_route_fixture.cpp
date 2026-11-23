// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/package.hpp"
#include "nexus/localize.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <strings.h>
using Result=std::uint32_t;using s64=std::int64_t;using u64=std::uint64_t;
constexpr Result Result_YatiContainerNotFound=987;
constexpr Result Result_XciInvalidHeader=988;
namespace nexus::diagnostics{struct Operation{explicit Operation(const char*){}};}
namespace nexus {inline Result logged{},reported{};void Log(const char*,Result r,u64=0){logged=r;}namespace reports{void QueueFailure(const char*,Result r){reported=r;}}}
#define R_UNLESS(condition,result) do{if(!(condition))return result;}while(false)
namespace fs{struct FsPath{const char* s;explicit FsPath(const char* path):s(path){}};}
namespace sphaira::ui{struct ProgressBox{bool cancel{};bool ShouldExit(){return cancel;}};}
namespace sphaira::yati{
namespace source{struct Base{bool HasJoinedParts(){return false;}};}
struct Diagnostic{std::string text;void Clear(){text.clear();}};struct ConfigOverride{Diagnostic* diagnostic{};};
namespace container{
struct Base{source::Base* source;char kind;s64 size;Base(source::Base* source,char kind,s64 size):source(source),kind(kind),size(size){}virtual ~Base()=default;};
struct Nsp:Base{Nsp(source::Base* source,s64 size):Base(source,'P',size){}};
struct Xci:Base{Xci(source::Base* source,s64 size):Base(source,'X',size){}};
}
namespace msp{Result Install(ui::ProgressBox*,source::Base*,s64){assert(false);return 1;}}
char selected{};source::Base* seen{};s64 seen_size{};
Result InstallFromContainer(ui::ProgressBox*,container::Base* container,const ConfigOverride&){selected=container->kind;seen=container->source;seen_size=container->size;return 0;}
// ROUTE_PRODUCTION
}
int main(){
    using namespace sphaira;ui::ProgressBox box;yati::source::Base source;
    for(const auto* filename:{"/share/game.nsp","/share/game.NSP","/share/game.nsz","/share/game.NsZ","/share/game.xci","/share/game.XCI","/share/game.xcz","/share/game.XcZ"}){
        assert(nexus::package::Supported(filename));
        assert(!yati::InstallFromSource(&box,&source,fs::FsPath(filename),{},123456));
        assert(yati::seen==&source&&yati::seen_size==123456);
        assert(!nexus::logged&&!nexus::reported);
        const auto kind=nexus::package::Classify(filename);const bool cartridge=kind==nexus::package::Kind::Xci||kind==nexus::package::Kind::Xcz;
        assert(yati::selected==(cartridge?'X':'P'));
    }
    for(const auto* filename:{"/share/game.zip","/share/game.nro","/share/noextension"}){
        yati::selected=0;assert(yati::InstallFromSource(&box,&source,fs::FsPath(filename),{},123456)==Result_YatiContainerNotFound&&yati::selected==0);
        assert(nexus::logged==Result_YatiContainerNotFound&&nexus::reported==Result_YatiContainerNotFound);
    }
    box.cancel=true;nexus::reported=0;
    assert(yati::InstallFromSource(&box,&source,fs::FsPath("noextension"),{},0)==Result_YatiContainerNotFound);
    assert(nexus::logged==Result_YatiContainerNotFound&&!nexus::reported);
    std::cout<<"Actual Yati network filename routing: NSP/NSZ and XCI/XCZ containers passed\n";
}
