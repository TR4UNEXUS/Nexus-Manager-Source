// Nexus Launch icon refresh. Only icon values change; boot commands are preserved.
#pragma once
#include <stddef.h>
#include <string.h>
#define NEXUS_EMU_ICON "bootloader/res/icon_payload_emu.bmp"
#define NEXUS_SYS_ICON "bootloader/res/atmosphere.bmp"
#define NEXUS_INI_LIMIT (64 * 1024)

typedef struct { const char *p; size_t n; } nexus_ini_span;
static inline nexus_ini_span nexus_trim(nexus_ini_span s) {
    while (s.n && (s.p[0]==' ' || s.p[0]=='\t' || s.p[0]=='\r' || s.p[0]=='\n')) { ++s.p; --s.n; }
    while (s.n && (s.p[s.n-1]==' ' || s.p[s.n-1]=='\t' || s.p[s.n-1]=='\r' || s.p[s.n-1]=='\n')) --s.n;
    return s;
}
static inline int nexus_equal(nexus_ini_span s, const char *text) {
    return s.n==strlen(text) && memcmp(s.p,text,s.n)==0;
}
static inline nexus_ini_span nexus_line(const char *data, size_t end, size_t *pos) {
    size_t start=*pos;
    while (*pos<end && data[*pos]!='\n') ++*pos;
    if (*pos<end) ++*pos;
    nexus_ini_span s={data+start,*pos-start}; return s;
}
static inline int nexus_pair(nexus_ini_span s, nexus_ini_span *key, nexus_ini_span *value) {
    s=nexus_trim(s);
    if (!s.n || s.p[0]=='#' || s.p[0]==';' || s.p[0]=='[' || s.p[0]=='{') return 0;
    const char *eq=memchr(s.p,'=',s.n);
    if (!eq) return 0;
    *key=nexus_trim((nexus_ini_span){s.p,(size_t)(eq-s.p)});
    *value=nexus_trim((nexus_ini_span){eq+1,s.n-(size_t)(eq+1-s.p)});
    return 1;
}
static inline size_t nexus_section_end(const char *data, size_t size, size_t start) {
    size_t pos=start; nexus_line(data,size,&pos); // skip this section's header
    while (pos<size) {
        size_t next=pos; nexus_ini_span line=nexus_trim(nexus_line(data,size,&pos));
        if (line.n && (line.p[0]=='[' || line.p[0]=='{')) return next;
    }
    return size;
}
// Classify by boot flags, so renamed CFW entries work too. Never alter stock,
// payload-chainload, ambiguous emuMMC or unrelated firmware entries.
static inline int nexus_section_kind(const char *data, size_t start, size_t end, int require_icon) {
    int package=0,emu=0,sys=0,stock=0,payload=0,icon=0; size_t pos=start;
    while (pos<end) {
        nexus_ini_span key,value;
        if (!nexus_pair(nexus_line(data,end,&pos),&key,&value)) continue;
        if (nexus_equal(key,"pkg3") || nexus_equal(key,"fss0")) package=nexus_equal(value,"atmosphere/package3");
        if (nexus_equal(key,"emummcforce")) emu=nexus_equal(value,"1");
        if (nexus_equal(key,"emummc_force_disable")) sys=nexus_equal(value,"1");
        if (nexus_equal(key,"stock")) stock=nexus_equal(value,"1");
        if (nexus_equal(key,"payload")) payload=1;
        if (nexus_equal(key,"icon")) icon=nexus_equal(value,NEXUS_EMU_ICON)?1:nexus_equal(value,NEXUS_SYS_ICON)?2:0;
    }
    int kind=(!package || stock || payload || emu==sys)?0:emu?1:2;
    return require_icon && icon!=kind ? 0 : kind;
}
static inline int nexus_themed_pack(const char *data, size_t size) {
    int found=0;
    for (size_t start=0;start<size;) {
        size_t end=nexus_section_end(data,size,start);
        int kind=nexus_section_kind(data,start,end,1);
        found|=kind; start=end;
    }
    return found==3;
}
// Caller supplies >= size*3+128 bytes. Returns 0 for invalid input/capacity.
static inline int nexus_merge_themed_icons(const char *data, size_t size, char *out,
                                           size_t capacity, size_t *out_size, unsigned *changed) {
    if (!data || !size || size>NEXUS_INI_LIMIT || !out || capacity<size*3+128 || memchr(data,0,size)) return 0;
    size_t used=0; *changed=0;
    const char *newline=memchr(data,'\r',size)?"\r\n":"\n";
    for (size_t start=0;start<size;) {
        size_t end=nexus_section_end(data,size,start),pos=start;
        int kind=nexus_section_kind(data,start,end,0),seen=0;
        const char *icon=kind==1?NEXUS_EMU_ICON:NEXUS_SYS_ICON;
        while (pos<end) {
            nexus_ini_span line=nexus_line(data,end,&pos),key,value;
            if (kind && nexus_pair(line,&key,&value) && nexus_equal(key,"icon")) {
                size_t prefix=(size_t)(value.p-line.p),suffix=line.n-prefix-value.n;
                memcpy(out+used,line.p,prefix);used+=prefix;
                memcpy(out+used,icon,strlen(icon));used+=strlen(icon);
                memcpy(out+used,value.p+value.n,suffix);used+=suffix;
                if (!nexus_equal(value,icon)) ++*changed;
                seen=1;
            } else { memcpy(out+used,line.p,line.n);used+=line.n; }
        }
        if (kind && !seen) {
            if (used && out[used-1]!='\n') { memcpy(out+used,newline,strlen(newline));used+=strlen(newline); }
            memcpy(out+used,"icon=",5);used+=5;
            memcpy(out+used,icon,strlen(icon));used+=strlen(icon);
            memcpy(out+used,newline,strlen(newline));used+=strlen(newline); ++*changed;
        }
        start=end;
    }
    *out_size=used; return 1;
}
