param(
    [string]$JuceModulesDir = (Join-Path $PSScriptRoot '..\deps\JUCE\modules'),
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$JuceModulesDir = (Resolve-Path -LiteralPath $JuceModulesDir).Path
if (!(Test-Path -LiteralPath (Join-Path $JuceModulesDir 'juce_core\juce_core.h'))) {
    throw 'JUCE 6.1.6 modules are required; see README.md.'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio 2022 C++ build tools are required.' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'No Visual Studio C++ toolchain found.' }
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
New-Item -ItemType Directory -Path 'bin\x64' -Force | Out-Null
& $msbuild 'fxsound\Project\FxSound.sln' /m /p:Configuration=Release /p:Platform=x64 "/p:JuceModulesDir=$JuceModulesDir" /p:MultiProcessorCompilation=true /nologo /verbosity:minimal
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }
if (!$SkipTests) {
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    New-Item -ItemType Directory -Path 'tests\bin' -Force | Out-Null
    foreach ($test in @('master_gain_test', 'maximizer_headroom_test')) {
        & cl.exe /nologo /std:c++17 /EHsc /MT /Idsp\include /Idsp\ptutil\include "tests\$test.cpp" "/Fotests\bin\$test.obj" "/Fetests\bin\$test.exe" lib\Release-x64\DfxDsp.lib lib\Release-x64\audiopassthru.lib user32.lib advapi32.lib shell32.lib
        if ($LASTEXITCODE -ne 0) { throw "Offline DSP test $test failed to compile." }
        & ".\tests\bin\$test.exe"
        if ($LASTEXITCODE -ne 0) { throw "Offline DSP regression $test failed." }
    }
    & (Join-Path $PSScriptRoot 'tests\run-profile-import-test.ps1') -JuceModulesDir $JuceModulesDir
}
Write-Output 'Built fxsound\Project\x64\Release\App\evlsound.exe. Audio was not started.'
