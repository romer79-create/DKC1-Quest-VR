# Validation and current limits

The private 0.8.2 milestone was tested on Quest 3 / Adreno 740. This source export retains its renderer and bridge logic, with configurable external build paths and local debug signing.

- Three matching GPU repeats: 46 real native-scene cases and four synthetic texture oracles each, using libraries extracted from the private APK.
- The 24 filter-off cases matched the earlier 0.6.1 renderer. Twelve DK/Diddy mesh pairs remained byte identical, closed and finite; projected anchor error was below 0.000001 m.
- Oracles checked solid colours, known checker dither mean, contrast away from an edge and exact isolated-alpha fallback. Modes, unsupported captures and renderer recreation were exercised.
- Two-eye 1680 × 1760 workload over twelve scenes averaged 22.420 ms with glFinish. Earlier filter candidates gave 41.435 and 30.045 ms; historical EPX4 gave 17.065 ms. These timings are not sustained app/compositor FPS.
- Human feedback preferred the new filter and reported fluid gameplay in the first level. It did not independently validate the entire campaign, sustained FPS or return/reopen lifecycle of 0.8.2.

Original passive fixtures and technical captures are deliberately not published because they contain game graphics. `tests/reconstruction_gpu_test.c` is source-only and requires privately generated fixtures plus the locally built renderer/core in its private working directory. It must not run in public CI with attached game data.

## Source export checks

The exported code passed the source-only audit. Its portable host test passed on Windows x64, including one million concurrent stereo frames, ring wrap/overflow/underflow, colour conversion, controller mappings and game pacing.

The public preparation script produced the same passive PPU capture taps as the accepted private build, ignoring comments/whitespace, and an identical private function header. Its ARM64 core build passed using the original 2,230 verified PIC objects. The Android frontend then passed `assembleDebug` and `lintDebug` offline with strict dependency verification and an isolated local debug key. That private test APK is not distributed and was not installed on the headset.

Public CI runs the portable host test on Linux and audits tracked files plus Git history. It does not build generated game code or use game graphics. A full clean generation/compile from a new machine has not been executed for this export. Contributions should identify which path was tested.
