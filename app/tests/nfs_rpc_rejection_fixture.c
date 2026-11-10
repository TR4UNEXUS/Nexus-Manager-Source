/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
enum reject_stat {RPC_MISMATCH=0,AUTH_ERROR=1};
struct rejected_reply {enum reject_stat stat;union {struct {uint32_t low,high;} mismatch_info;unsigned stat;} reject_data;};
struct rpc_context {char error[1024];};
struct rpc_pdu {void (*cb)(struct rpc_context*,int,void*,void*);void* private_data;};
#define RPC_STATUS_ERROR 1
static void rpc_set_error(struct rpc_context* rpc,const char* format,...){va_list args;va_start(args,format);vsnprintf(rpc->error,sizeof(rpc->error),format,args);va_end(args);}
static char* rpc_get_error(struct rpc_context* rpc){return rpc->error;}
/* PRODUCTION_REJECTED_REPLY */
static unsigned called;static char message[1024];static int marker;
static void complete(struct rpc_context* rpc,int status,void* data,void* argument){assert(status==RPC_STATUS_ERROR&&argument==&marker);assert(data==rpc_get_error(rpc));++called;snprintf(message,sizeof(message),"%s",(char*)data);}
int main(void){
    struct rpc_context rpc={0};struct rpc_pdu pdu={complete,&marker};struct rejected_reply reply={0};
    for(unsigned auth=0;auth<=7;++auth){
        called=0;reply.stat=AUTH_ERROR;reply.reject_data.stat=auth;
        rpc_report_rejected_reply(&rpc,&pdu,&reply);
        assert(called==1&&strstr(message,"authentication rejected")&&strcmp(message,rpc_get_error(&rpc))==0);
        char number[24];snprintf(number,sizeof(number),"status %u",auth);assert(strstr(message,number));
    }
    called=0;reply.stat=RPC_MISMATCH;reply.reject_data.mismatch_info.low=2;reply.reject_data.mismatch_info.high=3;
    rpc_report_rejected_reply(&rpc,&pdu,&reply);assert(called==1&&strstr(message,"version rejected")&&strstr(message,"2-3"));
    called=0;reply.stat=99;rpc_report_rejected_reply(&rpc,&pdu,&reply);assert(called==1&&strstr(message,"status 99"));
    puts("Actual RPC rejection handling: authentication codes, version mismatch, unknown rejection and persistent once-only error callback passed");
    return 0;
}
