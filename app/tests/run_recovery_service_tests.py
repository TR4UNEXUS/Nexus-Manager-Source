#!/usr/bin/env python3
"""Compile the real recovery scan, verification, report and journal adapters.

Horizon/filesystem IO and cryptographic primitives use the existing memory
fixture. No Switch data is modified. Actual content hashing has its own suite.
"""
from pathlib import Path
import re, subprocess, tempfile

A=Path(__file__).resolve().parents[1]
def strip_includes(text):
    return re.sub(r'^#(?:include "|pragma)[^\n]*\n','',text,flags=re.M)

source=(A/'sphaira/source/nexus/index_recovery.cpp').read_text()
source=strip_includes(source[:source.index('Result ReadUsbRoutingContent(')])+'\n}\n'
header=strip_includes((A/'sphaira/include/nexus/index_recovery.hpp').read_text())
hash_source=strip_includes((A/'sphaira/include/yati/nx/content_hash.hpp').read_text())
fixture=(A/'tests/recovery_fixture.hpp').read_text()
helpers=(A/'tests/recovery_regression.cpp').read_text().split('int main(){')[0]

with tempfile.TemporaryDirectory(prefix='nexus-recovery-') as tmp:
    T=Path(tmp)
    for suite in ('regression','mismatch','stale_routes','lr1'):
        cases=(A/f'tests/recovery_{suite}.cpp').read_text()
        if suite in ('stale_routes','lr1'): cases=helpers+cases
        generated=T/(suite+'.cpp')
        generated.write_text(fixture+'\n'+hash_source+'\n'+header+'\n'+source+'\n'+cases)
        for opt in ('-O0','-O2'):
            binary=T/(suite+opt)
            subprocess.run(['c++','-std=c++23',opt,'-Wall','-Wextra','-Werror',
                            '-fsanitize=undefined','-fno-sanitize-recover=all',
                            '-I'+str(A/'sphaira/include'),str(generated),'-o',str(binary)],check=True)
            subprocess.run([str(binary),str(T)],check=True)
    # Negative control: the previous routed GetPath decision fails before any
    # physical check. This is a temporary harness only, never a source rewrite.
    begin=source.index('    Result ContentPath(')
    end=source.index('    Result SameSize(',begin)
    legacy=source[:begin]+'''    Result ContentPath(const NcmContentInfo& info,std::string& path){
        char common[FS_MAX_PATH]{};R_TRY(ncmContentStorageGetPath(&native,common,sizeof(common),&info.content_id));
        R_UNLESS(nexus::ContentPath(common,false,path) && nexus::RegisteredFilePath(path),nexus::Invalid);return 0;
    }
'''+source[end:]
    control='''int main(){Reset();AddRecord();routed_path_result=0xA05;
        sphaira::ui::ProgressBox p;sphaira::nexusapp::RecoveryPlan plan;
        assert(sphaira::nexusapp::ScanUsbRecovery(&p,plan)==0xA05);
        assert(routed_path_queries==1 && writes==0);
        puts("PASS: old routed-path scan reproduces 0xA05 before recovery.");}
'''
    generated=T/'legacy.cpp'
    generated.write_text(fixture+'\n'+hash_source+'\n'+header+'\n'+legacy+'\n'+helpers+control)
    subprocess.run(['c++','-std=c++23','-O1','-I'+str(A/'sphaira/include'),str(generated),'-o',str(T/'legacy')],check=True)
    subprocess.run([str(T/'legacy')],check=True)
