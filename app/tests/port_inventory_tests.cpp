// Read-only integration check: no write/copy/remove implementation exists here.
#include "nexus/port_transfer_core.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
using namespace nexus::port;
struct ReadOnly {
    using Result=unsigned;
    static constexpr Result Invalid=1;
    fs::path root;
    Result Check(){return 0;}
    fs::path Map(const std::string& path){return root/fs::path(path).relative_path();}
    Result Stat(bool,const std::string& path,Entry& entry,bool& exists){
        const auto file=Map(path);const auto status=fs::symlink_status(file);
        exists=fs::exists(status);
        if(!exists)return 0;
        if(fs::is_symlink(status)||(!fs::is_regular_file(status)&&!fs::is_directory(status)))return Invalid;
        entry={path,fs::is_directory(status)?0:fs::file_size(file),fs::is_directory(status)};
        return 0;
    }
    Result List(bool,const std::string& path,std::vector<std::string>& names){
        names.clear();for(const auto& child:fs::directory_iterator(Map(path)))names.push_back(child.path().filename().string());
        return 0;
    }
};
int main(int argc,char** argv){
    if(argc!=3)throw std::runtime_error("Expected filesystem root and logical NRO path");
    ReadOnly io{fs::path(argv[1])};Plan plan;plan.nro=argv[2];plan.roots={DefaultRoot(plan.nro)};
    if(Inventory(io,plan))throw std::runtime_error("Production inventory refused this folder");
    std::uint64_t bytes{};std::size_t count=1,files{};
    for(const auto& entry:fs::recursive_directory_iterator(io.Map(plan.roots[0]))){
        ++count;if(entry.is_regular_file()){bytes+=entry.file_size();++files;}
    }
    if(bytes!=plan.bytes||count!=plan.entries.size())throw std::runtime_error("Inventory missed files or folders");
    std::cout<<"Read-only full-folder check: "<<files<<" files, "<<(count-files)<<" folders, "<<bytes<<" bytes; every entry selected\n";
}
