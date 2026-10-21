param(
    [string]$JuceModulesDir = (Join-Path $PSScriptRoot '..\..\deps\JUCE\modules')
)
$ErrorActionPreference = 'Stop'
$JuceModulesDir = (Resolve-Path -LiteralPath $JuceModulesDir).Path
$core = Join-Path $JuceModulesDir 'juce_core\juce_core.cpp'
if (!(Test-Path -LiteralPath $core)) { throw 'JUCE 6.1.6 sources are required.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio 2022 C++ build tools are required.' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'No Visual Studio C++ toolchain found.' }
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$bin = Join-Path $PSScriptRoot 'bin'
if ((Test-Path -LiteralPath $bin) -and ((Get-Item -LiteralPath $bin -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'tests/bin must not be a reparse point.'
}
New-Item -ItemType Directory -Path $bin -Force | Out-Null
$test = Join-Path $PSScriptRoot 'profile_import_test.cpp'
$exe = Join-Path $bin 'profile_import_test.exe'
# Compile JUCE core in isolation: this links no app, settings object, DSP, or audio code.
& cl.exe /nologo /std:c++17 /EHsc /MT /O2 /FC /DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 /DJUCE_STANDALONE_APPLICATION=1 /DJUCE_USE_CURL=0 /DNDEBUG "/I$JuceModulesDir" $test $core "/Fo$bin\" "/Fe$exe" shell32.lib ole32.lib
if ($LASTEXITCODE -ne 0) { throw 'Offline profile-import test failed to compile.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Offline profile-import regression failed.' }
