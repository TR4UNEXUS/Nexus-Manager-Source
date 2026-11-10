// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/file_manager_layout.hpp"
#include <cassert>
#include <iostream>
using namespace sphaira::ui::fileview;
int main(){
    static_assert(Rows==7);
    const auto clipboard=ClipboardLine();
    for(std::size_t i=0;i<70;++i){
        const auto row=Row(i);assert(row.x>=42 && row.x+row.w<=1238);
        assert(row.y>=174 && row.y+row.h<clipboard.y);
        assert(row.y==Row(i%Rows).y);
        if(i%Rows<Rows-1)assert(row.y+row.h<Row(i+1).y);
    }
    assert(clipboard.y+clipboard.h<653);
    for(bool cut:{false,true})for(bool success:{false,true})for(bool complete:{false,true})for(std::size_t removed:{0u,1u,100u})for(bool started:{false,true}){
        const bool expected=cut && (removed!=0 || started || (success && complete));
        assert(ClearCutAfterTransfer(cut,success,complete,removed,started)==expected);
    }
    // A partially removed cut is no longer a reusable intact selection; a
    // failed copy or pre-removal cut still leaves its clipboard available.
    assert(ClearCutAfterTransfer(true,false,false,1));
    assert(!ClearCutAfterTransfer(true,false,false,0));
    assert(!ClearCutAfterTransfer(false,true,true,0));
    assert(ClearCutAfterTransfer(true,false,false,0,true)); // first delete may have committed before reporting failure
    assert(IsSameOrChild("/ports/game","/ports/game"));
    assert(IsSameOrChild("/ports/game/data/a.bin","/ports/game"));
    assert(!IsSameOrChild("/ports/game2/data","/ports/game"));
    assert(!IsSameOrChild("/ports/game","/ports/game/data"));
    assert(!IsSameOrChild("/ports/game",""));
    assert(!IsSameOrChild("","/ports/game"));
    assert(IsSameOrChild("/ports/game","/"));
    std::cout<<"PASS file-manager row/clipboard separation, clipboard completion/failure states and deleted-subtree matching\n";
}
