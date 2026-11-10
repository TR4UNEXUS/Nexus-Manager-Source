// SPDX-License-Identifier: MIT
#include <switch.h>
#include <nexus/vsd_protocol.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
static unsigned passed,failed;
static void check(int ok,const char *stage){printf("%s %s (errno %d)\n",ok?"PASS":"FAIL",stage,ok?0:errno);if(ok)++passed;else ++failed;consoleUpdate(NULL);}
static bool active(void){
    u8 present=0;SmServiceName name=smEncodeName("ams:usb");
    Result rc=tipcDispatchInOut(smGetServiceSessionTipc(),65100,name,present);
    if(R_FAILED(rc)||!present)return false;
    Service s={0};rc=smGetService(&s,"ams:usb");if(R_FAILED(rc))return false;
    u64 pid_placeholder=0;NexusVsdStatus status={0};
    rc=serviceDispatchInOut(&s,NEXUS_VSD_QUERY,pid_placeholder,status,.in_send_pid=true);
    serviceClose(&s);
    return R_SUCCEEDED(rc)&&status.magic==NEXUS_VSD_MAGIC&&status.version==NEXUS_VSD_VERSION&&(status.flags&NEXUS_VSD_ACTIVE)&&status.generation;
}
static void run(void){
    if(!active()){printf("No active Virtual SD session. NO files written.\nLaunch this NRO from USB Loader / USB Homebrew.\n");return;}
    // Each run creates one fresh private directory. Never truncate user files.
    char dir[100],file[120],moved[120];
    snprintf(dir,sizeof(dir),"sdmc:/vsd-probe-%016llx",(unsigned long long)armGetSystemTick());
    snprintf(file,sizeof(file),"%s/data.bin",dir);snprintf(moved,sizeof(moved),"%s/renamed.bin",dir);
    int rc=mkdir(dir,0777);check(rc==0,"Create private USB test directory");if(rc)return;
    int fd=open(file,O_RDWR|O_CREAT|O_EXCL,0600);check(fd>=0,"Create/open file");if(fd<0)return;
    static unsigned char data[65536],back[65536];
    for(size_t i=0;i<sizeof(data);++i)data[i]=(unsigned char)((i*37)^0x5a);
    ssize_t wrote=write(fd,data,sizeof(data));check(wrote==(ssize_t)sizeof(data),"Write 64 KiB");
    check(fsync(fd)==0,"Flush");
    check(lseek(fd,123,SEEK_SET)==123,"Random seek");
    ssize_t got=read(fd,back,4096);check(got==4096&&!memcmp(back,data+123,4096),"Random read matches");
    check(close(fd)==0,"Close writer");
    fd=open(file,O_RDONLY);check(fd>=0,"Reopen saved file");
    if(fd>=0){got=read(fd,back,sizeof(back));check(got==(ssize_t)sizeof(back)&&!memcmp(data,back,sizeof(back)),"Full saved data matches");check(read(fd,back,1)==0,"EOF short read");close(fd);}
    struct stat info={0};check(stat(file,&info)==0&&info.st_size==(off_t)sizeof(data),"File size/stat");
    DIR *d=opendir(dir);bool found=false;
    if(d){struct dirent *e;while((e=readdir(d)))if(!strcmp(e->d_name,"data.bin"))found=true;closedir(d);}
    check(found,"Directory listing");
    check(rename(file,moved)==0,"Rename own file");
    // Only delete the two exact names created by this run; leave failures for inspection.
    if(!failed){check(unlink(moved)==0,"Delete own file");if(!failed)check(rmdir(dir)==0,"Remove empty test directory");}
    printf("\n%u passed, %u failed.\n",passed,failed);
    if(failed)printf("Inspect the isolated USB profile for retained test data.\n");
    printf("B returns to hbmenu; check that real SD files appear again.\n");
}
int main(int argc,char **argv){
    consoleInit(NULL);padConfigureInput(1,HidNpadStyleSet_NpadStandard);PadState pad;padInitializeDefault(&pad);
    printf("Nexus Virtual SD probe v1\n\n");
    printf("argv[0]: %s\n",argc?argv[0]:"(missing)");run();
    printf("\nPress B to exit. Do not disconnect USB while running.\n");
    while(appletMainLoop()){padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_B)break;consoleUpdate(NULL);}
    consoleExit(NULL);return 0;
}


