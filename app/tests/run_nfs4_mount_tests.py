#!/usr/bin/env python3
"""Run actual vendored NFSv4 connection callbacks with sanitized RPC mocks."""
from pathlib import Path
import argparse
import os
import re
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--legacy-source',type=Path,help='Privately retained pre-fix callback source; must reproduce the NULL crash')
args=parser.parse_args()
source=(args.legacy_source or root/'vendor/libnfs/lib/nfs_v4.c').read_text()
fixed=(root/'vendor/libnfs/lib/nfs_v4.c').read_text()
connect=(root/'vendor/libnfs/lib/libnfs.c').read_text()
header=(root/'vendor/libnfs/include/nfsc/libnfs-raw.h').read_text()
def block(source,signature):
    start=source.index(signature);brace=source.index('{',start);depth,end=1,brace+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
fixture=(root/'tests/nfs4_mount_fixture.c').read_text()
constants='\n'.join(re.findall(r'^#define RPC_STATUS_(?:SUCCESS|ERROR|CANCEL|TIMEOUT)\s+\d+.*$',header,re.M))
assert len(constants.splitlines())==4
fixture=fixture.replace('/* RPC_STATUS_CONSTANTS */',constants)
fixture=fixture.replace('/* PRODUCTION_CHECK */',block(source,'static int\ncheck_nfs4_error('))
fixture=fixture.replace('/* PRODUCTION_MOUNT */',block(source,'static void\nnfs4_mount_1_cb('))
fixture=fixture.replace('/* PRODUCTION_CONNECT */',block(connect,'static void\nrpc_connect_program_5_cb('))
fixture=fixture.replace('/* PRODUCTION_FIND */',block(fixed,'static int\nnfs4_find_op('))
fixture=fixture.replace('/* PRODUCTION_DECODE */',block(fixed,'static uint64_t\nnfs_pntoh64('))
fixture=fixture.replace('/* PRODUCTION_BOUNDS */',fixed[fixed.index('#define CHECK_GETATTR_BUF_SPACE'):fixed.index('static uint64_t\nspecdata4_to_rdev(')])
fixture=fixture.replace('/* PRODUCTION_RWMAX */',block(fixed,'static int\nnfs_parse_rwmax('))
fixture=fixture.replace('/* PRODUCTION_MOUNT5 */',block(fixed,'static void\nnfs4_mount_5_cb('))
with tempfile.TemporaryDirectory(prefix='nexus-nfs4-mount-') as directory:
    work=Path(directory);generated=work/'mount.c';generated.write_text(fixture)
    for optimization in ('-O0','-O2'):
        binary=work/('mount'+optimization)
        subprocess.run(['gcc','-std=c11',optimization,'-g','-UNDEBUG','-Wall','-Wextra','-Werror',
            '-Wno-unused-parameter','-Wno-sign-compare','-fsanitize=address,undefined','-fno-sanitize-recover=all',
            '-fno-omit-frame-pointer',str(generated),'-o',str(binary)],check=True)
        if args.legacy_source:
            with tempfile.TemporaryFile() as errors:
                result=subprocess.run([str(binary),'legacy'],stdout=subprocess.DEVNULL,stderr=errors,timeout=10,
                    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:handle_segv=0',UBSAN_OPTIONS='halt_on_error=1'))
                errors.seek(0);detail=errors.read(8192).decode(errors='replace')
            assert result.returncode!=0 and 'member access within null pointer' in detail,detail
            print('PASS reproduced original NULL COMPOUND dereference after successful NFS connect ('+optimization+')')
        else:
            subprocess.run([str(binary)],check=True,timeout=20)
