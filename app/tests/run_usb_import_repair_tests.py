#!/usr/bin/env python3
"""Exercise actual scan/classification/family selection with injected read-only IO."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'sphaira/source/nexus/usb_import_scan.cpp').read_text()
def function(marker):
    start = source.index(marker)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        if source[end] == '{': depth += 1
        elif source[end] == '}': depth -= 1
        end += 1
    return source[start:end]
fixture = (root / 'tests/usb_import_repair_tests.cpp').read_text()
header = (root / 'sphaira/include/nexus/usb_import_scan.hpp').read_text()
header = '\n'.join(line for line in header.splitlines() if not line.startswith('#'))
fixture = fixture.replace('// PUBLIC_SCAN_TYPES', header)
markers = {
    'CANCEL':'Result Cancelled(', 'CHECK':'Result Check(',
    'SAME_KEY':'bool SameKey(', 'HEADER_TYPE':'bool HeaderType(',
    'SAME_REFERENCES':'bool SameReferences(', 'NATIVE':'Result NativeRecord(',
    'BADGE':'Result BadgeHealthy(', 'CLASSIFY':'Result Classify(',
    'INSTALLED_IDENTITY':'bool InstalledIdentity(', 'INSTALLED_RECORD':'bool InstalledRecord(',
    'RECONCILE':'Result ReconcileFamilyMembers(', 'BASE':'Result CheckBaseGames(',
    'PLAN':'UsbLibraryPlan PlanUsbLibrary(', 'SUMMARY':'UsbLibrarySummary SummarizeUsbLibrary(',
    'CANONICAL':'std::string CanonicalUsbContentPath(', 'PREVIEW_IDENTITY':'void PreviewIdentity(',
    'MAKE_RECORD':'bool MakeRecord(', 'SCAN':'Result ScanUsbImports(',
}
for placeholder, marker in markers.items():
    fixture = fixture.replace('// PRODUCTION_' + placeholder, function(marker))
with tempfile.TemporaryDirectory(prefix='nexus-usb-repair-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(fixture)
    for opt in ('-O0', '-O2'):
        binary = path / ('test' + opt)
        subprocess.run(['g++', '-std=c++20', opt, '-Wall', '-Wextra', '-Werror',
                        '-Wno-misleading-indentation', '-fsanitize=undefined',
                        '-fno-sanitize-recover=all', '-I', str(root / 'sphaira/include'),
                        str(path / 'test.cpp'), '-lcrypto', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

    # Temporary old-predicate control: run the exact public names=false startup
    # scan with a valid installed Manager/forwarder beside a retail USB game.
    # No production source or device state is changed by this control.
    scoped_filter = 'if(key.type==NcmContentMetaType_Application&&!core::UserId(key.id))continue;'
    assert fixture.count(scoped_filter) == 1
    legacy = fixture.replace(scoped_filter, '')
    legacy = legacy.replace('int main(){return sphaira::nexusapp::Main();}', r'''int main(){
        using namespace sphaira::nexusapp;
        for(const u64 id:{0x05004e584d470000ULL,0x05005553424c0000ULL}){
            Reset();auto row=Row();auto unrelated=InstalledOnly(row,id,id,NcmContentMetaType_Application);
            Registered(unrelated);RetailUsb(row);const auto before=StateSnapshot();
            UsbImportScan scan;std::atomic_bool cancel{false};
            REQUIRE(ScanUsbImports(nullptr,scan,7,&cancel,false)==0x1759&&!scan.complete);
            REQUIRE(scan.detail.find("could not be associated safely")!=std::string::npos);
            REQUIRE(StateSnapshot()==before&&world.cache_reads==0&&world.name_reads==0);
        }
        std::cout<<"0.2.6 full background-scan negative control: Manager NSP and forwarder each reproduce 0x1759; storage/tickets/routes unchanged\\n";
    }''')
    (path / 'legacy.cpp').write_text(legacy)
    subprocess.run(['g++', '-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror',
                    '-Wno-misleading-indentation', '-fsanitize=undefined',
                    '-fno-sanitize-recover=all', '-I', str(root / 'sphaira/include'),
                    str(path / 'legacy.cpp'), '-lcrypto', '-o', str(path / 'legacy')], check=True)
    subprocess.run([str(path / 'legacy')], check=True)
