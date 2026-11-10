"""Run the production startup-monitor helper with injected thread results.

The filesystem-before-thread order is checked by check_focused_build.ps1.
This host test covers failure handling and subsequent chainload behavior;
it does not simulate the kernel or an application's NRO entry point.
"""
from pathlib import Path
import re
import subprocess
import os

app = Path(__file__).resolve().parents[1]
text = (app / 'hbl/source/main.c').read_text()
helper = re.search(r'static bool g_launchMonitorUnavailable;\nstatic void startLaunchMonitor\(void\) \{.*?\n\}', text, re.S)
assert helper is not None
out = app / 'build/homebrew-tests'
out.mkdir(parents=True, exist_ok=True)
unit = out / 'hbl_startup_generated.c'
unit.write_text(r'''
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uint32_t Result;
#define R_FAILED(rc) ((rc) != 0)
static Result next_result;
static unsigned starts, traces;
Result nexusWatchdogStart(uint64_t timeout) {
    assert(timeout == 30000000000ULL);
    ++starts;
    return next_result;
}
void nexusTrace(const char *stage, Result rc, uint64_t value) {
    assert(!value);
    if (!(traces % 2)) {
        assert(!strcmp(stage, "watchdog-start-begin") && !rc);
    } else {
        assert(!strcmp(stage, "watchdog-start-end") && rc == next_result);
    }
    ++traces;
}
''' + helper.group(0) + r'''
int main(void) {
    startLaunchMonitor();
    assert(starts == 1 && traces == 2 && !g_launchMonitorUnavailable);
    startLaunchMonitor();
    assert(starts == 2 && traces == 4 && !g_launchMonitorUnavailable);
    const Result failures[] = {1, 2, 0x1759};
    for (unsigned i=0; i<sizeof(failures)/sizeof(failures[0]); ++i) {
        starts=traces=0;
        g_launchMonitorUnavailable=false;
        next_result=failures[i];
        startLaunchMonitor();
        assert(starts == 1 && traces == 2 && g_launchMonitorUnavailable);
        next_result=0;
        startLaunchMonitor();
        assert(starts == 1 && traces == 2 && g_launchMonitorUnavailable);
    }
    puts("Production HBL monitor helper: success, recorded errors, nonfatal return, no repeated failed starts passed");
    return 0;
}
''')
exe = out / 'hbl_startup_tests.exe'
compiler=os.environ.get('CC','C:/devkitPro/msys2/usr/bin/gcc.exe' if os.name=='nt' else 'gcc')
subprocess.run([compiler, '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', str(unit), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
