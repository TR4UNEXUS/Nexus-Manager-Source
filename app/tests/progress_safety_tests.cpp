#include "nexus/progress_safety.hpp"
#include <stop_token>
#include <cassert>
#include <iostream>
int main(){
    using namespace nexus::safety;
    ProgressState state;std::stop_source cancel;
    state.Touch(100);assert(!state.CanDismiss() && !state.Slow(114,15));
    assert(state.Slow(115,15));
    cancel.request_stop();assert(cancel.stop_requested());
    // A cancelled operation whose IPC has not returned must retain its worker,
    // modal and buffers. Cancellation alone is never permission to dismiss.
    assert(!state.CanDismiss());
    assert(state.CanClose(Operation::ReadOnly,true));
    assert(!state.CanClose(Operation::MayWrite,true));
    assert(!state.CanClose(Operation::ReadOnly,false));
    state.Touch(120);assert(!state.Slow(121,15) && !state.Slow(119,15));
    state.Finish();assert(state.CanDismiss() && !state.Slow(1000,15));
    assert(!state.CanClose(Operation::ReadOnly,true));
    std::cout<<"progress safety cancellation/completion/read-only policy passed\n";
}
