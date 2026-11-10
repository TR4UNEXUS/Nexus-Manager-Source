"""Compile actual installed-game cleanup with bounded fake platform services.

No production implementation is mirrored: function/struct bodies are extracted
from manager.cpp verbatim, then run with fake NCM/NS/storage APIs. A pre-fix
variant replaces only the reference-scan call with its previous LoadMeta call
to demonstrate that the same legacy-forwarder fixture reproduced the failure.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def block(source, marker):
    start = source.index(marker)
    begin = source.index('{', start)
    depth, end = 1, begin + 1
    # Selected source bodies have balanced braces even inside comments/strings.
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    if source[end:end+1] == ';':
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', nargs='+', default=['g++'])
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = root / 'build/installed-cleanup-tests'
    output.mkdir(parents=True, exist_ok=True)
    path = root / 'sphaira/source/nexus/manager.cpp'
    source = path.read_text()
    storage = '\n'.join(block(source, m) for m in ('struct Storage {', 'Result CheckStorage(', 'Result InspectContent('))
    markers = ('struct Meta {', 'struct TransferSession {', 'bool SameVerifiedMeta(', 'Result LoadMeta(',
               'struct SharedReferenceReader {', 'Result AppMetas(', 'static Result RemoveVerifiedSourceInSession(')
    production = '\n'.join(block(source, m) for m in markers)
    fixture = (root / 'tests/installed_cleanup_fixture.hpp').read_text()
    fixture = fixture.replace('// PRODUCTION_STORAGE', storage).replace('// PRODUCTION_META_AND_CLEANUP', production)
    fixture += '\n' + (root / 'tests/installed_cleanup_cases.cpp').read_text()
    old = '''SharedReferenceReader reader{p,src,key};
        R_TRY(nexus::installed_move::VisitReferences<NcmContentInfo>(reader,[&](const NcmContentInfo& info){
            contents.erase(HexId(&info.content_id));
        },Invalid));'''
    assert fixture.count(old) == 1, 'Production reference scan changed: review extraction'
    previous = fixture.replace(old, 'Meta m;R_TRY(LoadMeta(src.db,key,m));for(const auto& i:m.infos)contents.erase(HexId(&i.content_id));')
    env = dict(os.environ)
    compiler = Path(args.compiler[0])
    if compiler.is_file():
        env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    results = []
    for variant, contents in (('current', fixture), ('previous', previous)):
        generated = output / (variant + '.cpp')
        generated.write_text(contents)
        for mode in ('O0', 'O2'):
            binary = output / (variant + '-' + mode + ('.exe' if sys.platform == 'win32' else ''))
            command = args.compiler + ['-std=c++20', '-' + mode, '-Wall', '-Wextra', '-Werror',
                '-Wno-misleading-indentation', '-I' + str(root / 'sphaira/include'), str(generated), '-o', str(binary)]
            if variant == 'previous':
                command.insert(len(args.compiler), '-DEXPECT_PRE_FIX_FAILURE')
            compiled = subprocess.run(command, env=env, text=True, capture_output=True)
            (output / (variant + '-' + mode + '.compile.log')).write_text(compiled.stdout + compiled.stderr)
            if compiled.returncode:
                print(compiled.stdout + compiled.stderr)
                raise SystemExit(compiled.returncode)
            result = subprocess.run([str(binary)], env=env, text=True, capture_output=True)
            print(result.stdout + result.stderr, end='')
            if result.returncode:
                raise SystemExit(result.returncode)
            results.append({'variant': variant, 'optimization': mode, 'output': result.stdout.strip()})
    pins = [path, root / 'sphaira/include/nexus/installed_move_core.hpp', root / 'sphaira/include/nexus/transfer_report.hpp',
            Path(__file__), root / 'tests/installed_cleanup_fixture.hpp', root / 'tests/installed_cleanup_cases.cpp']
    receipt = {'passed': True, 'source_sha256': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in pins}, 'results': results}
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
