// SPDX-License-Identifier: GPL-3.0-or-later
#include "language_runtime_fixture.hpp"
#include "i18n.hpp"
#include "nexus/languages.hpp"
#include "nexus/localize.hpp"
#include <yyjson.h>
#include <cassert>
#include <fstream>
#include <iterator>
std::string load(const std::string& path){std::ifstream f(path);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
 assert(argc==2);const std::string dir=argv[1];
 auto en=load(dir+"/en.json");auto english=yyjson_read(en.data(),en.size(),0);assert(english);auto english_root=yyjson_doc_get_root(english);std::size_t count=0;
 for(auto choice:nexus::languages::Choices){
  if(!choice.setting)continue;
  const std::string path=std::string("romfs:/i18n/")+choice.code+".json";
  auto data=load(dir+"/"+choice.code+".json");fs::files[path]=data;
  auto doc=yyjson_read(data.data(),data.size(),0);assert(doc);auto root=yyjson_doc_get_root(doc);assert(yyjson_obj_size(root)==yyjson_obj_size(english_root));
  assert(sphaira::i18n::init(choice.setting));
  yyjson_obj_iter it=yyjson_obj_iter_with(root);yyjson_val* key;
  while((key=yyjson_obj_iter_next(&it))){
   const std::string k=yyjson_get_str(key);const auto value=yyjson_obj_iter_get_val(key);assert(yyjson_is_str(value));
   const auto source=yyjson_obj_get(english_root,k.c_str());assert(yyjson_is_str(source));
   const std::string expected=yyjson_get_str(value),fallback=yyjson_get_str(source);
   if(sphaira::i18n::get(k,fallback)!=expected){std::fprintf(stderr,"Runtime rejected %s key: %s\n",choice.code,k.c_str());return 1;}
   if(k==fallback && std::string(sphaira::i18n::literal(k.c_str(),k.size()))!=expected){std::fprintf(stderr,"Literal mismatch %s: %s\n",choice.code,k.c_str());return 1;}
   ++count;
  }
  sphaira::i18n::exit();yyjson_doc_free(doc);
 }
 yyjson_doc_free(english);std::printf("All bundled catalogues accepted by production i18n: %zu translated entries.\n",count);
}
