"""Exercise production Manager-only policy and metadata transaction code."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'sphaira/source/yati/yati.cpp').read_text()
start = source.index('Result InstallManagerUpdateFromLocalNsp(')
installer = source[start:source.index('Result InstallFromFile(', start)]
config_start = installer.index('    ConfigOverride config{};')
config_end = installer.index('    Yati yati(', config_start)

def function(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError(signature)

fixture = (root / 'tests/manager_update_core_tests.cpp').read_text()
fixture = fixture.replace('// MANAGER_CONFIGURATION_PRODUCTION', installer[config_start:config_end])
fixture = fixture.replace('// MANAGER_STORAGE_GUARD_PRODUCTION', function('Result CheckManagerUpdateStorage('))
fixture = fixture.replace('// MANAGER_CONTROL_PATH_PRODUCTION', function('Result ManagerControlPath('))
fixture = fixture.replace('// MANAGER_SNAPSHOT_WRITER_PRODUCTION', function('bool Backup(const manager_update::Bytes& before'))
# Boundary checks supplement executable tests: this exact file handle is hashed
# before any installer setup, and only fully read-back NCAs reach the transaction.
assert installer.index('source::File source(') < installer.index('source.Read(') < installer.index('manager_update::DigestMatches(') < installer.index('yati.Setup(')
assert installer.index('CheckManagerUpdateStorage(storage,detail)') < installer.index('source::File source(')
before_write = function('bool BeforeWrite(){')
assert 'CheckManagerUpdateStorage(yati.storage_id,detail)' in before_write
assert installer.count('source::File ') == 1
assert installer.index('manager_update::ValidLayout(layout)') < installer.index('yati.InstallCnmtNca(')
assert installer.index('manager_update::DisplayVersionMatches(') < installer.index('yati.PublishNcas(') < installer.index('ManagerVerifyPublishedContent(') < installer.index('manager_update::ReplaceMetadata(')
assert 'nacp_size==0x4000' in installer
for prohibited in ('ImportTickets(', 'RemoveInstalledNcas(', 'RegisterNcasAndPushRecord(', 'PushApplicationRecord(', 'avmPushLaunchVersion(', 'ncmContentStorageDelete('):
    assert prohibited not in installer, prohibited

with tempfile.TemporaryDirectory(prefix='nexus-manager-update-') as directory:
    work = Path(directory)
    generated = work / 'test.cpp'
    generated.write_text(fixture)
    for optimization in ('-O0', '-O2'):
        binary = work / ('test' + optimization)
        subprocess.run(['g++', '-std=c++20', optimization, '-g', '-UNDEBUG',
                        '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        '-I', str(root / 'sphaira/include'), str(generated),
                        '-o', str(binary)], check=True)
        run = work / ('run' + optimization)
        run.mkdir()
        subprocess.run([str(binary)], check=True, timeout=10, cwd=run)
