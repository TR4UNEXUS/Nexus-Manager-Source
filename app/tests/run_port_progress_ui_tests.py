"""Exercise the production progress setters with platform/display calls mocked.

Child forwarder installation must not reset a folder transfer's counters.
"""
from pathlib import Path
import subprocess

app = Path(__file__).resolve().parents[1]
out = app / 'build/port-transfer-tests'
out.mkdir(parents=True, exist_ok=True)
source = (app / 'sphaira/source/ui/progress_box.cpp').read_text()
names = ('SetTitle', 'NewTransfer', 'ResetTranfser', 'UpdateTransfer',
         'SetOverallTransfer', 'SetProgressNote', 'SetDetailedProgress', 'BeginProgressRange', 'EndProgressRange')
methods = []
declarations = []
for name in names:
    start = source.index(('void ' if name in ('BeginProgressRange','EndProgressRange') else 'auto ') + 'ProgressBox::' + name + '(')
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    methods.append(source[start:end])
    declarations.append(source[start:brace].replace('ProgressBox::', '') + ';')
fixture = r'''
#include "nexus/transfer_activity.hpp"
#include "nexus/task_progress.hpp"
#include <cassert>
#include <cstdint>
#include <string>
#include <iostream>
using s64 = std::int64_t;
#define SCOPED_MUTEX(x)
unsigned armGetSystemTick(){return 1;}
struct Clock {void Update(){}};
struct Safety {void Touch(unsigned){}};
struct ProgressBox {
    s64 m_size{}, m_offset{}, m_last_offset{}, m_speed{};
    bool m_overall{}, m_finishing{}, m_detailed{},m_transfer_log{};
    std::string m_title, m_transfer, m_progress_note, m_byte_detail;
    Clock m_timestamp; Safety m_safety; nexus::TaskProgress m_task_progress;
    DECLARATIONS
};
METHODS
int main(){
    ProgressBox box;
    box.SetTitle("Left 4 Dead");
    box.SetDetailedProgress(35,"Copying game files","Copied 0.5 MB / 5.0 GB","18.0 MB/s");
    // These are real child-workflow calls used during shortcut installation.
    box.NewTransfer("Creating Program").UpdateTransfer(0,8);
    box.ResetTranfser();
    box.SetOverallTransfer(100,100,"Child complete",true);
    box.SetProgressNote("Child note");box.SetTitle("Child title");
    assert(box.m_size==10000 && box.m_offset==3500);
    assert(box.m_title=="Left 4 Dead" && box.m_transfer=="Copying game files");
    assert(box.m_byte_detail=="Copied 0.5 MB / 5.0 GB" && box.m_progress_note=="18.0 MB/s");
    box.SetDetailedProgress(75,"Creating HOME shortcut","Copied 5.0 GB / 5.0 GB","Keep storage connected");
    box.SetDetailedProgress(10,"Late update","wrong","wrong");
    assert(box.m_offset==7500 && box.m_transfer=="Creating HOME shortcut");
    box.SetDetailedProgress(100,"Port move complete","Copied 5.0 GB / 5.0 GB","Verified");
    assert(box.m_offset==9999 && box.m_progress_note=="Verified");
    // Ordinary file transfers retain their existing byte progress behavior.
    ProgressBox ordinary;
    ordinary.NewTransfer("file").UpdateTransfer(512,1024);
    assert(ordinary.m_offset==512 && ordinary.m_size==1024);
    ordinary.SetOverallTransfer(600,1024,"next",false);
    ordinary.SetOverallTransfer(500,1024,"stale",false);
    assert(ordinary.m_offset==5859 && ordinary.m_transfer=="next");
    ProgressBox batch;
    for(unsigned file=0;file<50;++file){
        batch.BeginProgressRange(file*200,(file+1)*200);
        batch.NewTransfer("file");
        batch.UpdateTransfer(1,2);
        assert(batch.m_offset==file*200+100);
        batch.ResetTranfser();
        batch.UpdateTransfer(2,2);
        assert(batch.m_offset==std::min(9999U,(file+1)*200));
        batch.EndProgressRange();
    }
    batch.m_task_progress.Finish(false);assert(batch.m_task_progress.value==9999);
    batch.m_task_progress.Finish(true);assert(batch.m_task_progress.value==10000);
    std::cout<<"Production UI progress: child-workflow isolation, monotonic progress, completion and ordinary transfers passed\n";
}
'''.replace('DECLARATIONS', '\n'.join(declarations)).replace('METHODS', '\n'.join(methods))
test = out / 'port_progress_ui_tests.cpp'
test.write_text(fixture)
binary = out / 'port_progress_ui_tests'
subprocess.run(['g++', '-std=c++20', '-O1', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                '-I',str(app/'sphaira/include'),str(test), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
