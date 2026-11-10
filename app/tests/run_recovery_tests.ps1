param([string]$Compiler='C:/devkitPro/msys2/usr/bin/g++.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$qa=Join-Path $root 'build/recovery-tests'
$null=New-Item -ItemType Directory -Force -Path $qa
$env:PATH=(Split-Path $Compiler -Parent)+';'+$env:PATH
# Generated harness compiles the actual production adapter and plan declarations.
# Only the platform include directives are replaced by memory-backed SDK mocks.
$header=Get-Content -Raw -LiteralPath "$root/sphaira/include/nexus/index_recovery.hpp"
$source=Get-Content -Raw -LiteralPath "$root/sphaira/source/nexus/index_recovery.cpp"
$header=$header -replace '(?m)^#(?:include|pragma)[^\r\n]*\r?\n',''
$source=$source -replace '(?m)^#include "[^"\r\n]*"\r?\n',''
$fixture=Get-Content -Raw -LiteralPath "$PSScriptRoot/recovery_fixture.hpp"
$hash=Get-Content -Raw -LiteralPath "$root/sphaira/include/yati/nx/content_hash.hpp"
$hash=$hash -replace '(?m)^#(?:include "|pragma)[^\r\n]*\r?\n',''
$menu=Get-Content -Raw -LiteralPath "$root/sphaira/source/ui/menus/nexus_menu.cpp"
$start=$menu.IndexOf('static void ShowRecoveryResult(')
$end=$menu.IndexOf('void Menu::ResumeInstalls(){',$start)
if($start -lt 0 -or $end -le $start){throw 'Report UI helper not found'}
$ui=$menu.Substring($start,$end-$start)
$presentation=Get-Content -Raw -LiteralPath "$root/sphaira/include/nexus/recovery_ui.hpp"
$presentation=$presentation -replace '(?m)^#(?:include|pragma)[^\r\n]*\r?\n',''
$flowStart=$menu.IndexOf('void Menu::RecoverUsbIndexes(u64')
$flowEnd=$menu.IndexOf('void Menu::CleanupTargets(){',$flowStart)
if($flowStart -lt 0 -or $flowEnd -le $flowStart){throw 'Recovery workflow not found'}
$flow=$menu.Substring($flowStart,$flowEnd-$flowStart)
foreach($suite in @('regression','mismatch','r1_ui')) {
    $cases=Get-Content -Raw -LiteralPath "$PSScriptRoot/recovery_$suite.cpp"
    $cases=$cases.Replace('// RECOVERY_UI_PRODUCTION',$ui)
    $cases=$cases.Replace('// RECOVERY_WORKFLOW_PRODUCTION',$flow)
    $generated=Join-Path $qa "$suite.cpp"
    [IO.File]::WriteAllText($generated,$fixture+"`n"+$hash+"`n"+$header+"`n"+$source+"`n"+$presentation+"`n"+$cases,[Text.UTF8Encoding]::new($false))
    foreach($mode in @('O0','O2')) {
        $exe=Join-Path $qa "$suite-$mode.exe"
        & $Compiler -std=c++20 -Wall -Wextra -Werror "-$mode" -I "$root/sphaira/include" $generated -o $exe
        if($LASTEXITCODE){throw "Compile failed: $suite / $mode"}
        & $exe $qa
        if($LASTEXITCODE){throw "Tests failed: $suite / $mode"}
    }
}
