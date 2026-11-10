"""Run production network configuration and POSIX source-adapter tests (WSL)."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
out = root / 'build/network-tests'
out.mkdir(parents=True, exist_ok=True)
for test in ('network_location_tests', 'network_source_tests'):
    binary = out / test
    subprocess.run(['g++', '-std=c++20', '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                    '-Wno-misleading-indentation', '-I' + str(root / 'sphaira/include'),
                    str(root / 'tests' / (test + '.cpp')), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
