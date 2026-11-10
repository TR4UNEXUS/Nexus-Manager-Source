"""Exercise actual quick/full Yati verification with fault-injected USB I/O.

Reuses the existing aggregate-verification platform fixture; the production
methods and proof helper are compiled unchanged. This is a flow/fault test,
not a substitute for the separate real NCA/NCZ hash tests.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
yati = (root / 'sphaira/source/yati/yati.cpp').read_text()
progress = (root / 'sphaira/source/ui/progress_box.cpp').read_text()

def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

fixture = (root / 'build/reused-progress-validation/fixture.cpp').read_text().split('int main(){')[0]
fixture = fixture.replace('struct Header {u64 size{};};', 'struct Header {u64 size{};u8 padding[0xc00-8]{};};')
fixture = fixture.replace('nexus::verification::Written usb_written;', 'nexus::verification::Written usb_written; bool placeholder_owned{};int placeholder_id{};u8 hash[32]{};')
fixture = fixture.replace('nexus::verification::Cache usb_verified{};', 'nexus::verification::Cache usb_verified{};struct Diagnostic{std::string text;void Set(int,const std::string&,u64){}};Diagnostic* diagnostic{};u64 diagnostic_title{};')
fixture = fixture.replace('namespace nexus {constexpr Result Invalid=299;', 'namespace nexus {namespace registration{constexpr Result ContentAlreadyExists=333;}namespace install{namespace Failure{constexpr int Publication=1;}}constexpr Result Invalid=299;')
fixture = fixture.replace('void Log(const char*,Result,u64){}', 'void Log(const char*,Result,u64=0){}')
fixture = fixture.replace('static bool cancel_now{};', 'static bool cancel_now{},cancel_after_verify{};static unsigned corrupt_header_id{};static Result register_result{};static bool storage_has{true};static s64 storage_size{};')
fixture = fixture.replace('verified.push_back(id.bytes[0]);return 0;', 'verified.push_back(id.bytes[0]);if(cancel_after_verify)cancel_now=true;return 0;')
fixture = fixture.replace('std::memset(data,id.bytes[0],count);return 0;', 'std::memset(data,id.bytes[0],count);if(corrupt_header_id==id.bytes[0]&&!offset&&count)static_cast<u8*>(data)[count-1]^=1;return 0;')
fixture = fixture.replace('template<class... Args>void log_write', '''Result ncmContentStorageRegister(int*,const NcmContentId*,const int*){return register_result;}
Result ncmContentStorageHas(int*,bool* out,const NcmContentId*){*out=storage_has;return 0;}
Result ncmContentStorageGetSizeFromContentId(int*,s64* out,const NcmContentId*){*out=storage_size;return 0;}
template<class... Args>void log_write''')
fixture = fixture.replace(block(fixture, '    Result PublishNca(const NcaCollection& nca)'), '    Result PublishNca(const NcaCollection& nca);')
methods, declarations = [], []
for name in ('NewTransfer', 'UpdateTransfer', 'SetProgressNote', 'BeginNetworkTransfer', 'AddNetworkBytes', 'EndNetworkTransfer'):
    marker = ('void' if name in ('BeginNetworkTransfer', 'AddNetworkBytes', 'EndNetworkTransfer') else 'auto') + ' ProgressBox::' + name + '('
    method = block(progress, marker)
    methods.append(method)
    declarations.append(method[:method.index('{')].replace('ProgressBox::', '') + ';')
fixture = fixture.replace('// PROGRESS_DECLARATIONS', '\n'.join(declarations)).replace('// PROGRESS_METHODS', '\n'.join(methods))
fixture = fixture.replace('// VERIFICATION_STATE', block(yati, 'struct UsbVerificationProgress {') + ';')
fixture = fixture.replace('// VERIFICATION_KEY', block(yati, 'nexus::verification::Key VerificationKey('))
fixture = fixture.replace('// YATI_METHODS', '\n'.join(block(yati, name) for name in ('Result Yati::PublishNca(', 'Result Yati::PublishNcas(', 'Result Yati::VerifyReusedNca(')))
tests = (root / 'tests/usb_quick_verification_tests.cpp').read_text()
# Proof promotion is part of the actual write/flush call chain, not a caller
# option. Assert its placement; full worker/crypto tests run separately.
worker = block(yati, 'Result Yati::InstallNcaInternal(')
assert worker.index('GetWriteOffset()==t_data.GetWriteSize()') < worker.index('source_hash.Matches(') < worker.index('usb_written.source_matched=true')
install = block(yati, 'Result Yati::InstallNca(')
assert install.index('R_TRY(ncmContentStorageFlushPlaceHolder') < install.index('usb_written.flushed=true') < install.index('R_TRY(PublishNca(nca))')
assert 'quick_usb_final_verify=override.quick_usb_final_verify&&physical_target==nexus::Target::Usb' in yati
with tempfile.TemporaryDirectory(prefix='nexus-usb-quick-') as directory:
    work = Path(directory)
    source = work / 'production.cpp'
    source.write_text(fixture + '\n' + tests)
    for optimization in ('-O0', '-O2'):
        binary = work / ('quick' + optimization)
        subprocess.run(['g++', '-std=c++20', optimization, '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        '-Wno-misleading-indentation', '-Wno-deprecated-declarations',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all', '-I', str(root/'sphaira/include'),
                        str(source), '-lcrypto', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
