// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/network_location_core.hpp"
#include <vector>

namespace nexus::network {
// UI-thread-only credentials for this process. Nothing in this type serializes
// passwords; a name change cannot retarget a secret to another server/account.
inline bool SameEndpoint(const Connection& a,const Connection& b){
    return a.protocol==b.protocol&&a.host==b.host&&a.share==b.share&&a.user==b.user&&a.domain==b.domain;
}
class SessionPasswords {
    struct Entry {Connection connection;std::string password;};
    std::vector<Entry> entries;
public:
    ~SessionPasswords(){Clear();}
    bool Find(const Connection& c,std::string& password)const{
        password.clear();
        if(c.protocol!="SMB"||!Valid(c))return false;
        for(const auto& entry:entries)if(SameEndpoint(entry.connection,c)){password=entry.password;return true;}
        return false;
    }
    void Forget(const Connection& c){
        for(auto i=entries.begin();i!=entries.end();){
            if(SameEndpoint(i->connection,c)){std::fill(i->password.begin(),i->password.end(),'\0');i=entries.erase(i);}
            else ++i;
        }
    }
    void Remember(const Connection& c,const std::string& password){
        if(c.protocol!="SMB"||!Valid(c)||password.size()>512)return;
        Forget(c);
        if(entries.size()>=64){std::fill(entries.front().password.begin(),entries.front().password.end(),'\0');entries.erase(entries.begin());}
        entries.push_back({c,password});
    }
    void Clear(){for(auto& entry:entries)std::fill(entry.password.begin(),entry.password.end(),'\0');entries.clear();}
};
}
