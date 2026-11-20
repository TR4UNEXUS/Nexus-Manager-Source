// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>
namespace nexus {
inline unsigned WorkPart(std::uint64_t done,std::uint64_t total,unsigned end=10000){
    if(!total)return 0;
    return static_cast<unsigned>((static_cast<__uint128_t>(std::min(done,total))*end)/total);
}
struct TaskProgress {
    unsigned begin{},end{10000},value{};
    bool known{};
    std::vector<std::pair<unsigned,unsigned>> parents;
    unsigned Position(std::uint64_t done,std::uint64_t total)const{
        return begin+WorkPart(done,total,end-begin);
    }
    void Set(std::uint64_t done,std::uint64_t total){
        if(!total)return;
        known=true;value=std::max(value,std::min(9999U,Position(done,total)));
    }
    void Push(unsigned from,unsigned to){
        from=std::min(from,10000U);to=std::clamp(to,from,10000U);
        const auto a=Position(from,10000),b=Position(to,10000);
        parents.emplace_back(begin,end);begin=a;end=b;Set(0,1);
    }
    void Pop(){if(parents.empty())return;begin=parents.back().first;end=parents.back().second;parents.pop_back();}
    void Finish(bool success){if(success){known=true;value=10000;}}
};
}
