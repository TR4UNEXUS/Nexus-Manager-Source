"""Test the production forwarder NPDM patch against the real embedded template."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
from forwarder_romfs_tests import APP, between


def verify_npdm(data, tid, address=3):
    def u32(offset):
        assert 0 <= offset <= len(data) - 4
        return struct.unpack_from("<I", data, offset)[0]

    def u64(offset):
        assert 0 <= offset <= len(data) - 8
        return struct.unpack_from("<Q", data, offset)[0]

    assert data[:4] == b"META" and (data[12] >> 1) & 7 == address, "wrong address-space type"
    aci, aci_size, acid, acid_size = struct.unpack_from("<IIII", data, 0x70)
    assert aci + aci_size <= len(data) and acid + acid_size <= len(data)
    assert data[aci:aci+4] == b"ACI0" and data[acid+0x200:acid+0x204] == b"ACID"
    assert u64(aci+16) == tid and u64(acid+0x210) == tid and u64(acid+0x218) == tid
    for label, base, total, kac_fields in (("ACI0", aci, aci_size, 0x30), ("ACID", acid, acid_size, 0x230)):
        offset, size = u32(base+kac_fields), u32(base+kac_fields+4)
        assert offset + size <= total and size % 4 == 0
        caps = [u32(base+offset+i) for i in range(0, size, 4)]
        flags = [c for c in caps if c & 15 == 7]
        assert len(flags) == 1, label+": missing/duplicate kernel flags"
        cap = flags[0]
        assert cap == 0x030073F7, label+": not the four-core descriptor"
        assert (cap >> 16) & 255 == 0 and (cap >> 24) & 255 == 3, label+": CPU range not 0-3"
        assert (cap >> 4) & 63 == 63 and (cap >> 10) & 63 == 28, label+": priority range"
        app_type = [c for c in caps if c & 0x3FFF == 0x1FFF]
        assert len(app_type) == 1 and (app_type[0] >> 14) & 7 == 1, label+": not an application"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--compiler", default="C:/devkitPro/msys2/usr/bin/g++.exe")
    args = p.parse_args()
    source = (APP/"sphaira/source/owo.cpp").read_text()
    definitions = (APP/"sphaira/include/owo.hpp").read_text()
    wire = (APP/"sphaira/include/yati/nx/npdm.hpp").read_text().split("namespace sphaira::npdm {", 1)[1].split("} // namespace", 1)[0]
    core = (APP/"sphaira/include/utils/core.hpp").read_text().split("enum class CpuCoreMode", 1)[1].split("};", 1)[0]
    enums = "enum class ForwarderAddressSpace" + between(definitions, "enum class ForwarderAddressSpace", "struct OwoConfig {")
    patch = "struct NpdmPatch {" + between(source, "struct NpdmPatch {", "struct NcapPatch {")
    functions = "auto npdm_patch_kc(" + between(source, "auto npdm_patch_kc(", "void patch_nacp(")
    homebrew = (APP/"sphaira/source/nexus/homebrew.cpp").read_text()
    assert "cfg.address_space=ForwarderAddressSpace::Bit39;" in homebrew
    assert "cfg.core_mode=CpuCoreMode::Four;" in homebrew
    assert "address_space{ForwarderAddressSpace::Bit39}" in definitions
    assert "core_mode{CpuCoreMode::Four}" in definitions
    out = APP/"build/homebrew-tests"
    out.mkdir(parents=True, exist_ok=True)
    unit = out/"forwarder_npdm_generated.cpp"
    unit.write_text("""
#include <cstdint>
#include <cstring>
#include <vector>
#include <fstream>
#include <iterator>
#include <cassert>
using u8=uint8_t;using u32=uint32_t;using u64=uint64_t;
#define BIT(n) (1U<<(n))
#define R_FAILED(rc) ((rc)!=0)
#define MAKEHOSVERSION(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define ON_SCOPE_EXIT(call) ((void)0)
enum SplConfigItem {Dummy};
int splInitialize(){return 0;}
void splExit(){}
int splGetConfig(SplConfigItem,u64* value){*value=(u64)MAKEHOSVERSION(1,12,0)<<40;return 0;}
""" + "namespace npdm {"+wire+"}\n" + "enum class CpuCoreMode"+core+"};\n" + enums + patch + functions + """
int main(int argc,char** argv){
    assert(argc==4);
    std::ifstream in(argv[1],std::ios::binary);
    std::vector<u8> data((std::istreambuf_iterator<char>(in)),{});
    assert(data.size()>=0x300 && !std::memcmp(data.data(),"META",4));
    NpdmPatch patch{};patch.tid=0x0500012345678000ULL;
    assert(patch.address_space==ForwarderAddressSpace::Bit39 && patch.core_mode==CpuCoreMode::Four);
    patch.address_space=argv[3][0]=='1'?ForwarderAddressSpace::Bit36:ForwarderAddressSpace::Bit39;
    assert(patch_npdm(data,patch));
    std::ofstream out(argv[2],std::ios::binary);out.write((const char*)data.data(),data.size());
    assert(bool(out));
}
""")
    exe = out/"forwarder_npdm_tests.exe"
    subprocess.run([args.compiler,"-std=c++20","-O1","-Wall","-Wextra",str(unit),"-o",str(exe)],check=True)
    build=Path(os.environ.get("NEXUS_TEST_BUILD_DIR",str(APP/"build/Nexus")))
    for address in (3, 1):
        artifact = out/('usb-forwarder.npdm' if address == 3 else 'port-forwarder-36bit.npdm')
        subprocess.run([str(exe),str(build/"hbl/exefs/main.npdm"),str(artifact),str(address)],check=True)
        verify_npdm(artifact.read_bytes(),0x0500012345678000,address)
    print("Production NPDM patch: 36-bit and 39-bit, cores 0-3, priorities 28-63, application type and title ID verified in both ACI0/ACID")


if __name__ == "__main__":
    main()
