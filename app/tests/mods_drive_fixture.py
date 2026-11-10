"""Read-only real drive comparison: metadata and existing 16-byte marker only.

No commercial payload is opened, copied, decrypted, or included in output.
Outputs are confined to the requested QA directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--usb',required=True)
p.add_argument('--sd',required=True)
p.add_argument('--title',required=True)
p.add_argument('--test-exe',required=True)
p.add_argument('--out',required=True)
a=p.parse_args()
usb,sd,out=Path(a.usb),Path(a.sd),Path(a.out)
out.mkdir(parents=True,exist_ok=True)
relative=f'/atmosphere/contents/{a.title}/romfs'
romfs=usb/relative.lstrip('/')
entries=[]
for f in sorted(romfs.rglob('*')):
    assert not f.is_symlink() and not (f.stat().st_file_attributes & 0x400)
    if f.name=='.nexus-mod-id' and f.parent==romfs:continue
    entries.append(dict(path=f.relative_to(romfs).as_posix(),directory=f.is_dir(),size=0 if f.is_dir() else f.stat().st_size))
fixture=out/'drive-metadata.json'
fixture.write_text(json.dumps(dict(root=relative,title=a.title,token=(romfs/'.nexus-mod-id').read_bytes().hex(),entries=entries)),encoding='utf-8')
generated=out/'app-generated-usb_romfs.idx'
def msys(path):
    return subprocess.check_output(['C:/devkitPro/msys2/usr/bin/cygpath.exe','-u',str(Path(path).resolve())],text=True).strip()
subprocess.run([str(Path(a.test_exe).resolve()),msys(fixture),msys(generated)],check=True)
current=sd/f'atmosphere/contents/{a.title}/usb_romfs.idx'
b=generated.read_bytes()
assert b==current.read_bytes(), 'App index differs from working M2 registration'
report=dict(index_identical=True,sha256=hashlib.sha256(b).hexdigest(),files=sum(not e['directory'] for e in entries),data_bytes=sum(e['size'] for e in entries),commercial_payload_read=False,drive_writes=False)
(out/'drive-comparison.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
