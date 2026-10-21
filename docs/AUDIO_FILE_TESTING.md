# Test tremolo with a WAV file

This workflow runs the sound engine through the existing GoogleTest/CTest target.
It needs no DAW, audio device, microphone permission, or running standalone app.

From `todo/`, configure with your own mono or stereo WAV:

```bash
cmake --preset default-with-tests -DTREMOLO_TEST_INPUT_WAV="/absolute/path/input.wav"
cmake --build --preset default-with-tests --target TremoloCoursePluginTest
ctest --preset default-with-tests
```

Use an absolute input path. The file stays outside the repository and is never
modified. CMake remembers the path until you change or clear it. A missing file
fails configuration; an unreadable, empty, or unsupported WAV fails the test.

The tests process the file in 512-frame blocks, including a shorter final block,
at its original sample rate and channel count. They render both shapes at 5 Hz:

- `todo/cmake-build-with-tests/test/rendered/sine.wav`
- `todo/cmake-build-with-tests/test/rendered/triangle.wav`
- `todo/cmake-build-with-tests/test/rendered/sine_to_triangle.wav`
- `todo/cmake-build-with-tests/test/rendered/triangle_to_sine.wav`

The transition renders use the same input file and switch shapes exactly halfway
through it, with the engine's 25 ms smoothing enabled. They need at least two
input frames. For the 3-second 440 Hz test tone, the switch occurs at 1.5 seconds.

Outputs are 32-bit float WAVs and are replaced on successful subsequent runs.
They preserve peaks above 1.0 without normalization or integer clipping. Start
playback quietly; the effect's gain can reach 1.4. Choose an input outside this
generated output directory. A silence-only input correctly produces silence.

To render only the file tests and print their output paths:

```bash
ctest --preset default-with-tests -R RendersInputWav -V
```

Play either output in your preferred player. The tests do not start playback.
Listening complements the automated checks; successful export does not prove
that the sound is perceptually correct.

To run without a personal file:

```bash
cmake --preset default-with-tests -DTREMOLO_TEST_INPUT_WAV=
cmake --build --preset default-with-tests --target TremoloCoursePluginTest
ctest --preset default-with-tests
```

The four personal-file cases are explicitly skipped when no WAV is configured.
Generated-LFO tests in `TremoloTest.cpp` are excluded from the test target; the
remaining processor and bypass tests still run. The file tests check finite samples,
gain bounds, preservation of signal, and saved WAV sample rate, channels, and
frame count.

These tests exercise `Tremolo` directly, so they work at checkpoint A. They do
not test host parameter wiring, bypass, or real-time performance.
