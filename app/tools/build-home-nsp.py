"""Build/round-trip verify the USB Loader HOME launcher. No keys in output/logs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

TITLE = "05005553424c0000"
TARGET = b"sdmc:/switch/usbloader/USBLoader.nro"

def run(exe, args, cwd):
    # These utilities can print derived keys. Never capture or publish their output.
    result = subprocess.run([str(exe), *map(str, args)], cwd=cwd,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            timeout=120)
    if result.returncode:
        raise RuntimeError(f"{Path(exe).name} failed, exit {result.returncode}; output suppressed")

def extract_pfs(data, dest):
    assert data[:4] == b"PFS0"
    count, strings = struct.unpack_from("<II", data, 4)
    assert 0 < count <= 16
    base = 16 + count * 24 + strings
    assert base <= len(data)
    names = data[16 + count * 24:base]
    for i in range(count):
        off, size, nameoff = struct.unpack_from("<QQI", data, 16 + 24 * i)
        assert nameoff < len(names) and b"\0" in names[nameoff:]
        name = names[nameoff:].split(b"\0", 1)[0].decode("ascii")
        assert name and "/" not in name and "\\" not in name and ".." not in name
        assert off <= len(data)-base and size <= len(data)-base-off
        path = dest / name
        with path.open("xb") as f: f.write(data[base+off:base+off+size])

def main():
    p=argparse.ArgumentParser()
    for name in ("keys", "packer", "npdmtool", "hactool", "output"):
        p.add_argument("--"+name, required=True, type=Path)
    a=p.parse_args()
    app=Path(__file__).resolve().parents[1]
    a.output=a.output.resolve()
    if a.output.exists(): raise RuntimeError("Output already exists; no overwrite")
    for f in (a.keys,a.packer,a.npdmtool,a.hactool):
        if not f.is_file(): raise RuntimeError("Missing build prerequisite")
    stage=Path(tempfile.mkdtemp(prefix="home-nsp-",dir=app/"build"))
    for folder in ("exefs","romfs","control","logo","unpacked","verified"):
        (stage/folder).mkdir()
    nso=app/"build/Nexus/hbl/exefs/main"
    assert nso.read_bytes()[:4]==b"NSO0"
    shutil.copy2(nso,stage/"exefs/main")
    cfg=json.loads((app/"hbl/hbl.json").read_text())
    cfg.update(name="USB Loader",title_id="0x"+TITLE,title_id_range_min="0x"+TITLE,
               title_id_range_max="0x"+TITLE,address_space_type=3)
    for cap in cfg["kernel_capabilities"]:
        if cap["type"]=="kernel_flags":
            cap["value"].update(highest_thread_priority=63,lowest_thread_priority=28,
                                highest_cpu_id=3,lowest_cpu_id=0)
    config=stage/"home.json";config.write_text(json.dumps(cfg,indent=2))
    run(a.npdmtool,[config,stage/"exefs/main.npdm"],stage)
    for name in ("nextNroPath","nextArgv"):(stage/"romfs"/name).write_bytes(TARGET)
    nro=(app/"build/Nexus/switch/usbloader/USBLoader.nro").read_bytes()
    assert nro[16:20]==b"NRO0"
    aset=struct.unpack_from("<I",nro,24)[0]
    assert nro[aset:aset+4]==b"ASET"
    off,size=struct.unpack_from("<QQ",nro,aset+24)
    assert size==0x4000 and aset+off+size<=len(nro)
    nacp=nro[aset+off:aset+off+size]
    assert nacp[:10]==b"USB Loader" and nacp[0x3060:0x3066]==b"0.6.7\0"
    (stage/"control/control.nacp").write_bytes(nacp)
    shutil.copy2(app/"assets/usb-loader-icon.jpg",stage/"control/icon_AmericanEnglish.dat")
    run(a.packer,["--keyset",a.keys.resolve(),"--titleid",TITLE,"--keepncadir"],stage)
    nsp=stage/"hacbrewpack_nsp"/(TITLE+".nsp")
    assert nsp.is_file()
    data=nsp.read_bytes();extract_pfs(data,stage/"unpacked")
    ncas=list((stage/"unpacked").glob("*.nca"));assert len(ncas)==3
    for file in ncas:
        assert file.name[:32]==hashlib.sha256(file.read_bytes()).hexdigest()[:32]
        run(a.hactool,["-x","--disablekeywarns","--suppresskeys","-k",a.keys.resolve(),
            "--exefsdir="+str(stage/"verified/exefs"),"--romfsdir="+str(stage/"verified/romfs"),file],stage)
    verified=stage/"verified"
    assert (verified/"exefs/main").read_bytes()==nso.read_bytes()
    assert (verified/"exefs/main.npdm").read_bytes()==(stage/"exefs/main.npdm").read_bytes()
    from sys import path as import_path
    import_path.insert(0,str(app/"tests"))
    from forwarder_npdm_tests import verify_npdm
    verify_npdm((verified/"exefs/main.npdm").read_bytes(),int(TITLE,16))
    for name in ("nextNroPath","nextArgv"):assert (verified/"romfs"/name).read_bytes()==TARGET
    # hacBrewPack rewrites these documented application-identity fields.
    expected=bytearray(nacp);tid=int(TITLE,16)
    for off in (0x3038,0x3078,*range(0x30b0,0x30f0,8)):struct.pack_into("<Q",expected,off,tid)
    struct.pack_into("<Q",expected,0x3070,tid+0x1000)
    expected[0x30f1]=0 # automatic logo handling
    expected[0x3212]=0 # program index
    expected[0x34d1]=0 # initial program index
    struct.pack_into("<I",expected,0x34d4,1) # single program
    assert (verified/"romfs/control.nacp").read_bytes()==expected
    a.output.parent.mkdir(parents=True,exist_ok=True)
    with a.output.open("xb") as f:f.write(data)
    manifest={"app":"USB Loader","version":"0.6.7","title_id":TITLE,
              "kind":"HOME launcher; matching NRO must remain on real SD", "target":TARGET.decode(),
              "nsp_sha256":hashlib.sha256(data).hexdigest(),"nro_sha256":hashlib.sha256(nro).hexdigest(),
              "embedded_hbl_sha256":hashlib.sha256(nso.read_bytes()).hexdigest(),
              "address_space_bits":39,"cpu_core_mask":"0xF (cores 0-3)","virtual_sd_protocol":1,"console_tested":False,
              "checks":["PFS bounds","3 NCA content hashes","decrypted ExeFS bytes", "NPDM bytes",
                        "RomFS target/arguments","NACP bytes"],"stage":str(stage)}
    a.output.with_suffix(".json").write_text(json.dumps(manifest,indent=2))
    print(json.dumps(manifest,indent=2))

if __name__=="__main__":main()
