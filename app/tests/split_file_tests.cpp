#include "yati/source/file.hpp"
#include "nexus/package.hpp"
#include <cassert>
#include <iostream>
using sphaira::yati::source::File;
int main(){
 fs::Fs fs;
 for(const auto& pattern:{std::string("/games/Old.xc"),std::string("/games/Old.xci.0")}) {
  fake::files.clear();std::vector<u8> expected;
  for(unsigned i=0;i<3;++i){std::vector<u8> b(7+i);for(unsigned j=0;j<b.size();++j)b[j]=expected.size()+j;fake::files[pattern+char('0'+i)]=b;expected.insert(expected.end(),b.begin(),b.end());}
  const auto first=pattern+"0";
  {
   File f(&fs,first);assert(!f.GetOpenResult()&&f.HasJoinedParts());s64 size{};assert(!f.GetSize(&size)&&size==s64(expected.size()));
   for(bool short_read:{false,true}){fake::short_read=short_read;
    for(unsigned start=0;start<=expected.size();++start)for(unsigned length=0;length<=expected.size()+2;++length){
     std::vector<u8> out(length+2,0xcc);u64 got{};assert(!f.Read(out.data()+1,start,length,&got));
     assert(got==std::min<std::size_t>(length,expected.size()-start));
     assert(!std::memcmp(out.data()+1,expected.data()+start,got)&&out.front()==0xcc&&out.back()==0xcc);
    }
   }
   u8 b[32];u64 got{};fake::zero_read=true;assert(f.Read(b,0,3,&got));fake::zero_read=false;
   fake::fail_read=pattern+"1";assert(f.Read(b,0,20,&got));fake::fail_read.clear();
   assert(f.Read(b,-1,3,&got)&&f.Read(b,0,-1,&got));
  }
  assert(fake::active==0 && fake::max_active<=1);
  {File f(&fs,pattern+"1");assert(f.GetOpenResult());}
  fake::files.erase(pattern+"1");{File f(&fs,first);assert(f.GetOpenResult());}
  fake::files[pattern+"1"]={};{File f(&fs,first);assert(f.GetOpenResult());}
  fake::files[pattern+"1"]={1};fake::fail_open=pattern+"2";{File f(&fs,first);assert(f.GetOpenResult()==95);}fake::fail_open.clear();
  fake::fail_dir=true;{File f(&fs,first);assert(f.GetOpenResult()==94);}fake::fail_dir=false;
 }
 fake::files={{"/regular.nsp",{1,2,3}}};{File f(&fs,"/regular.nsp");assert(!f.GetOpenResult()&&!f.HasJoinedParts());}
 assert(fake::active==0);std::cout<<"Real split source: both naming schemes, cross-part random/short reads, bounded handles, holes, empty parts, IO errors passed\n";
}
