# evlsound

A personal Windows audio app based on FxSound 1.2.13.0, with **Master Gain from -60 to +20 dB** and an independent profile. It keeps the upstream equalizer, effects, and limiter while allowing more attenuation before the effects stage.

Lower gain can provide additional processing headroom. Restoring listening volume at the physical output does not raise the digital output ceiling. Dynamic Boost is nonlinear and can counteract some attenuation; see [how the audio processing works](docs/AUDIO_SIGNAL_PATH.md).

## Use

1. Keep the official FxSound installation: evlsound uses its existing virtual audio driver.
2. Extract the entire portable release folder. Keep `Factsoft` and the fonts beside `evlsound.exe`.
3. Exit stock FxSound using its tray menu, then open `evlsound.exe`.
4. On first launch, evlsound copies existing settings, presets, and autosaves into `%APPDATA%\evlsound`. It does not overwrite the originals.
5. Open the equalizer's advanced controls and adjust **Master Gain**. Your imported gain is retained initially; the app does not automatically jump to -60 dB or raise Windows volume.
6. To return to stock, exit evlsound and launch FxSound normally.

Both apps share an audio-engine lock and cannot process concurrently. Their command forwarding and profiles are separate. Stock updates and automatic startup are disabled in this portable build. The executable is an unsigned custom build.

## What changed

- Master Gain: -60 to +20 dB, with validated GUI, CLI, saved-settings, and DSP paths.
- Invalid gain input preserves the current valid gain; an invalid startup override preserves valid saved gain.
- Independent app name, executable, profile, and legacy per-user DSP/device registry state.
- A first-run copy of the existing profile, with the settings file committed only after the preset copy succeeds.
- Same audio engine, EQ behavior, effects, driver, and peak-limiter ceiling as the base release.

`--evlsound-check` exits before GUI/profile/audio initialization. Exit code 0 means the stock-compatible engine lock is available; 170 means it is busy. This is a launch-readiness check, not an audio-quality measurement.

## Build and test

Requires Visual Studio 2022 C++ tools, Windows SDK 10, and JUCE 6.1.6. The checked-in Visual Studio project is the supported x64 build path; regenerating it in Projucer would discard local project changes.

```powershell
git clone --depth 1 --branch 6.1.6 https://github.com/juce-framework/JUCE.git ../deps/JUCE
./build-evlsound.ps1
```

Alternatively pass `-JuceModulesDir C:\path\to\JUCE\modules`. The script builds Release x64 and runs offline regression tests against the actual DSP library. Those tests do not start playback or modify audio-device settings. The GUI executable is produced at `fxsound\Project\x64\Release\App\evlsound.exe`.

See [CLI options](docs/COMMAND_LINE_OPTIONS.md), [upstream provenance](UPSTREAM.md), and [the upstream README](docs/UPSTREAM_README.md). The original `fxmcp` source remains upstream-only and is not included in the evlsound binary release.

## License

AGPL-3.0; original copyright and license notices are retained. This is an independent modified build, not an official FxSound release. See the included license file.
