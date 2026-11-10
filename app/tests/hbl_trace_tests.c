#define NEXUS_MOCK_IMPLEMENTATION
#include "nexus_trace.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static int fs_opens,fs_closes,file_closes,writes,usb_active,fail_open,fail_create,fail_write;
static u64 total,tick;
u64 svcGetSystemTick(void){return ++tick;}
Result svcGetInfo(u64* out,int id,Handle h,u64 sub){(void)id;(void)h;(void)sub;*out=0x0500123400000000;return 0;}
Result fsOpenSdCardFileSystem(FsFileSystem* s){assert(!usb_active);++fs_opens;s->id=73;return fail_open?1:0;}
Result fsFsCreateDirectory(FsFileSystem* s,const char* path){assert(s->id==73);assert(!strncmp(path,"/switch",7));return 0;}
Result fsFsCreateFile(FsFileSystem* s,const char* path,u64 size,u32 opt){
    assert(s->id==73 && size==0 && opt==0);assert(strstr(path,"/switch/nexusmanager/forwarder-debug/0500123400000000-"));return fail_create?1:0;
}
Result fsFsOpenFile(FsFileSystem* s,const char* path,u32 mode,FsFile* f){
    (void)path;assert(s->id==73 && mode==(FsOpenMode_Write|FsOpenMode_Append));f->id=91;return 0;
}
Result fsFileWrite(FsFile* f,u64 off,const void* data,u64 size,u32 opts){
    assert(f->id==91 && off==total && size<=16384 && off+size<=16384 && opts==FsWriteOption_Flush);
    assert(!memcmp(data,"HBL-0.6.8-SD8 tick=",sizeof("HBL-0.6.8-SD8 tick=")-1));++writes;if(fail_write)return 1;total+=size;return 0;
}
void fsFileClose(FsFile* f){assert(f->id==91);++file_closes;}
void fsFsClose(FsFileSystem* s){assert(s->id==73);++fs_closes;}
int main(int argc,char** argv){
    const char* mode=argc>1?argv[1]:"normal";
    fail_open=!strcmp(mode,"open-fail");fail_create=!strcmp(mode,"create-fail");
    nexusTraceOpen();nexusTrace("early",0,0);
    assert(fs_opens==0 && writes==0); // no SD access before marker discovery
    if(!strcmp(mode,"timed") || !strcmp(mode,"unresolved")){
        if(!strcmp(mode,"timed"))nexusTraceConfigure(false);
        usb_active=1;
        for(int i=0;i<1000;++i)nexusTrace("test",0,i);
        nexusTraceClose();nexusTraceConfigure(true);nexusTraceOpen();
        assert(fs_opens==0 && fs_closes==0 && file_closes==0 && writes==0);
        puts("Timed or unresolved launch performs zero physical-SD trace I/O");return 0;
    }
    nexusTraceConfigure(true);usb_active=1;
    fail_write=!strcmp(mode,"write-fail");
    for(int i=0;i<500;++i)nexusTrace("test",0,i);
    nexusTraceClose();const int before=writes;
    nexusTraceOpen();nexusTrace("after-close",0,0);nexusTraceClose();
    assert(fs_opens==1 && writes==before && total<=16384);
    if(fail_open)assert(fs_closes==0 && file_closes==0 && writes==0);
    else if(fail_create)assert(fs_closes==1 && file_closes==0 && writes==0);
    else assert(fs_closes==1 && file_closes==1 && writes>0);
    puts("Real HBL trace: original SD handle retained, no reopen after USB activation, exclusive create, flush, 16KiB bound, failures/close safe");
}
