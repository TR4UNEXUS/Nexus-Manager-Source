from pathlib import Path
import subprocess,tempfile
A=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='nexus-meta-') as folder:
 for opt in ('-O0','-O2'):
  binary=Path(folder)/'test'
  subprocess.run(['c++','-std=c++23',opt,'-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(A/'sphaira/include'),str(A/'tests/verified_meta.cpp'),'-lcrypto','-o',str(binary)],check=True)
  subprocess.run([str(binary)],check=True)
