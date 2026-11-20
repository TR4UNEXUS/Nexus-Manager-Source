"""Compile the actual forwarder RomFS builder and embedded HBL reader on the host.

No model implementation of either is used: isolated functions are extracted from
the production sources, with only the filesystem read transport substituted.
"""
import argparse
from pathlib import Path
import subprocess

APP = Path(__file__).resolve().parents[1]


def between(text, begin, end):
    assert text.count(begin) == 1 and text.count(end) == 1
    return text.split(begin, 1)[1].split(end, 1)[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="C:/devkitPro/msys2/usr/bin/g++.exe")
    parser.add_argument("--libnx", type=Path, default=APP.parent / "fw23-build-deps/libnx")
    args = parser.parse_args()
    source = (APP / "sphaira/source/owo.cpp").read_text()
    hbl = (APP / "hbl/source/main.c").read_text()
    header = (args.libnx / "include/switch/runtime/devices/romfs_dev.h").read_text()
    wire = between(header, "/// RomFS header.", "/**\n * @brief Mounts the Application's RomFS.")
    buf = "struct BufHelper {" + between(source, "struct BufHelper {", "struct NcaEntry {")
    build = "typedef struct romfs_dirent_ctx {" + between(
        source, "typedef struct romfs_dirent_ctx {", "auto npdm_patch_kc(")
    reader = "static const romfs_file* findRomfsFile(" + between(
        hbl, "static const romfs_file* findRomfsFile(", "static Result restoreProgramPath(")
    out = APP / "build/homebrew-tests"
    out.mkdir(parents=True, exist_ok=True)
    unit = out / "forwarder_romfs_generated.cpp"
    unit.write_text("""
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <string>
#include <span>
#include <stdexcept>
#include <iostream>
#include <random>
#include <set>
#include <fstream>
using u8=uint8_t;using u32=uint32_t;using u64=uint64_t;using Result=u32;
constexpr u32 IVFC_HASH_BLOCK_SIZE=0x4000, ROMFS_ENTRY_EMPTY=UINT32_MAX, ROMFS_FILEPARTITION_OFS=0x200;
struct FileEntry {std::string name;std::vector<u8> data;};
using FileEntries=std::vector<FileEntry>;
#define R_SUCCEEDED(rc) ((rc)==0)
struct FsStorage {const std::vector<u8>* bytes;};
Result fsStorageRead(FsStorage* s,u64 off,void* out,u64 size){
    if(off>s->bytes->size() || size>s->bytes->size()-off)return 1;
    std::memcpy(out,s->bytes->data()+off,size);return 0;
}
""" + wire + buf + build + reader + r"""
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
void check(const FileEntries& entries, const char* fixture=nullptr){
    u64 size{};const auto image=romfs_build(entries,&size);
    const auto* h=reinterpret_cast<const romfs_header*>(image.data());
    require(h->headerSize==80 && sizeof(romfs_file)==32 && sizeof(romfs_dir)==24,"wire layout");
    require(size<=image.size() && h->fileTableOff+h->fileTableSize<=size,"table bounds");
    const auto* root=reinterpret_cast<const romfs_dir*>(image.data()+h->dirTableOff);
    const auto* table=image.data()+h->fileTableOff;
    FsStorage storage{&image};
    std::vector<std::pair<u64,u64>> ranges;
    for(const auto& e:entries){
        const auto name=e.name.substr(1);
        const auto* file=findRomfsFile(table,h->fileTableSize,root,name.c_str());
        require(file!=nullptr,"missing "+name);
        require(file->dataSize==e.data.size(),name+": stored size "+std::to_string(file->dataSize)+" != actual "+std::to_string(e.data.size()));
        const auto begin=h->fileDataOff+file->dataOff,end=begin+file->dataSize;
        require(begin>=h->fileDataOff && end<=h->dirHashTableOff,"data/table overlap: "+name);
        for(const auto& r:ranges)require(begin>=r.second||end<=r.first,"file overlap: "+name);
        if(begin!=end)ranges.emplace_back(begin,end);
        std::vector<u8> read(e.data.size()+1,0xFE);
        require(readRomfsFile(&storage,h,table,h->fileTableSize,root,name.c_str(),read.data(),e.data.size()),"HBL read: "+name);
        require(!std::memcmp(read.data(),e.data.data(),e.data.size()) && read.back()==0xFE,"byte mismatch: "+name);
        // Also verify hash-chain lookup, independently of HBL's sibling walk.
        const auto* buckets=reinterpret_cast<const u32*>(image.data()+h->fileHashTableOff);
        u32 off=buckets[calc_path_hash(0,(const u8*)name.data(),0,name.size())%(h->fileHashTableSize/4)];
        std::set<u32> seen;bool found=false;
        while(off!=UINT32_MAX){
            require(off+sizeof(romfs_file)<=h->fileTableSize && seen.insert(off).second,"bad hash chain");
            const auto* f=reinterpret_cast<const romfs_file*>(table+off);
            require(off+sizeof(*f)+f->nameLen<=h->fileTableSize,"hash filename bounds");
            if(f==file)found=true;
            off=f->nextHash;
        }
        require(found,"hash lookup: "+name);
    }
    require(!findRomfsFile(table,h->fileTableSize,root,"missing"),"absent marker");
    if(fixture){std::ofstream f(fixture,std::ios::binary);f.write((const char*)image.data(),image.size());require(bool(f),"fixture write");}
}
std::vector<u8> bytes(const std::string& s){return {s.begin(),s.end()};}
int main(int argc,char** argv){try{
    const std::string logical="sdmc:/switch/nfsmw-nx/nfsmw-nx.nro";
    FileEntries usb{{"/nextArgv",bytes("\""+logical+"\"")},{"/nextNroPath",bytes(logical)},
        {"/nexusUsbPath",bytes("/switch/nfsmw-nx/nfsmw-nx.nro")},{"/nexusNroHash",std::vector<u8>(32,0xAB)}};
    check(usb,argc>1?argv[1]:nullptr);
    check({usb[0],usb[1]}); // ordinary HOME app, two files
    check({{"/control.nacp",std::vector<u8>(0x4000,0x4E)},{"/icon_AmericanEnglish.dat",std::vector<u8>(17431,0xDA)}});
    std::mt19937 rng(601);
    for(unsigned trial=0;trial<2000;++trial){FileEntries entries;
        for(unsigned n=1+rng()%12,i=0;i<n;++i){
            std::string name="/file-"+std::to_string(i)+std::string(rng()%60,'x');
            std::vector<u8> data(rng()%4096);for(auto& b:data)b=rng();
            entries.push_back({name,data});
        }
        check(entries);
    }
    std::cout<<"Production forwarder builder + production HBL reader: USB four-file, ordinary two-file, control/icon and 2000 variable-size round trips passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
""")
    exe = out / "forwarder_romfs_tests.exe"
    subprocess.run([args.compiler, "-std=c++20", "-O1", "-Wall", "-Wextra", str(unit), "-o", str(exe)], check=True)
    subprocess.run([str(exe), str(out / "usb-forwarder.romfs")], check=True)


if __name__ == "__main__":
    main()
