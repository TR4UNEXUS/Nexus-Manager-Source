// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/routing_backup.hpp"
#include "nexus/bootstrap.h"
#include "nexus/nx_Client.hpp"
#include "nexus/lifecycle.hpp"
#include "nexus/localize.hpp"
#include "utils/thread.hpp"
#include "app.hpp"
#include "defines.hpp"
#include <atomic>
#include <mutex>
#include <cstdio>
namespace sphaira::nexusapp {
namespace {
Thread worker{};bool attempted{},started{};std::atomic_bool done{};
std::mutex saving;
std::atomic<Result> notice_result{};std::atomic<u64> notice_sequence{};std::atomic_bool notice_pending{};
Result last_notice{};bool warned{};
// This is an archive of existing routing files, not an automatic game scan or
// ticket repair. Missing files never replace a recoverable console archive.
Result ExistingMaps(bool& present){
 present=false;
 if(!nexusHasSdBootstrap(envGetLoaderInfo(),envGetLoaderInfoSize())){nexus::Client c;NexusVsdStatus v{};R_TRY(c.VirtualSdStatus(v));R_UNLESS(!(v.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED)),nexus::Unavailable);}
 FsFileSystem sd{};R_TRY(fsOpenSdCardFileSystem(&sd));ON_SCOPE_EXIT(fsFsClose(&sd));
 unsigned found{};
 for(unsigned i=0;i<3;++i){char path[FS_MAX_PATH]{};snprintf(path,sizeof(path),"%s",nexus::repair::Live(i).c_str());FsDirEntryType type{};
  const auto rc=fsFsGetEntryType(&sd,path,&type);if(rc==MAKERESULT(2,1))continue;R_TRY(rc);R_UNLESS(type==FsDirEntryType_File,nexus::Invalid);++found;}
 R_UNLESS(found==0||found==3,nexus::Invalid);present=found==3;return 0;
}
void Save(){
 std::scoped_lock lock(saving);bool present{};auto rc=ExistingMaps(present);u64 sequence{};
 if(R_SUCCEEDED(rc)&&!present)return;
 if(R_SUCCEEDED(rc))rc=ConfirmConsoleRoutingBackup(sequence);
 nexus::Log("LR1-automatic-console-backup-readback",rc,sequence);
 notice_sequence.store(sequence);notice_result.store(rc);notice_pending.store(true,std::memory_order_release);
}
void Worker(void*){Save();done.store(true,std::memory_order_release);}
void Join(){if(started){const auto wait=threadWaitForExit(&worker);if(R_FAILED(wait))fatalThrow(wait);const auto close=threadClose(&worker);if(R_FAILED(close))fatalThrow(close);started=false;}}
}
void BackupConsoleRoutingAfterChange(){Save();}
void PollAutomaticRoutingBackup(){
 if(!nexus::lifecycle::CanPoll())return;
 if(!attempted){attempted=true;
  auto rc=utils::CreateThread(&worker,Worker,nullptr,64*1024);
  if(R_SUCCEEDED(rc)){rc=threadStart(&worker);if(R_FAILED(rc))threadClose(&worker);else started=true;}
  if(R_FAILED(rc)){notice_result.store(rc);notice_pending.store(true);}
 }
 if(started&&done.load(std::memory_order_acquire))Join();
 if(!notice_pending.exchange(false,std::memory_order_acquire))return;
 const auto rc=notice_result.load();
 if(R_FAILED(rc)){
  if(!warned||last_notice!=rc)App::Notify("USB map backup could not be saved. Open Help & recovery > USB library backup."_nx);
  warned=true;last_notice=rc;
 }else if(warned){App::Notify("USB map backup saved and checked in console storage."_nx);warned=false;last_notice=0;}
}
void StopAutomaticRoutingBackup(){attempted=true;Join();}
}
