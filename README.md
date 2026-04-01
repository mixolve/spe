# spe

[latest version (currently macOS only)](https://github.com/mixolve/spe/releases/latest/download/spe-macos.zip)

spe is a spectral dynamics processor with separate `DUAL-MONO` and `STEREO` stages, global DSP controls, and a post-processing spectrum analyser.

Current functions:

- `DUAL-MONO`
- independent `LL-THRESHOLD` and `RR-THRESHOLD`
- independent `LL-ADAPTIVE` and `RR-ADAPTIVE`
- independent `LL-OFFSET` and `RR-OFFSET`
- stage `BYPASS`

- `STEREO`
- linked stereo `THRESHOLD`
- linked stereo `ADAPTIVE`
- linked stereo `OFFSET`
- stage `BYPASS`

- `GLOBAL`
- `IN-GAIN`
- `ATTACK`
- `RELEASE`
- `KNEE`
- `RATIO`
- DSP `WINDOW-SIZE`
- DSP `SLOPE`
- `OUT-GAIN`
- `DELTA`

- `ANALYSER`
- analyser `FFT-SIZE`
- analyser `OVERLAP`
- horizontal range: `LEFT`, `RIGHT`
- vertical range: `LOW`, `HIGH`
- display `SLOPE`
- time averaging `TIME`
- `HIDE`

- processing / behaviour
- spectral compression per FFT bin
- separate `DUAL-MONO` and `STEREO` processing stages
- adaptive threshold mode for `DUAL-MONO` and `STEREO`
- post-processed spectrum display
- gain reduction overlay
- latency-reported DSP windowing
- delta monitoring
