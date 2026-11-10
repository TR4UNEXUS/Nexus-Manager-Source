/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RPC_STATUS_CONSTANTS */
#define RPC_CONTEXT_MAGIC 0x71727374
#define NFS4_OK 0
#define NFS4ERR_ACCESS 13
#define OP_GETATTR 9
struct rpc_context;
struct nfs_context;
typedef void (*rpc_cb)(struct rpc_context*,int,void*,void*);
typedef void (*nfs_cb)(int,struct nfs_context*,void*,void*);
struct rpc_context {unsigned magic;rpc_cb connect_cb;};
struct nfs_internal {unsigned char verifier[8];char* client_name;int auto_reconnect,timeout,retrans;};
struct nfs_context {struct nfs_internal* nfsi;char error[256];};
struct nfs4_cb_data {struct nfs_context* nfs;nfs_cb cb;void* private_data;char* path;};
struct rpc_cb_data {rpc_cb cb;void* private_data;};
typedef struct {struct {struct {char* attrlist4_val;unsigned attrlist4_len;} attr_vals;} obj_attributes;} GETATTR4resok;
typedef struct {int resop;union {struct {union {GETATTR4resok resok4;} GETATTR4res_u;} opgetattr;} nfs_resop4_u;} nfs_resop4;
typedef struct {int status;struct {unsigned resarray_len;nfs_resop4* resarray_val;} resarray;} COMPOUND4res;
typedef struct {int argop;} nfs_argop4;
typedef struct {struct {unsigned argarray_len;nfs_argop4* argarray_val;} argarray;} COMPOUND4args;
struct rpc_pdu {int queued;};
static unsigned callbacks,nfs_frees,rpc_frees,queued,resiliency;
static uint64_t readmax,writemax;
static int queue_fails,last_status;
static void* last_payload;
static char last_message[256];
static const int user_cookie=1234;
static struct nfs4_cb_data* pending;
static struct rpc_context rpc;
static struct nfs_internal internal;
static struct nfs_context nfs;
static void nfs_set_error(struct nfs_context* ctx,const char* format,...){
    char text[256];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
    memcpy(ctx->error,text,sizeof(text));ctx->error[sizeof(ctx->error)-1]=0;
}
static char* nfs_get_error(struct nfs_context* ctx){return ctx->error;}
static void nfs_set_readmax(struct nfs_context* ctx,uint64_t value){assert(ctx==&nfs);readmax=value;}
static void nfs_set_writemax(struct nfs_context* ctx,uint64_t value){assert(ctx==&nfs);writemax=value;}
static void rpc_set_resiliency(struct rpc_context* ctx,int reconnect,int timeout,int retrans){
    assert(ctx==&rpc&&reconnect==internal.auto_reconnect&&timeout==internal.timeout&&retrans==internal.retrans);++resiliency;
}
static const char* nfsstat4_to_str(int status){assert(status==NFS4ERR_ACCESS);return "NFS4ERR_ACCESS";}
static int nfsstat4_to_errno(int status){assert(status==NFS4ERR_ACCESS);return -EACCES;}
static void free_nfs4_cb_data(struct nfs4_cb_data* data){++nfs_frees;assert(nfs_frees==1);free(data);}
static void free_rpc_cb_data(struct rpc_cb_data* data){++rpc_frees;assert(rpc_frees==1);free(data);}
static void finished(int status,struct nfs_context* ctx,void* payload,void* private_data){
    ++callbacks;assert(callbacks==1&&ctx==&nfs&&private_data==&user_cookie);
    last_status=status;last_payload=payload;
    if(payload)snprintf(last_message,sizeof(last_message),"%s",(char*)payload);
}
static struct nfs4_cb_data* NewData(void){
    struct nfs4_cb_data* data=calloc(1,sizeof(*data));assert(data);
    data->nfs=&nfs;data->cb=finished;data->private_data=(void*)&user_cookie;data->path="/games";return data;
}
static int nfs4_op_setclientid(struct nfs_context* ctx,nfs_argop4* op,void* verifier,char* name){
    assert(ctx==&nfs&&verifier==internal.verifier&&name==internal.client_name);op->argop=35;return 1;
}
static void nfs4_mount_2_cb(struct rpc_context* ctx,int status,void* reply,void* private_data){
    (void)reply;assert(ctx==&rpc&&status==RPC_STATUS_SUCCESS);struct nfs4_cb_data* data=private_data;
    data->cb(0,data->nfs,NULL,data->private_data);free_nfs4_cb_data(data);
}
static struct rpc_pdu* rpc_nfs4_compound_task(struct rpc_context* ctx,rpc_cb cb,COMPOUND4args* args,void* private_data){
    static struct rpc_pdu pdu;
    ++queued;assert(queued==1&&ctx==&rpc&&cb==nfs4_mount_2_cb);
    assert(args->argarray.argarray_len==1&&args->argarray.argarray_val[0].argop==35);
    if(queue_fails){nfs_set_error(&nfs,"queue unavailable");return NULL;}
    pending=private_data;return &pdu;
}
/* PRODUCTION_CHECK */
/* PRODUCTION_MOUNT */
/* PRODUCTION_CONNECT */
/* PRODUCTION_FIND */
/* PRODUCTION_DECODE */
/* PRODUCTION_BOUNDS */
/* PRODUCTION_RWMAX */
/* PRODUCTION_MOUNT5 */
static void Reset(void){
    callbacks=nfs_frees=rpc_frees=queued=resiliency=0;readmax=writemax=0;queue_fails=last_status=0;last_payload=NULL;last_message[0]=0;pending=NULL;
    memset(&rpc,0,sizeof(rpc));rpc.magic=RPC_CONTEXT_MAGIC;
    memset(&internal,0,sizeof(internal));internal.client_name="Nexus test";
    memset(&nfs,0,sizeof(nfs));nfs.nfsi=&internal;
}
static void Connect(int status,void* payload){
    struct rpc_cb_data* data=calloc(1,sizeof(*data));assert(data);
    data->cb=nfs4_mount_1_cb;data->private_data=NewData();rpc.connect_cb=nfs4_mount_1_cb;
    rpc_connect_program_5_cb(&rpc,status,payload,data);
    assert(rpc.connect_cb==NULL&&rpc_frees==1);
}
int main(int argc,char** argv){
    (void)argv;Reset();
    /* This exact path crashed before the fix: successful connect passes NULL. */
    Connect(RPC_STATUS_SUCCESS,NULL);
    if(argc>1)return 2; /* Legacy run must fault before reaching here. */
    assert(queued==1&&pending&&callbacks==0&&nfs_frees==0);
    nfs4_mount_2_cb(&rpc,RPC_STATUS_SUCCESS,NULL,pending);assert(callbacks==1&&nfs_frees==1&&last_status==0);
    /* A failed queue terminates once and keeps an explanatory error message. */
    Reset();queue_fails=1;Connect(RPC_STATUS_SUCCESS,NULL);
    assert(queued==1&&!pending&&callbacks==1&&nfs_frees==1&&last_status==-ENOMEM);
    assert(strstr(last_message,"Failed to queue SETCLIENTID")&&strstr(last_message,"queue unavailable"));
    /* Transport error payloads are not thrown away or parsed as COMPOUND. */
    const char* transport="connection refused";
    for(int status=RPC_STATUS_ERROR;status<=RPC_STATUS_TIMEOUT;++status){
        Reset();Connect(status,status==RPC_STATUS_ERROR?(void*)transport:NULL);
        assert(!queued&&callbacks==1&&nfs_frees==1);
        if(status==RPC_STATUS_ERROR)assert(last_status==-EFAULT&&last_payload==transport&&!strcmp(last_message,transport));
        if(status==RPC_STATUS_CANCEL)assert(last_status==-EINTR&&!strcmp(last_message,"Command was cancelled"));
        if(status==RPC_STATUS_TIMEOUT)assert(last_status==-EINTR&&!strcmp(last_message,"Command timed out"));
    }
    /* Unknown status and missing successful compound replies must fail, never
     * silently proceed or dereference an absent/non-response payload. */
    Reset();Connect(99,(void*)(uintptr_t)1);
    assert(!queued&&callbacks==1&&nfs_frees==1&&last_status==-EFAULT&&strstr(last_message,"invalid RPC response"));
    for(int test=0;test<3;++test){
        Reset();struct nfs4_cb_data* data=NewData();
        assert(check_nfs4_error(&nfs,test?99:RPC_STATUS_SUCCESS,data,test==2?(void*)(uintptr_t)1:NULL,"READ"));
        assert(callbacks==1&&nfs_frees==1&&last_status==-EFAULT&&strstr(last_message,"READ")&&strstr(last_message,"invalid RPC response"));
    }
    /* Ordinary compound successes and NFS error responses retain semantics. */
    Reset();struct nfs4_cb_data* data=NewData();COMPOUND4res reply={0};reply.status=NFS4_OK;
    assert(!check_nfs4_error(&nfs,RPC_STATUS_SUCCESS,data,&reply,"READ")&&!callbacks&&!nfs_frees);free_nfs4_cb_data(data);
    Reset();data=NewData();reply.status=NFS4ERR_ACCESS;
    assert(check_nfs4_error(&nfs,RPC_STATUS_SUCCESS,data,&reply,"READ"));
    assert(callbacks==1&&nfs_frees==1&&last_status==-EACCES&&strstr(last_message,"/games")&&strstr(last_message,"NFS4ERR_ACCESS"));
    /* A malformed GETATTR/RWMAX must return after its failure callback and
     * cleanup, rather than fall through into a second success/free. */
    for(unsigned length=0;length<=16;++length){
        Reset();uint32_t attributes[4]={htonl(0),htonl(65536),htonl(0),htonl(131072)};
        nfs_resop4 op={0};op.resop=OP_GETATTR;
        op.nfs_resop4_u.opgetattr.GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val=(char*)attributes;
        op.nfs_resop4_u.opgetattr.GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len=length;
        reply=(COMPOUND4res){0};reply.resarray.resarray_len=1;reply.resarray.resarray_val=&op;
        nfs4_mount_5_cb(&rpc,RPC_STATUS_SUCCESS,&reply,NewData());
        assert(callbacks==1&&nfs_frees==1);
        if(length<16)assert(last_status==-EINVAL&&resiliency==0&&strstr(last_message,"Not enough data in fattr4"));
        else assert(last_status==0&&resiliency==1&&readmax==65536&&writemax==131072);
    }
    Reset();reply=(COMPOUND4res){0};nfs4_mount_5_cb(&rpc,RPC_STATUS_SUCCESS,&reply,NewData());
    assert(callbacks==1&&nfs_frees==1&&last_status==-EINVAL&&resiliency==0&&strstr(last_message,"No GETATTR result"));
    Reset();nfs4_mount_5_cb(&rpc,RPC_STATUS_SUCCESS,NULL,NewData());
    assert(callbacks==1&&nfs_frees==1&&last_status==-EFAULT&&resiliency==0);
    puts("Actual NFSv4 connect/mount/error callbacks: NULL connection success, SETCLIENTID queue, transport failures and once-only cleanup passed");
    return 0;
}
