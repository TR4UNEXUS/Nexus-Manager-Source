"""Check quick/deep physical I/O decisions using the real import inspector."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "sphaira/source/yati/yati.cpp").read_text()
start = source.index("enum class ExistingCheck")
end = source.index("\n}\n}", start) + 2
inspect = source[start:end]
fixture = (root / "tests/pc_import_checks_fixture.cpp").read_text()
start = source.index("Result PublishExistingUsbRecord(")
end = source.index("\n}\n", start) + 2
fixture = fixture.replace("// PRODUCTION_PUBLISH", source[start:end])
with tempfile.TemporaryDirectory() as temp:
    cpp = Path(temp) / "checks.cpp"
    binary = Path(temp) / "checks"
    cpp.write_text(fixture.replace("// PRODUCTION_CHECK", inspect.replace('"_nx', '"')))
    subprocess.run(["g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
