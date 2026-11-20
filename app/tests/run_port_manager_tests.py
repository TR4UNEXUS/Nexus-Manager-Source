"""Run production port/mod recovery tests at both optimization levels."""
from pathlib import Path
import subprocess,tempfile
app=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="nexus-port-manager-") as temp:
 for optimization in ("O0","O2"):
  exe=Path(temp)/optimization
  subprocess.run(["g++","-std=c++20","-"+optimization,"-UNDEBUG","-fsanitize=undefined","-Wall","-Wextra","-Werror","-Wno-misleading-indentation","-I"+str(app/"sphaira/include"),str(app/"tests/port_manager_tests.cpp"),"-o",str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
