# Upstream provenance

evlsound is a modified distribution of [FxSound](https://github.com/fxsound2/fxsound-app).

- Base release: `v1.2.13.0`
- Base commit: `d2a01f7396b0e23bbbd15cfd9fc26617f6a6a3a4`
- Resource commit: `9672dee04ed8b551701c61963d6745d4ab8cd857`
- Windows framework: [JUCE 6.1.6](https://github.com/juce-framework/JUCE/tree/6.1.6), commit `2f980209cc4091a4490bb1bafc5d530f16834e58`

FxSound's original copyright notices and AGPL-3.0 license are retained. evlsound is an independent custom build, not an official FxSound release. Its name, settings isolation, startup safeguards, and expanded master-gain range are local modifications. The installed FxSound virtual driver is reused and is not redistributed or modified here.

The stock `fxmcp` source is retained for provenance, but it is not part of the evlsound release and must not be used to send custom negative gain values to stock FxSound.
