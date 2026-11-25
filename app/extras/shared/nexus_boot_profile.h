// SPDX-License-Identifier: GPL-2.0-or-later
// Merge the selected Nexus pack's own boot dependencies into recognised CFW
// entries. Preserve stock/payload entries, user names, emuMMC and other KIPs.
#pragma once
#include "hekate_icons.h"
static inline int nexus_boot_mode(const char *data,size_t size) {
    int mode=0,found=0;
    if(!data||!size||size>NEXUS_INI_LIMIT||memchr(data,0,size))return 0;
    for(size_t start=0;start<size;){
        size_t end=nexus_section_end(data,size,start),pos=start;
        if(nexus_section_kind(data,start,end,0)){
            int loader=0,hoc=0,secmon=0;
            while(pos<end){nexus_ini_span k,v;if(!nexus_pair(nexus_line(data,end,&pos),&k,&v))continue;
                if(nexus_equal(k,"kip1")&&nexus_equal(v,"atmosphere/kips/nexus-loader.kip"))loader=1;
                if(nexus_equal(k,"kip1")&&nexus_equal(v,"atmosphere/kips/hoc.kip"))hoc=1;
                if(nexus_equal(k,"secmon"))secmon=nexus_equal(v,"atmosphere/exosphere.bin")?1:2;
            }
            if(!loader||secmon==2||hoc!=(secmon==1))return 0;
            int current=hoc?2:1;if(mode&&mode!=current)return 0;mode=current;++found;
        }start=end;
    }return found?mode:0;
}
static inline int nexus_merge_boot_profile(const char *data,size_t size,const char *pack,size_t pack_size,
                                         char *out,size_t capacity,size_t *out_size,unsigned *changed){
    int mode=nexus_boot_mode(pack,pack_size),themed=nexus_themed_pack(pack,pack_size);size_t used=0;unsigned matched=0;
    if(!mode||!data||!size||size>NEXUS_INI_LIMIT||memchr(data,0,size)||!out||!out_size||!changed)return 0;
    *changed=0;const char* newline=memchr(data,'\r',size)?"\r\n":"\n";
#define NEXUS_APPEND(p,n) do{size_t len_=(n);if(len_>capacity-used)return 0;memcpy(out+used,(p),len_);used+=len_;}while(0)
#define NEXUS_LINE(s) do{const char* text_=(s);NEXUS_APPEND(text_,strlen(text_));NEXUS_APPEND(newline,strlen(newline));}while(0)
    for(size_t start=0;start<size;){
        size_t end=nexus_section_end(data,size,start),pos=start;int kind=nexus_section_kind(data,start,end,0),wildcard=0;
        while(pos<end){
            nexus_ini_span line=nexus_line(data,end,&pos),k,v;
            if(kind&&nexus_pair(line,&k,&v)){
                if(nexus_equal(k,"kip1")&&(nexus_equal(v,"atmosphere/kips/nexus-loader.kip")||nexus_equal(v,"atmosphere/kips/hoc.kip")))continue;
                if(nexus_equal(k,"kip1")&&(nexus_equal(v,"atmosphere/kips/*")||nexus_equal(v,"atmosphere/kips/*.kip")))wildcard=1;
                if(nexus_equal(k,"secmon")){if(!nexus_equal(v,"atmosphere/exosphere.bin"))return 0;continue;}
                if(nexus_equal(k,"icon"))continue;
            }
            NEXUS_APPEND(line.p,line.n);
        }
        if(kind){
            if(++matched>32)return 0;
            if(used&&out[used-1]!='\n')NEXUS_APPEND(newline,strlen(newline));
            if(!wildcard){NEXUS_LINE("kip1=atmosphere/kips/nexus-loader.kip");if(mode==2)NEXUS_LINE("kip1=atmosphere/kips/hoc.kip");}
            if(mode==2)NEXUS_LINE("secmon=atmosphere/exosphere.bin");
            if(themed){NEXUS_LINE(kind==1?"icon=" NEXUS_EMU_ICON:"icon=" NEXUS_SYS_ICON);}
            else NEXUS_LINE("icon=bootloader/res/icon_payload.bmp");
            ++*changed;
        }start=end;
    }
#undef NEXUS_LINE
#undef NEXUS_APPEND
    if(!matched||used>NEXUS_INI_LIMIT)return 0;*out_size=used;return 1;
}
