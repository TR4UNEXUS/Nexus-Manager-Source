"""Compile the production reference scanner and transfer messages on the host.

Run with Python on a compiler host (including WSL). The compiler defaults to c++;
use --compiler clang++ or --compiler /path/to/zig c++ for another toolchain.
"""
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', nargs='+', default=['c++'])
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    app = Path(__file__).resolve().parents[1]
    out = args.out or app / 'build/installed-move-tests'
    out.mkdir(parents=True, exist_ok=True)
    for name in ('installed_move_core_tests', 'transfer_report_tests'):
        for optimization in ('O0', 'O2'):
            binary = (out / (name + '-' + optimization + ('.exe' if sys.platform == 'win32' else ''))).resolve()
            command = args.compiler + ['-std=c++20', '-' + optimization, '-Wall', '-Wextra', '-Werror',
                                      '-I' + str(app / 'sphaira/include'), str(app / 'tests' / (name + '.cpp')),
                                      '-o', str(binary)]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
