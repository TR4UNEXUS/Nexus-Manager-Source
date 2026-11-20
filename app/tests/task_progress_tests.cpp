// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/task_progress.hpp"
#include <cassert>
#include <iostream>
int main(){
    nexus::TaskProgress task;unsigned last{};
    for(unsigned file=0;file<50;++file){
        task.Push(file*200,(file+1)*200);
        for(unsigned phase=0;phase<2;++phase){
            task.Push(phase*5000,(phase+1)*5000);
            for(unsigned bytes=0;bytes<=100;++bytes){task.Set(bytes,100);assert(task.value>=last&&task.value<10000);last=task.value;}
            task.Pop();
        }
        task.Pop();assert(task.parents.empty());
    }
    assert(task.value==9999);task.Finish(false);assert(task.value==9999);task.Finish(true);assert(task.value==10000);
    task={};task.Push(2000,8000);task.Set(1,2);assert(task.value==5000);
    task.Set(0,2);assert(task.value==5000);task.Set(1,0);assert(task.value==5000);
    task.Pop();task.Push(8000,10000);task.Set(UINT64_MAX/2,UINT64_MAX);assert(task.value>=8999&&task.value<=9000);
    task.Pop();assert(nexus::WorkPart(1,2)==5000&&nexus::WorkPart(0,0)==0);
    std::cout<<"Whole-task progress: 50 files, nested phases, monotonic percentages, unknown totals, failure and success passed\n";
}
