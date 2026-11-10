"""Compile and run the production file-manager engine with two isolated disks."""
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
    out = args.out or app / 'build/file-manager-tests'
    out.mkdir(parents=True, exist_ok=True)
    for optimization in ('O0', 'O2'):
        binary = (out / ('file-manager-' + optimization + ('.exe' if sys.platform == 'win32' else ''))).resolve()
        command = args.compiler + ['-std=c++20', '-' + optimization, '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                                  '-Wno-misleading-indentation', '-I' + str(app / 'sphaira/include'),
                                  str(app / 'tests/file_manager_tests.cpp'), '-o', str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
