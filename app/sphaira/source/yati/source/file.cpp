#include "yati/source/file.hpp"
#include "nexus/package.hpp"
#include <array>
#include <limits>

namespace sphaira::yati::source {
File::File(fs::Fs* fs, const fs::FsPath& path) : m_fs{fs} {
    m_open_result = Open(path);
}
Result File::Open(const fs::FsPath& path) {
    const std::string original(path.s);
    const auto split=nexus::package::SplitPart(original);
    if(!split) {
        R_TRY(m_fs->OpenFile(path,FsOpenMode_Read,&m_file));
        R_TRY(m_file.GetSize(&m_size));
        R_UNLESS(m_size>=0,Result_XciInvalidHeader);
        m_parts.push_back({path,0,m_size});
        return 0;
    }
    R_UNLESS(split.index==0,Result_XciInvalidHeader);
    m_split=true;
    const auto slash=original.find_last_of("/\\");
    const auto folder=slash==original.npos?std::string("."):original.substr(0,slash+1);
    const auto name=original.substr(slash==original.npos?0:slash+1);
    const auto pattern=nexus::package::SplitPart(name);
    std::array<std::string,100> names;
    unsigned last{};
    fs::Dir dir;
    R_TRY(m_fs->OpenDirectory(folder,FsDirOpenMode_ReadFiles,&dir));
    FsDirectoryEntry entry{};
    for(;;) {
        s64 got{};R_TRY(dir.Read(&got,1,&entry));
        if(!got)break;
        R_UNLESS(got==1,Result_XciInvalidHeader);
        const std::string candidate(entry.name);
        const auto other=nexus::package::SplitPart(candidate);
        if(!other || other.digits!=pattern.digits ||
            !nexus::package::EqualAscii(std::string_view(candidate).substr(0,other.prefix),std::string_view(name).substr(0,pattern.prefix)))continue;
        R_UNLESS(names[other.index].empty(),Result_XciInvalidHeader); // case aliases are ambiguous
        names[other.index]=candidate;
        last=std::max(last,other.index);
    }
    for(unsigned i=0;i<=last;++i) {
        R_UNLESS(!names[i].empty(),Result_XciInvalidHeader); // never silently skip a missing middle part
        const auto full=folder+(folder.back()=='/'?"":"/")+names[i];
        R_UNLESS(full.size()<FS_MAX_PATH,Result_XciInvalidHeader);
        fs::File file;
        R_TRY(m_fs->OpenFile(full,FsOpenMode_Read,&file));
        s64 size{};R_TRY(file.GetSize(&size));
        R_UNLESS(size>0 && size<=INT64_MAX-m_size,Result_XciInvalidHeader);
        m_parts.push_back({full,m_size,size});m_size+=size;
    }
    R_TRY(m_fs->OpenFile(m_parts.front().path,FsOpenMode_Read,&m_file));
    return 0;
}
Result File::Read(void* buf, s64 off, s64 size, u64* bytes_read) {
    *bytes_read=0;
    R_TRY(GetOpenResult());
    R_UNLESS(off>=0 && size>=0 && off<=m_size && (buf || !size),Result_XciInvalidHeader);
    const auto wanted=std::min(size,m_size-off);
    while(*bytes_read<u64(wanted)) {
        const auto at=off+s64(*bytes_read);
        auto index=m_active;
        if(at<m_parts[index].offset || at>=m_parts[index].offset+m_parts[index].size) {
            index=0;
            while(index<m_parts.size() && at>=m_parts[index].offset+m_parts[index].size)++index;
            R_UNLESS(index<m_parts.size(),Result_XciInvalidHeader);
        }
        if(index!=m_active) {
            m_file.Close();
            R_TRY(m_fs->OpenFile(m_parts[index].path,FsOpenMode_Read,&m_file));
            m_active=index;
        }
        const auto& part=m_parts[index];
        const auto count=std::min(wanted-s64(*bytes_read),part.size-(at-part.offset));
        u64 got{};
        R_TRY(m_file.Read(at-part.offset,static_cast<u8*>(buf)+*bytes_read,count,0,&got));
        R_UNLESS(got>0 && got<=u64(count),Result_XciInvalidHeader);
        *bytes_read+=got;
    }
    return 0;
}
Result File::GetSize(s64* out) {
    R_TRY(GetOpenResult());*out=m_size;return 0;
}
} // namespace sphaira::yati::source
