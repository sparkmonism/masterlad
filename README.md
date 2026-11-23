# Masterlad

Songcraft Audio. A mastering suite in one plugin (AU / VST3 / Standalone), built with JUCE.

    Input gain > EQ > Dynamics > Exciter > Imager > Maximizer > meters

Each module has its own power dot (top, next to its tab). The chain order is fixed.

## Build (macOS)

Requires Xcode command line tools and CMake (`brew install cmake`). From inside this folder,
one command at a time:

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j 8
    killall -9 AudioComponentRegistrar
    auval -v aufx Mstl Sngc

The AU is copied to `~/Library/Audio/Plug-Ins/Components/` automatically.

## Modules

**EQ** - six bands: low shelf, four bells, high shelf. Drag points on the graph (freq/gain),
scroll over a point for Q, double-click a point to switch the band. The live spectrum of the
output sits behind the curve. PROCESS: Stereo, Mid only or Side only. Minimum-phase.

**Dynamics** - four-band compressor (Linkwitz-Riley 24 dB/oct crossovers, three adjustable).
Per band: Threshold, Ratio, Attack, Release, Gain, Bypass. 6 dB soft knee. Stereo linked.
Gain reduction meter on each band. Off by default.

**Exciter** - high-passes at Frequency, saturates (Soft / Tube / Tape / Hard), and blends the
result in parallel. Runs oversampled using the Oversampling selector. Off by default.

**Imager** - four-band stereo width (0% = mono, 100% = unchanged, 200% = double side).

**Maximizer** - look-ahead limiter with 4x inter-sample peak detection. Gain pushes into the
limiter, Ceiling is the output limit, Release with an Auto option. The ceiling holds on true peak
(it leaves about 0.15 dB of safety margin).

**Meters** (right) - input gain, input/output peak meters, Momentary / Short-term / Integrated
loudness (LUFS, ITU-R BS.1770 / EBU R128 gating), true peak (dBTP, held until RESET), and
stereo correlation. RESET clears integrated loudness and the true peak hold.

## Notes

- Latency is constant: the maximizer look-ahead (about 2.3 ms) plus the exciter's 8x oversampler
  delay. The plugin reports it to the host, and changing the Oversampling setting never changes it.
- Bypass is a short crossfade against the latency-aligned input.
- The input fader is the only level control before the chain. The Maximizer ceiling sets the final
  output level.
- Not included (compared with Ozone): linear-phase EQ, dynamic EQ, vintage modules, reference
  tracks, assistant/auto mastering, dithering, reordering the chain.
