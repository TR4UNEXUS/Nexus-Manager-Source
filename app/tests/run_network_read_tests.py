"""Exercise production SMB/NFS reads, Source faults and real stdio file reads."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
def extract(path, name):
    source = (root / path).read_text()
    start = source.index(name)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

fixture = (root / 'tests/network_read_backend_fixture.cpp').read_text()
fixture = fixture.replace('// SMB_READ_PRODUCTION', extract('sphaira/source/utils/devoptab_smb2.cpp', 'ssize_t Device::devoptab_read('))
fixture = fixture.replace('// NFS_READ_PRODUCTION', extract('sphaira/source/utils/devoptab_nfs.cpp', 'ssize_t Device::devoptab_read('))
fixture = fixture.replace('// NFS_MOUNT_PRODUCTION', extract('sphaira/source/utils/devoptab_nfs.cpp', 'bool Device::Mount('))
fixture = fixture.replace('// NFS_DIAGNOSTICS_PRODUCTION', '\n'.join(extract('sphaira/source/utils/devoptab_nfs.cpp', name) for name in ('std::string NfsFailureMessage(', 'bool Device::Fail(', 'Device::~Device(', 'bool Device::CheckRootAccess(', 'Result common::ConnectNfsShare(')))
fixture = fixture.replace('// SEEK_BRIDGE_PRODUCTION', extract('sphaira/source/utils/devoptab_common.cpp', 'off_t devoptab_seek('))
installer = (root / 'sphaira/source/yati/yati.cpp').read_text()
batch_start = installer.index('        if (!is_usb_destination && App::IsFileBaseEmummc())')
batch_end = installer.index('\n    }', batch_start)
batch_selection = installer[batch_start:batch_end]
assert installer.count('ThreadData t_data{') == 1
assert 'ThreadData t_data{this, tickets, std::addressof(nca), destination->IsUsb()};' in installer
batch_test = '''#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <utility>
using u64=std::uint64_t;
constexpr u64 INFLATE_BUFFER_MAX=4*1024*1024;
struct App {static inline bool file_emummc{};static bool IsFileBaseEmummc(){return file_emummc;}};
std::pair<u64,u64> Batch(bool is_usb_destination,bool file_emummc){
    App::file_emummc=file_emummc;u64 read_buffer_size{},max_buffer_size{};
''' + batch_selection + '''
    return {read_buffer_size,max_buffer_size};
}
int main(){for(bool usb:{false,true})for(bool file:{false,true}){
    const auto [read,maximum]=Batch(usb,file);
    assert(read==(!usb&&file?512*1024:4*1024*1024));assert(maximum==4*1024*1024);
}std::cout<<"Production Yati batches: USB always 4 MiB; SD file emuMMC 512 KiB; unchanged 4 MiB buffer ceiling passed\\n";}
'''
with tempfile.TemporaryDirectory(prefix='nexus-network-read-') as directory:
    work = Path(directory)
    extracted = work / 'production_backends.cpp'
    extracted.write_text(fixture)
    batches = work / 'production_batches.cpp'
    batches.write_text(batch_test)
    for optimization in ('-O0', '-O2'):
        for index, source in enumerate((extracted, root / 'tests/network_source_fault_tests.cpp',
                                        root / 'tests/network_source_tests.cpp', batches)):
            binary = work / (str(index) + optimization)
            subprocess.run(['g++', '-std=c++20', optimization, '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                            '-Wno-misleading-indentation', '-Wno-sign-compare', '-I', str(root / 'sphaira/include'),
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
