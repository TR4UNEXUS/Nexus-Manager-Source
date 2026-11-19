// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace nexus::transfer {
// One producer (FTP), one consumer (Yati). No package is staged on either
// storage device. Backpressure limits memory even if storage is much slower.
class FtpStream {
public:
    using Result=std::uint32_t;
    static constexpr Result Aborted=(405u<<9)|345u, Invalid=(11u<<9)|345u;
    static constexpr std::uint64_t Limit=1ULL<<40;
    explicit FtpStream(std::size_t capacity=4*1024*1024):buffer(std::max<std::size_t>(capacity,1)){}
    Result Write(const void* data,std::uint64_t offset,std::size_t size){
        std::unique_lock lock(mutex);
        if(offset!=received||size>Limit-received||(!data&&size)||ended||done)return Invalid;
        auto* src=static_cast<const unsigned char*>(data);
        while(size){
            changed.wait(lock,[&]{return error||done||used<buffer.size();});
            if(error||done)return error?error:Invalid;
            const auto pos=(head+used)%buffer.size();
            const auto n=std::min({size,buffer.size()-used,buffer.size()-pos});
            std::memcpy(buffer.data()+pos,src,n);used+=n;received+=n;src+=n;size-=n;changed.notify_all();
        }
        return error;
    }
    template<class Check> Result Read(void* data,std::size_t size,std::uint64_t& actual,Check check){
        actual=0;if(!data&&size)return Invalid;if(!size)return 0;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
        std::unique_lock lock(mutex);
        while(!error&&!used&&!ended){
            lock.unlock();const Result rc=check();lock.lock();
            if(rc){FailLocked(rc);break;}
            if(std::chrono::steady_clock::now()>=deadline){FailLocked(Aborted);break;}
            changed.wait_for(lock,std::chrono::milliseconds(50));
        }
        if(error)return error;
        const auto n=std::min({size,used,buffer.size()-head});
        if(n){std::memcpy(data,buffer.data()+head,n);head=(head+n)%buffer.size();used-=n;consumed+=n;actual=n;changed.notify_all();}
        return 0; // zero is EOF only after a clean FTP data-connection close.
    }
    void EndInput(bool complete){std::scoped_lock lock(mutex);ended=true;if(!complete)FailLocked(Aborted);changed.notify_all();}
    void Cancel(Result rc=Aborted){std::scoped_lock lock(mutex);FailLocked(rc?rc:Aborted);}
    void Complete(Result rc){
        std::scoped_lock lock(mutex);
        if(!rc&&(!ended||used||!received))rc=Invalid;
        if(rc)FailLocked(rc);
        result=error?error:rc;done=true;changed.notify_all();
    }
    Result WaitResult(){std::unique_lock lock(mutex);changed.wait(lock,[&]{return done||error;});return error?error:result;}
    std::uint64_t Received(){std::scoped_lock lock(mutex);return received;}
    std::uint64_t Consumed(){std::scoped_lock lock(mutex);return consumed;}
private:
    void FailLocked(Result rc){if(!error)error=rc;changed.notify_all();}
    std::mutex mutex;std::condition_variable changed;
    std::vector<unsigned char> buffer;std::size_t head{},used{};
    std::uint64_t received{},consumed{};Result error{},result{};bool ended{},done{};
};
}
