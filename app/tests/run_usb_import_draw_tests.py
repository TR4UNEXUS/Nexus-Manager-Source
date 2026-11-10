# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile actual production ReviewMenu with deterministic geometry instrumentation."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "sphaira/source/ui/menus/usb_import_menu.cpp").read_text()
start = source.index("struct ReviewRow ")
end = source.index("\nview::Kind Kind(", start)
fixture = (root / "tests/usb_import_draw_fixture.cpp").read_text()
with tempfile.TemporaryDirectory(prefix="usb-import-draw-") as tmp:
    generated = Path(tmp) / "draw.cpp"
    generated.write_text(fixture.replace("// PRODUCTION_REVIEW_MENU", source[start:end]))
    for name, flags in (("debug", ["-O0"]), ("optimized", ["-O2"]), ("ubsan", ["-O1", "-fsanitize=undefined", "-fno-sanitize-recover=all"])):
        binary = Path(tmp) / name
        subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", *flags,
                        "-I", str(root / "sphaira/include"), str(generated), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
