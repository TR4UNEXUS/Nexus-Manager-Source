"""Verify the built test NRO's identity and exact embedded diagnostic loader."""
from pathlib import Path
import hashlib
import json
import struct

app = Path(__file__).resolve().parents[1]
build = app / "build/NexusPorts"
binary = build / "sphaira.nro"
data = binary.read_bytes()
assert data[0x10:0x14] == b"NRO0"
code_size = struct.unpack_from("<I", data, 0x18)[0]
assert data[code_size:code_size+4] == b"ASET"
nacp_offset, nacp_size = struct.unpack_from("<QQ", data, code_size+24)
nacp = data[code_size+nacp_offset:code_size+nacp_offset+nacp_size]
assert len(nacp) == 0x4000 and nacp == (build / "sphaira/sphaira.nacp").read_bytes()
def text(offset, size):
    return nacp[offset:offset+size].split(b"\0", 1)[0].decode("utf-8")
assert text(0, 0x200) == "Nexus Manager"
assert text(0x200, 0x100) == "TechRepairs4U"
assert text(0x3060, 16) == "0.1.11-SD8"
embedded = {}
for name in ("main", "main.npdm"):
    payload = (build / "hbl/exefs" / name).read_bytes()
    assert payload and payload in data, f"NRO does not embed current HBL {name}"
    embedded[name] = {"bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}
assert data == (build / "switch/nexusmanager/NexusManager.nro").read_bytes()
receipt = {
    "passed": True,
    "artifact": str(binary), "bytes": len(data),
    "sha256": hashlib.sha256(data).hexdigest(),
    "title": text(0, 0x200), "author": text(0x200, 0x100),
    "version": text(0x3060, 16), "embedded_hbl": embedded,
    "protocol_sha256": hashlib.sha256((app / "sphaira/include/nexus/startup_timing_protocol.h").read_bytes()).hexdigest(),
    "hardware_tested": False,
}
sources = [
    "build-nexus-wsl.sh", "hbl/CMakeLists.txt", "hbl/source/main.c",
    "hbl/source/nexus_trace.c", "hbl/source/nexus_trace.h",
    "hbl/source/nexus_timing.c", "hbl/source/nexus_timing.h",
    "hbl/source/nexus_vsd.c", "hbl/source/nexus_read.h",
    "sphaira/CMakeLists.txt", "sphaira/include/nexus/startup_timing_protocol.h",
    "sphaira/include/nexus/homebrew.hpp", "sphaira/include/owo.hpp",
    "sphaira/include/ui/menus/nexus_menu.hpp", "sphaira/source/nexus/homebrew.cpp",
    "sphaira/source/owo.cpp", "sphaira/source/ui/menus/nexus_menu.cpp",
]
receipt["local_source_sha256"] = {
    name: hashlib.sha256((app/name).read_bytes()).hexdigest() for name in sources
}
receipt["files"] = {str(app/name): digest for name, digest in receipt["local_source_sha256"].items()}
for path in (binary, build/"hbl/exefs/main", build/"hbl/exefs/main.npdm", build/"switch/nexusmanager/NexusManager.nro"):
    receipt["files"][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
receipt["local_validation_logs"] = {
    name: hashlib.sha256((app/"build"/name).read_bytes()).hexdigest()
    for name in ("SD8-timing-build.log", "SD8-timing-tests.log", "SD8-manager-validation.txt")
}
receipt["validation"] = {
    "cross_build": "sphaira_nro target succeeded; current HBL bytes embedded exactly",
    "host_tests": "tests/run_hbl_timing_tests.sh passed",
    "legacy_focused_source_checks": "stopped on pre-existing MenuIcon::Cleanup naming guard",
    "build_command": "NEXUS_VERSION=0.1.11-SD8 NEXUS_BUILD_JOBS=6 sh build-nexus-wsl.sh",
}
result = json.dumps(receipt, indent=2)
(app / "build/SD8-manager-build-receipt.json").write_text(result+"\n")
print(result)
