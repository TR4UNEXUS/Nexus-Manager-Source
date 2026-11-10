// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace nexus::updates {
enum class Publish {Ok,Conflict,BackupFailed,Restored,RestoreFailed,PublishFailed,CommitFailed};
// Caller owns and has fully reread/verified staging bytes. No cancellation
// inside this tiny rename sequence; never overwrite/delete a backup.
template<class Store>
Publish PublishVerified(Store& io,const char* temp,const char* current,const char* backup,bool replace){
    if(!io.Regular(temp) || (replace?!io.Regular(current):!io.Absent(current)) || (replace&&!io.Absent(backup)))return Publish::Conflict;
    if(replace){
        if(!io.Rename(current,backup))return Publish::BackupFailed;
        if(!io.Commit()){
            const bool restored=io.Rename(backup,current);
            return restored&&io.Commit()?Publish::Restored:Publish::RestoreFailed;
        }
    }
    if(!io.Rename(temp,current)){
        if(!replace)return Publish::PublishFailed;
        const bool restored=io.Rename(backup,current);
        return restored&&io.Commit()?Publish::Restored:Publish::RestoreFailed;
    }
    return io.Commit()?Publish::Ok:Publish::CommitFailed;
}
}
