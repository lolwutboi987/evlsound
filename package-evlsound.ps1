param([Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'fxsound\Project\x64\Release\App\evlsound.exe'
if (!(Test-Path -LiteralPath $exe)) { throw 'Build evlsound first.' }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$Destination = (Resolve-Path -LiteralPath $Destination).Path
Copy-Item -LiteralPath $exe -Destination (Join-Path $Destination 'evlsound.exe')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'bin\x64\Factsoft') -Destination $Destination -Recurse -Force
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'Installer\Resources\Fonts') -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $Destination $_.Name)
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'LICENSE') -Destination (Join-Path $Destination 'LICENSE')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'UPSTREAM.md') -Destination (Join-Path $Destination 'UPSTREAM.md')
$readme = @'
# evlsound 0.1.0

Master Gain now supports -60 to +20 dB. This provides more attenuation before
the inherited effects/maximizer. The digital output ceiling is unchanged.

1. Exit installed FxSound using its system-tray menu.
2. Run evlsound.exe from this folder. Keep Factsoft and the fonts beside it.
3. Adjust Master Gain in the advanced EQ controls, then use playback volume
   for listening loudness. The imported gain starts at its previous value.
4. To go back, exit evlsound and launch installed FxSound.

On first launch, settings, presets, and autosaves are copied into
%APPDATA%\evlsound. Original FxSound files are preserved. The existing signed
FxSound driver is reused. No driver or startup replacement is included.
This is an unsigned, independently modified application.

Source and build instructions: https://github.com/lolwutboi987/evlsound
Signal-path explanation: https://github.com/lolwutboi987/evlsound/blob/main/docs/AUDIO_SIGNAL_PATH.md

Offline DSP, parser, headroom, and profile-import checks are documented in the
source repository. A live audio listening test has not been performed.
'@
[System.IO.File]::WriteAllText((Join-Path $Destination 'README.md'), $readme, [System.Text.UTF8Encoding]::new($false))
Get-FileHash -LiteralPath (Join-Path $Destination 'evlsound.exe') -Algorithm SHA256 |
    ForEach-Object { $_.Hash.ToLowerInvariant() + '  evlsound.exe' } |
    Set-Content -LiteralPath (Join-Path $Destination 'SHA256SUMS.txt') -Encoding ascii
Write-Output "Packaged $Destination"
