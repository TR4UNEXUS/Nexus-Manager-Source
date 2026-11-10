#!/usr/bin/env python3
"""Exercise actual RPC rejection reporting and preserve the sync mount reason."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'vendor/libnfs/lib/pdu.c').read_text()
start=source.index('static void rpc_report_rejected_reply(');brace=source.index('{',start);end=brace+1;depth=1
while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
assert 'rpc_report_rejected_reply(rpc, pdu, &msg.body.rbody.reply.rreply);' in source
fixture=(root/'tests/nfs_rpc_rejection_fixture.c').read_text().replace('/* PRODUCTION_REJECTED_REPLY */',source[start:end])
with tempfile.TemporaryDirectory(prefix='nexus-nfs-rpc-rejection-') as directory:
    work=Path(directory);test=work/'test.c';test.write_text(fixture)
    for opt in ('-O0','-O2'):
        binary=work/('test'+opt)
        subprocess.run(['gcc','-std=c11',opt,'-UNDEBUG','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(test),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
