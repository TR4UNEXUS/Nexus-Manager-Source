#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define FS_MAX_PATH 0x301
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;typedef u32 Result;typedef u32 Handle;
typedef int SmServiceName;typedef struct {int active;} Service;
enum {Module_Libnx=345,LibnxError_NotFound=1,LibnxError_BadInput=11,SfBufferAttr_HipcMapAlias=1,SfBufferAttr_In=2,SfBufferAttr_Out=4};
#define MAKERESULT(m,d) ((m)|((d)<<9))
#define R_SUCCEEDED(r) ((r)==0)
#define R_FAILED(r) ((r)!=0)
typedef struct {const void* ptr;size_t size;} Buffer;
typedef struct {bool in_send_pid;int in_num_handles;Handle in_handles[1];int buffer_attrs[2];Buffer buffers[2];} Params;
Result mockDispatch(int,const void*,size_t,void*,size_t,Params);
#define serviceDispatchInOut(s,c,i,o,...) ((void)(s),mockDispatch(c,&(i),sizeof(i),&(o),sizeof(o),(Params){__VA_ARGS__}))
#define serviceDispatchOut(s,c,o,...) ((void)(s),mockDispatch(c,0,0,&(o),sizeof(o),(Params){__VA_ARGS__}))
#define serviceDispatchIn(s,c,i,...) ((void)(s),mockDispatch(c,&(i),sizeof(i),0,0,(Params){__VA_ARGS__}))
#define smGetServiceSession() ((Service*)0)
#define smGetServiceSessionTipc() ((Service*)0)
#define tipcDispatchInOut serviceDispatchInOut
Result smInitialize(void);void smExit(void);Result smGetService(Service*,const char*);void serviceClose(Service*);
SmServiceName smEncodeName(const char*);bool hosversionAtLeast(int,int,int);void svcSleepThread(uint64_t);
typedef struct {int id;} FsFileSystem;
typedef struct {int id;} FsFile;
enum {FsWriteOption_Flush=1,FsOpenMode_Write=2,FsOpenMode_Append=4,InfoType_ProgramId=18,CUR_PROCESS_HANDLE=0xFFFF};
u64 svcGetSystemTick(void);
u64 armTicksToNs(u64);
Result svcGetInfo(u64*,int,Handle,u64);
Result fsOpenSdCardFileSystem(FsFileSystem*);
Result fsFsCreateDirectory(FsFileSystem*,const char*);
Result fsFsCreateFile(FsFileSystem*,const char*,u64,u32);
Result fsFsOpenFile(FsFileSystem*,const char*,u32,FsFile*);
Result fsFileWrite(FsFile*,u64,const void*,u64,u32);
void fsFileClose(FsFile*);void fsFsClose(FsFileSystem*);

// libnx forwards a FULL FS_MAX_PATH-byte HIPC input buffer, not strlen(path).
// In the logger builds check the caller's actual object size before decay.
// Keep this out of the mock implementations' function definitions.
#if defined(NEXUS_PATH_CHECKS) && !defined(NEXUS_MOCK_IMPLEMENTATION)
#include <stdio.h>
#include <stdlib.h>
static inline const char* mockCheckedFsPath(const char* path, size_t capacity) {
    if (capacity == (size_t)-1 || capacity < FS_MAX_PATH) {
        fputs("Fixed-size FS IPC path is smaller than FS_MAX_PATH\n", stderr);
        exit(91);
    }
    return path;
}
#define MOCK_FS_PATH(p) mockCheckedFsPath((p), __builtin_object_size((p), 1))
#define fsFsCreateDirectory(s,p) fsFsCreateDirectory((s),MOCK_FS_PATH(p))
#define fsFsCreateFile(s,p,n,o) fsFsCreateFile((s),MOCK_FS_PATH(p),(n),(o))
#define fsFsOpenFile(s,p,m,f) fsFsOpenFile((s),MOCK_FS_PATH(p),(m),(f))
#endif
