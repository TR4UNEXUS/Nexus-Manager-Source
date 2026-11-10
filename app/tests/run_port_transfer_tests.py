"""Run the production folder-transfer engine and forwarder parser on the host.

Examples:
  python tests/run_port_transfer_tests.py --compiler clang++
  python tests/run_port_transfer_tests.py --compiler C:/tools/zig.exe c++
Add --inventory-root D:/ --nro /switch/left4dead/wine-nx-runtime.nro to
compare the production inventory with a read-only walk of a real port folder.
"""
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', nargs='+', default=['c++'])
    parser.add_argument('--out', type=Path)
    parser.add_argument('--inventory-root', type=Path)
    parser.add_argument('--nro', default='/switch/left4dead/wine-nx-runtime.nro')
    args = parser.parse_args()
    app = Path(__file__).resolve().parents[1]
    out = args.out or app / 'build/port-transfer-tests'
    out.mkdir(parents=True, exist_ok=True)
    names = ['port_transfer_tests', 'forwarder_contract_tests', 'library_tests']
    if args.inventory_root:
        names.append('port_inventory_tests')
    for name in names:
        binary = (out / (name + ('.exe' if sys.platform == 'win32' else ''))).resolve()
        command = args.compiler + ['-std=c++20', '-O1', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                                  '-I' + str(app / 'sphaira/include'), str(app / 'tests' / (name + '.cpp')),
                                  '-o', str(binary)]
        with (out / (name + '.compile.log')).open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            print((out / (name + '.compile.log')).read_text(), file=sys.stderr)
            raise SystemExit(result.returncode)
        run = [str(binary)]
        if name == 'port_inventory_tests':
            run += [str(args.inventory_root), args.nro]
        subprocess.run(run, check=True)


if __name__ == '__main__':
    main()
