# Validation: evlsound 0.1.0

Local validation completed on Windows on 2026-10-21 with Visual Studio 2022,
MSVC 14.44.35207, Windows SDK 10.0.26100.0, and JUCE 6.1.6.

## Passed

- Release x64 application build. The upstream code still emits compiler warnings;
  the build completed with zero errors.
- Strict gain parsing, finite/range checks, invalid-input preservation, and saved
  settings versus startup-override precedence.
- Actual DSP master-gain sample processing in mono, stereo, 5.1, and 7.1 layouts.
- Actual EQ/master and maximizer stress test: the same synthetic signal limited
  at -20 dB and did not limit at -40/-60 dB, with the limiter ceiling unchanged.
- First-run profile copy, saved presets and autosaves, nested presets, existing
  profile preservation, missing source, copy/read/enumeration failure, retry,
  and destination-junction rejection. Fixtures are confined to tests/bin.
- Early `--evlsound-check` returned expected busy status 170 while installed
  FxSound was running. Its process and settings were unchanged; no custom
  profile was created by that diagnostic.
- Independent source review of gain paths, profile isolation, native mutex/IPC
  handling, and update/startup isolation; identified defects were corrected.

## Scope and limits

No live audio playback, headphone measurement, listening comparison, or
real-time limiter-gain capture was performed. The synthetic test is not evidence
that any particular user's track was clipping. GUI layout and real device
startup have not been visually exercised in the custom build.

Profile tests exercise source-read and directory-enumeration failures; failure
of the final temporary-file rename was not injected. Concurrent startup was
source-reviewed, not stress-tested against the running audio application.

Run `build-evlsound.ps1` to rebuild and repeat the offline tests. The native audio
driver remains the separately installed official FxSound driver.
