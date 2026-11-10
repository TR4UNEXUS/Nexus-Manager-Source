"""App-only, verified 0.6.9 runtime and corresponding-source packages."""
import argparse,hashlib,json,struct,zipfile
from pathlib import Path,PurePosixPath
from elftools.elf.elffile import ELFFile
app=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--out',required=True)
p.add_argument('--confirmed-app',required=True)
a=p.parse_args();out=Path(a.out).resolve();old=Path(a.confirmed_app).resolve();out.mkdir(parents=True,exist_ok=True)
firmware=json.loads((app.parents[1]/'outputs/Nexus-USB-Stability-T1-20261001/manifest.json').read_text())
sha=lambda b:hashlib.sha256(b).hexdigest()
nro=(app/'build/Nexus/switch/usbloader/USBLoader.nro').read_bytes()
assert nro[0x10:0x14]==b'NRO0'
aset=struct.unpack_from('<I',nro,0x18)[0];assert nro[aset:aset+4]==b'ASET'
for at in (8,24,40):
    off,size=struct.unpack_from('<QQ',nro,aset+at);assert off>=56 and size and aset+off+size<=len(nro)
nacp=aset+struct.unpack_from('<Q',nro,aset+24)[0]
assert nro[nacp:nacp+512].split(b'\0')[0]==b'USB Loader'
assert nro[nacp+0x3060:nacp+0x3070].split(b'\0')[0]==b'0.6.9'
assert b'APP-0.6.9 seq=' in nro and b'USB Mods' in nro and b'No supported mods here' in nro
assert b'usb-pinned-open-T1' in nro
changed=[]
for folder in ('sphaira','hbl','common'):
    for f in (old/folder).rglob('*'):
        if f.is_file() and f.read_bytes()!=(app/f.relative_to(old)).read_bytes():changed.append(f.relative_to(old).as_posix())
assert set(changed)=={'sphaira/CMakeLists.txt','sphaira/source/nexus/nx_Client.cpp','sphaira/source/nexus/startup_trace.cpp'},changed
for rel in ('hbl/source/main.c','hbl/source/nexus_vsd.c','hbl/source/nexus_trace.c','hbl/source/trampoline.s','hbl/hbl.json','common/watchdog.c','sphaira/source/owo.cpp','sphaira/source/main.cpp','sphaira/source/app.cpp','sphaira/source/nexus/homebrew.cpp'):
    assert (app/rel).read_bytes()==(old/rel).read_bytes(),rel
with (old/'build/Nexus/hbl/hbl.elf').open('rb') as f,(app/'build/Nexus/hbl/hbl.elf').open('rb') as g:
    x,y=ELFFile(f),ELFFile(g)
    matched=[]
    for s in x.iter_sections():
        if s['sh_flags']&2 and s.name!='.note.gnu.build-id':
            t=y.get_section_by_name(s.name);assert t and s['sh_addr']==t['sh_addr'] and s['sh_size']==t['sh_size'] and s.data()==t.data(),s.name;matched.append(s.name)
embedded=(app/'build/Nexus/hbl/exefs/main').read_bytes()
assert nro.count(embedded)==1 and b'Nexus Virtual SD probe v1' not in nro
cache=(app/'build/Nexus/CMakeCache.txt').read_text()
for option in ('NEXUS_BUNDLED_PROBE','ENABLE_LIBUSBHSFS','ENABLE_LIBUSBDVD'):assert option+':BOOL=OFF' in cache
manifest=dict(version='0.6.9',nro_sha256=sha(nro),firmware_included=False,hardware_tested=False,
    required_package3_sha256=firmware['new_sha256'],
    existing_home_nsp_reinstall_required=False,existing_forwarders_changed=False,bundled_probe=False,
    embedded_hbl_version='0.6.7',embedded_hbl_sha256=sha(embedded),hbl_alloc_sections_identical_except_build_id=matched,
    imported_types=['RomFS directory index','explicit ExeFS cache','explicit build-ID IPS/IPS32'],
    pinned_usb_ncm_command=65031,registration_automatic=False,raw_usb_owner=False,game_data_copied=False,game_data_deleted=False,
    tests=['M2 production index validator','GTA 105-file byte-identical index','legacy M1 journal migration','every transaction mutation fault','interrupted rollback recovery','ownership/conflict/media/path/limit checks','forwarder/startup/worker regression tests','UI geometry six tiles'])
runtime=out/'USB-Loader-0.6.9-Stability-T1-20261001.zip'
source=out/'USB-Loader-0.6.9-Source-20261001.zip'
def safe(name):
    q=PurePosixPath(name)
    assert not q.is_absolute() and '..' not in q.parts and '\\' not in name
    assert not {'.git','__pycache__','captures','fatal_errors','crash_reports'}&set(q.parts)
    assert q.name.lower() not in {'prod.keys','title.keys','keys.dat','package3','hoc.kip','hbl.nsp'}
    assert q.suffix.lower() not in {'.token','.utr','.nca','.xci','.nsz','.nsp','.kip','.kip1'}
def add(z,name,b):safe(name);z.writestr(name,b)
with zipfile.ZipFile(runtime,'x',zipfile.ZIP_DEFLATED) as z:
    add(z,'switch/usbloader/USBLoader.nro',nro)
    for src,dst in [('RELEASE-0.6.9.txt','READ-ME-FIRST.txt'),('tools/Apply-App.ps1','Apply-App.ps1'),('LICENSE','LICENSE'),('ATTRIBUTION.md','ATTRIBUTION.md'),('DEPENDENCIES.json','DEPENDENCIES.json')]:add(z,dst,(app/src).read_bytes())
    add(z,'BUILD-MANIFEST.json',json.dumps(manifest,indent=2).encode())
    for dep in (app/'vendor').iterdir():
        if dep.is_dir():
            for f in dep.iterdir():
                if f.is_file() and f.name.upper().startswith(('LICENSE','LICENCE','COPYING','COPYRIGHT')):add(z,'licenses/'+dep.name+'/'+f.name,f.read_bytes())
with zipfile.ZipFile(source,'x',zipfile.ZIP_DEFLATED) as z:
    for f in sorted(app.rglob('*')):
        if not f.is_file():continue
        rel=f.relative_to(app);name=rel.as_posix()
        if rel.parts[0] in {'build','.github','.vscode'} or {'.git','__pycache__','node_modules'}&set(rel.parts):continue
        if name in {'build-output.log','build-error.log','BUILD-MANIFEST.json'}:continue
        # zstd/build/cmake is SOURCE, not generated build output: retain it.
        add(z,'USB-Loader/'+name,f.read_bytes())
    add(z,'USB-Loader/BUILD-MANIFEST.json',json.dumps(manifest,indent=2).encode())
for f in (runtime,source):
    with zipfile.ZipFile(f) as z:
        assert z.testzip() is None;assert len(z.namelist())==len(set(z.namelist()))
        for n in z.namelist():safe(n)
    print(json.dumps(dict(file=str(f),size=f.stat().st_size,sha256=sha(f.read_bytes()))))
print(json.dumps(manifest,indent=2))
