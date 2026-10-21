# Build the tremolo backend one small edit at a time

You can follow this guide entirely in `todo/`; you do not need to open the completed project. Each numbered step explains **why**, identifies **where**, and gives only the lines to add or change. Do not replace whole files.

Paths below are relative to `todo/`. Code anchors are used instead of line numbers because line numbers move as you work. Keep the surrounding braces unless a step explicitly changes them. A class member belongs inside the class; a method definition in a `.cpp` belongs inside its existing `namespace tremolo` block.

Your current `Tremolo.h` already contains `ApplySmoothing` and `LfoWaveform`. Steps 1–2 let you verify those additions; do not add duplicates. All later steps start from your current TODO files.

## What the sound engine does

For every frame, generate one LFO sample, calculate `gain = 1 + 0.4 × LFO`, and apply that gain to every audio channel. With an LFO between −1 and +1, gain ranges from **0.6 to 1.4**. This effect raises and lowers volume around unity; it does not fade to silence. Leave headroom for the possible gain increase.

The LFO defaults to 5 Hz, supports sine and triangle shapes, and has a fixed depth of `0.4f`. JUCE provides the oscillators and smoothing; you will connect those existing library components.

## Part A — Generate and apply the LFO

### Step 1: Verify the smoothing flag

**File:** `tremolo_plugin/include/Tremolo/Tremolo.h`

**Why:** The setters need to distinguish normal changes from changes that should happen immediately.

**Where:** Immediately inside `namespace tremolo`, before `class Tremolo`, verify this line exists. You have already added it; skip this insertion if present.

```cpp
enum class ApplySmoothing { no, yes };
```

**Check:** The enum is outside the class and inside the namespace.

### Step 2: Verify the waveform choices

**File:** `tremolo_plugin/include/Tremolo/Tremolo.h`

**Why:** The selected waveform will index the oscillator array.

**Where:** Inside `class Tremolo`, under `public:`, verify this enum exists. You have already added it; skip this insertion if present.

```cpp
  enum class LfoWaveform : size_t {
    sine = 0,
    triangle = 1,
  };
```

**Check:** There is exactly one waveform enum, with sine at index 0 and triangle at index 1.

### Step 3: Define depth and the waveform functions

**File:** `tremolo_plugin/include/Tremolo/Tremolo.h`

**Why:** Depth controls the volume swing. Both waveform functions convert oscillator phase into a value between −1 and +1. Their phase offsets align the initial zero crossings.

**Where:** Find these existing lines:

```cpp
  // You should put class members and private functions here
```

Replace only those lines with:

```cpp
  static constexpr auto modulationDepth = 0.4f;

  static float sine(float phase) {
    // start phase is -pi -> change it to 0 to match the mathematical sine
    return std::sin(phase + juce::MathConstants<float>::pi);
  }

  static float triangle(float phase) {
    // offset the phase by pi/2 to return 0 if phase equals 0
    // and match the sine waveform
    // (otherwise, the waveform starts at 1)
    const auto offsetPhase = phase - juce::MathConstants<float>::halfPi;

    // Source:
    // https://thewolfsound.com/sine-saw-square-triangle-pulse-basic-waveforms-in-synthesis/#triangle
    const auto ft = offsetPhase / juce::MathConstants<float>::twoPi;
    return 4.f * std::abs(ft - std::floor(ft + 0.5f)) - 1.f;
  }
```

### Step 4: Store the oscillators and transition state

**File:** `tremolo_plugin/include/Tremolo/Tremolo.h`

**Why:** Each oscillator owns its phase. The two shape fields track the selected and requested waveforms. The smoother stores a 25 ms transition ramp.

**Where:** Find this anchor:

```cpp
  static float triangle(float phase) {
    // offset the phase by pi/2 to return 0 if phase equals 0
    // and match the sine waveform
    // (otherwise, the waveform starts at 1)
    const auto offsetPhase = phase - juce::MathConstants<float>::halfPi;

    // Source:
    // https://thewolfsound.com/sine-saw-square-triangle-pulse-basic-waveforms-in-synthesis/#triangle
    const auto ft = offsetPhase / juce::MathConstants<float>::twoPi;
    return 4.f * std::abs(ft - std::floor(ft + 0.5f)) - 1.f;
  }
```

Copy these lines immediately **after** it:

```cpp
  std::array<juce::dsp::Oscillator<float>, 2u> lfos{
      juce::dsp::Oscillator<float>{sine},
      juce::dsp::Oscillator<float>{triangle}};

  LfoWaveform currentLfo = LfoWaveform::sine;
  LfoWaveform lfoToSet = currentLfo;

  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
      lfoTransitionSmoother{0.f};
```

**Check:** These are private members. Both oscillators use named functions. The sine function uses `phase + pi` because JUCE starts the oscillator phase at −pi.

### Step 5: Set the initial modulation rate

**File:** `tremolo_plugin/include/Tremolo/Tremolo.h`

**Why:** Set both oscillators to 5 Hz immediately when the engine is constructed.

**Where:** Find this anchor:

```cpp
  enum class LfoWaveform : size_t {
    sine = 0,
    triangle = 1,
  };
```

Copy these lines immediately **after** it:

```cpp
  Tremolo() { setModulationRateHz(5.f, ApplySmoothing::no); }
```

### Step 6: Prepare JUCE for the host sample rate

**File:** `tremolo_plugin/include/Tremolo/Tremolo.h`

**Why:** An oscillator needs the actual sample rate to turn frequency into phase increments. Its channel count is one because it produces one shared control signal.

**Where:** Find these existing lines:

```cpp
    juce::ignoreUnused(sampleRate, expectedMaxFramesPerBlock);
```

Replace only those lines with:

```cpp
    const juce::dsp::ProcessSpec processSpec{
        .sampleRate = sampleRate,
        .maximumBlockSize =
            static_cast<juce::uint32>(expectedMaxFramesPerBlock),
        .numChannels = 1u,
    };
    for (auto& lfo : lfos) {
      lfo.prepare(processSpec);
    }
    lfoTransitionSmoother.reset(sampleRate, 0.025 /* 25 milliseconds */);

```

**Check:** The `prepare()` signature and closing brace remain. The smoother duration is 0.025 seconds.

### Step 7: Add the rate setter

**File:** `tremolo_plugin/include/Tremolo/Tremolo.h`

**Why:** Hosts will update rate during playback. JUCE smooths frequency unless the force flag requests an immediate update.

**Where:** Find this anchor:

```cpp
  void prepare(double sampleRate, int expectedMaxFramesPerBlock) {
```

Copy these lines immediately **before** it:

```cpp
  void setModulationRateHz(
      float rateHz,
      ApplySmoothing applySmoothing = ApplySmoothing::yes) noexcept {
    const auto force = applySmoothing == ApplySmoothing::no;
    for (auto& lfo : lfos) {
      lfo.setFrequency(rateHz, force);
    }
  }
```

### Step 8: Add the waveform setter

**File:** `tremolo_plugin/include/Tremolo/Tremolo.h`

**Why:** Store the requested shape here. Starting its transition separately prevents repeated parameter reads from restarting it.

**Where:** Find this anchor:

```cpp
  void prepare(double sampleRate, int expectedMaxFramesPerBlock) {
```

Copy these lines immediately **before** it:

```cpp
  void setLfoWaveform(LfoWaveform waveform,
                      ApplySmoothing applySmoothing = ApplySmoothing::yes) {
    jassert(waveform == LfoWaveform::sine || waveform == LfoWaveform::triangle);

    lfoToSet = waveform;

    if (applySmoothing == ApplySmoothing::no) {
      currentLfo = waveform;
    }
  }
```

### Step 9: Generate values and start shape transitions

**File:** `tremolo_plugin/include/Tremolo/Tremolo.h`

**Why:** A shape change ramps from a sampled value of the old shape to one of the new shape. Otherwise, the selected JUCE oscillator generates the next sample. Passing zero to `processSample()` requests the generated signal without adding input audio.

**Where:** Find this anchor:

```cpp
  std::array<juce::dsp::Oscillator<float>, 2u> lfos{
```

Copy these lines immediately **before** it:

```cpp
  void updateLfoWaveform() {
    if (lfoToSet != currentLfo) {
      // update the smoother
      lfoTransitionSmoother.setCurrentAndTargetValue(getNextLfoValue());

      currentLfo = lfoToSet;

      // initiate smoothing
      lfoTransitionSmoother.setTargetValue(getNextLfoValue());
    }
  }

  float getNextLfoValue() {
    if (lfoTransitionSmoother.isSmoothing()) {
      return lfoTransitionSmoother.getNextValue();
    }
    // the argument is added to the generated sample, thus, we pass in 0
    // to get just the generated sample
    return lfos[juce::toUnderlyingType(currentLfo)].processSample(0.f);
  }
```

**Check:** Both methods are under `private:`. This is a ramp between sampled values, not a crossfade of two continuously advancing oscillators.

### Step 10: Check the requested shape at each block

**File:** `tremolo_plugin/include/Tremolo/Tremolo.h`

**Why:** Starting a transition once per block is enough; individual smoothing values advance per frame.

**Where:** Inside `process()`, copy this call immediately before the existing `// for each frame` comment:

```cpp
    updateLfoWaveform();
```

### Step 11: Generate one LFO sample per frame

**File:** `tremolo_plugin/include/Tremolo/Tremolo.h`

**Why:** All channels must share the same LFO value. Generating it inside the channel loop would advance phase too fast and give left/right different modulation.

**Where:** Find these existing lines:

```cpp
      // TODO: generate the LFO value
```

Replace only those lines with:

```cpp
      const auto lfoValue = getNextLfoValue();
```

### Step 12: Turn the LFO into gain

**File:** `tremolo_plugin/include/Tremolo/Tremolo.h`

**Why:** Offset the bipolar oscillator by one after scaling it to depth 0.4.

**Where:** Find these existing lines:

```cpp
      // TODO: calculate the modulation value
```

Replace only those lines with:

```cpp
      const auto modulationValue = modulationDepth * lfoValue + 1.f;
```

### Step 13: Multiply the input sample

**File:** `tremolo_plugin/include/Tremolo/Tremolo.h`

**Why:** This is the actual tremolo operation.

**Where:** Find these existing lines:

```cpp
        // TODO: modulate the sample
        const auto outputSample = inputSample;
```

Replace only those lines with:

```cpp
        const auto outputSample = modulationValue * inputSample;
```

**Check:** Keep the existing `buffer.setSample(...)` call immediately below this code.

### Step 14: Reset oscillator phase when resources are released

**File:** `tremolo_plugin/include/Tremolo/Tremolo.h`

**Why:** The processor already calls this method from `releaseResources()`. Phase must persist during consecutive audio blocks.

**Where:** Find these existing lines:

```cpp
  void reset() noexcept {}
```

Replace only those lines with:

```cpp
  void reset() noexcept {
    for (auto& lfo : lfos) {
      lfo.reset();
    }
  }
```

**Check:** Do not call `reset()` from `process()` or `processBlock()`.


**Checkpoint A:** The existing processor already calls the engine's `prepare()`, `process()`, and `reset()`. You now have a fixed-rate 5 Hz tremolo. Build the basic plugin with `cd todo`, `cmake --preset default`, then `cmake --build --preset default`. Steps 5–9 temporarily reference methods that are added later, so build after completing this part, rather than after every insertion.

To listen using your own WAV without a DAW or microphone, follow the [audio-file test workflow](AUDIO_FILE_TESTING.md). It runs through the existing test target and saves sine/triangle renders for playback.

## Part B — Let the host control the sound

### Step 15: Store references to the three parameters

**File:** `tremolo_plugin/include/Tremolo/Parameters.h`

**Why:** The processor owns each parameter object. These references let the backend read their current values.

**Where:** Find this anchor:

```cpp
  explicit Parameters(juce::AudioProcessor&);
```

Copy these lines immediately **after** it:

```cpp
  juce::AudioParameterFloat& rate;
  juce::AudioParameterBool& bypassed;
  juce::AudioParameterChoice& waveform;

  JUCE_DECLARE_NON_COPYABLE(Parameters)
  JUCE_DECLARE_NON_MOVEABLE(Parameters)
```

**Check:** All added declarations are inside `struct Parameters`, before its `};`.

### Step 16: Transfer parameter ownership to JUCE

**File:** `tremolo_plugin/source/Parameters.cpp`

**Why:** The helper registers a parameter with the processor, transfers ownership from its `unique_ptr`, and returns a reference to that same object. The anonymous namespace keeps this helper local to this implementation.

**Where:** Immediately after the existing `namespace tremolo {`, copy these lines:

```cpp
namespace {
auto& addParameterToProcessor(juce::AudioProcessor& processor, auto parameter) {
  auto& result = *parameter;
  processor.addParameter(parameter.release());
  return result;
}
}  // namespace
```

### Step 17: Define the rate parameter

**File:** `tremolo_plugin/source/Parameters.cpp`

**Why:** Create a host control measured in Hz. The range has a 0.01 Hz interval and a 0.4 skew for finer control at lower rates.

**Where:** Inside the anonymous namespace from the previous step, add this function **before** its closing `}  // namespace`, keeping it above the `Parameters` constructor:

```cpp
juce::AudioParameterFloat& createModulationRateParameter(
    juce::AudioProcessor& processor) {
  constexpr auto versionHint = 1;
  return addParameterToProcessor(
      processor,
      std::make_unique<juce::AudioParameterFloat>(
          juce::ParameterID{"modulation.rate", versionHint}, "Modulation rate",
          juce::NormalisableRange<float>{0.1f, 20.f, 0.01f, 0.4f}, 5.f,
          juce::AudioParameterFloatAttributes{}.withLabel("Hz")));
}
```

### Step 18: Define the bypass parameter

**File:** `tremolo_plugin/source/Parameters.cpp`

**Why:** Expose a boolean bypass control to the host, initially off.

**Where:** Inside the anonymous namespace from the previous step, add this function **before** its closing `}  // namespace`, keeping it above the `Parameters` constructor:

```cpp
juce::AudioParameterBool& createBypassedParameter(
    juce::AudioProcessor& processor) {
  constexpr auto versionHint = 1;
  return addParameterToProcessor(
      processor,
      std::make_unique<juce::AudioParameterBool>(
          juce::ParameterID{"bypassed", versionHint}, "Bypass", false));
}
```

### Step 19: Define the waveform parameter

**File:** `tremolo_plugin/source/Parameters.cpp`

**Why:** Expose two named choices. Their indices must match the waveform enum: Sine is 0 and Triangle is 1.

**Where:** Inside the anonymous namespace from the previous step, add this function **before** its closing `}  // namespace`, keeping it above the `Parameters` constructor:

```cpp
juce::AudioParameterChoice& createWaveformParameter(
    juce::AudioProcessor& processor) {
  constexpr auto versionHint = 1;
  return addParameterToProcessor(
      processor,
      std::make_unique<juce::AudioParameterChoice>(
          juce::ParameterID{"modulation.waveform", versionHint},
          "Modulation waveform", juce::StringArray{"Sine", "Triangle"}, 0));
}
```

### Step 20: Initialize the references in the constructor

**File:** `tremolo_plugin/source/Parameters.cpp`

**Why:** Reference members must be initialized in the initializer list, in declaration order.

**Where:** Find these existing lines:

```cpp
// TODO: create parameters
// TODO: retrieve references to parameters
// TODO: add parameters to the processor
```

Replace only those lines with:

```cpp
    : rate{createModulationRateParameter(processor)},
      bypassed{createBypassedParameter(processor)},
      waveform{createWaveformParameter(processor)}
```

### Step 21: Remove the now-obsolete unused-argument marker

**File:** `tremolo_plugin/source/Parameters.cpp`

**Why:** The constructor now uses its processor argument through the initializer list.

**Where:** Inside the existing `Parameters` constructor body, delete the line below. Leave the body braces in place:

```cpp
  juce::ignoreUnused(processor);
```

**Check:** The constructor body is empty, following the initializer list.


The registered controls are: rate `modulation.rate` (0.1–20 Hz, default 5 Hz), bypass `bypassed` (default false), and waveform `modulation.waveform` (Sine/Triangle, default index 0). Keep these IDs and the waveform order stable for automation and saved projects. The rate range uses an interval of 0.01 Hz and skew of 0.4.

## Part C — Finish the existing bypass smoother

This helper already contains the dry buffer and gain ramps. You only need the following small additions. Its default fade lasts 10 ms and can span several blocks, despite the existing header comment referring to a single block.

### Step 22: Initialize bypass state in the constructor

**File:** `tremolo_plugin/include/Tremolo/BypassTransitionSmoother.h`

**Why:** Start with dry gain zero and wet gain one, using the existing reset method.

**Where:** Find this anchor:

```cpp
    jassert(0.0 < crossfadeLengthSeconds);
```

Copy these lines immediately **after** it:

```cpp
    reset();
```

### Step 23: Reset after preparing the dry buffer

**File:** `tremolo_plugin/include/Tremolo/BypassTransitionSmoother.h`

**Why:** Starting playback should initialize the newly prepared buffer and gain state.

**Where:** In `prepare()`, immediately after `wetGain.reset(spec.sampleRate, crossfadeLengthSeconds);`, add:

```cpp
    reset();
```

### Step 24: Add private bypass-state queries

**File:** `tremolo_plugin/include/Tremolo/BypassTransitionSmoother.h`

**Why:** The target tells us which bypass state was requested. The second query skips copying and mixing when the effect is fully active.

**Where:** Find this anchor:

```cpp
  double crossfadeLengthSeconds = 0.0;
```

Copy these lines immediately **before** it:

```cpp
  [[nodiscard]] bool isBypassed() const noexcept {
    return juce::exactlyEqual(dryGain.getTargetValue(), 1.0f);
  }

  [[nodiscard]] bool shouldAvoidProcessing() const noexcept {
    return !isTransitioning() && !isBypassed();
  }
```

### Step 25: Avoid restarting an unchanged bypass target

**File:** `tremolo_plugin/include/Tremolo/BypassTransitionSmoother.h`

**Why:** The audio callback sets bypass every block. Repeatedly resetting an unfinished ramp would prevent it from completing.

**Where:** Find this anchor:

```cpp
  void setBypass(bool bypass) noexcept {
```

Copy these lines immediately **after** it:

```cpp
    if (bypass == isBypassed()) {
      return;
    }
```

### Step 26: Skip unnecessary work in setDryBuffer()

**File:** `tremolo_plugin/include/Tremolo/BypassTransitionSmoother.h`

**Why:** When fully active, the buffer already contains the desired wet signal and no dry/wet mixing is needed.

**Where:** Find this anchor:

```cpp
  void setDryBuffer(const juce::AudioBuffer<float>& buffer) noexcept {
```

Copy these lines immediately **after** it:

```cpp
    if (shouldAvoidProcessing()) {
      return;
    }
```

**Check:** Put the guard before the assertions. Keep the existing copy/gain/mix code below it.

### Step 27: Skip unnecessary work in mixToWetBuffer()

**File:** `tremolo_plugin/include/Tremolo/BypassTransitionSmoother.h`

**Why:** When fully active, the buffer already contains the desired wet signal and no dry/wet mixing is needed.

**Where:** Find this anchor:

```cpp
  void mixToWetBuffer(juce::AudioBuffer<float>& buffer) noexcept {
```

Copy these lines immediately **after** it:

```cpp
    if (shouldAvoidProcessing()) {
      return;
    }
```

**Check:** Put the guard before the assertions. Keep the existing copy/gain/mix code below it.


## Part D — Connect parameters and bypass to the callback

### Step 28: Give the processor its registered parameters

**File:** `tremolo_plugin/include/Tremolo/PluginProcessor.h`

**Why:** Constructing this member registers the three parameters with this processor.

**Where:** Find these existing lines:

```cpp
  // TODO: add parameters
```

Replace only those lines with:

```cpp
  Parameters parameters{*this};
```

### Step 29: Store the bypass transition helper

**File:** `tremolo_plugin/include/Tremolo/PluginProcessor.h`

**Why:** The dry buffer and gain ramps must persist between audio callbacks.

**Where:** Find this anchor:

```cpp
  Tremolo tremolo;
```

Copy these lines immediately **after** it:

```cpp
  BypassTransitionSmoother bypassTransitionSmoother;
```

### Step 30: Declare the host and test accessors

**File:** `tremolo_plugin/include/Tremolo/PluginProcessor.h`

**Why:** The host needs access to the bypass parameter. The optional serializer tests need references to the registered controls.

**Where:** Find this anchor:

```cpp
  void setStateInformation(const void* data, int sizeInBytes) override;
```

Copy these lines immediately **after** it:

```cpp
  [[nodiscard]] Parameters& getParameterRefs() noexcept;
  juce::AudioProcessorParameter* getBypassParameter() const noexcept override;
```

**Check:** These declarations are public, before `private:`.

### Step 31: Prepare the bypass buffer before playback

**File:** `tremolo_plugin/source/PluginProcessor.cpp`

**Why:** Allocate dry-buffer storage before real-time processing. Support whichever input/output bus has more channels.

**Where:** Find this anchor:

```cpp
  tremolo.prepare(sampleRate, expectedMaxFramesPerBlock);
```

Copy these lines immediately **after** it:

```cpp
  bypassTransitionSmoother.prepare(
      {.sampleRate = sampleRate,
       .maximumBlockSize = static_cast<juce::uint32>(expectedMaxFramesPerBlock),
       .numChannels = static_cast<juce::uint32>(juce::jmax(
           getTotalNumInputChannels(), getTotalNumOutputChannels()))});
```

### Step 32: Reset bypass when resources are released

**File:** `tremolo_plugin/source/PluginProcessor.cpp`

**Why:** Reset both the sound engine and its dry/wet transition state.

**Where:** Find this anchor:

```cpp
  tremolo.reset();
```

Copy these lines immediately **after** it:

```cpp
  bypassTransitionSmoother.reset();
```

### Step 33: Read parameters and decide whether to process

**File:** `tremolo_plugin/source/PluginProcessor.cpp`

**Why:** Update bypass first, so its initial fade block is not skipped. Then update rate/shape and return only when fully bypassed.

**Where:** Find these existing lines:

```cpp
  // TODO: update parameters
  // TODO: check for bypass
```

Replace only those lines with:

```cpp
  const auto bypassed = parameters.bypassed.get();
  bypassTransitionSmoother.setBypass(bypassed);

  const auto fullyBypassed =
      bypassed && !bypassTransitionSmoother.isTransitioning();
  const auto smoothing =
      fullyBypassed ? ApplySmoothing::no : ApplySmoothing::yes;

  tremolo.setModulationRateHz(parameters.rate.get(), smoothing);
  tremolo.setLfoWaveform(
      static_cast<Tremolo::LfoWaveform>(parameters.waveform.getIndex()),
      smoothing);

  if (fullyBypassed) {
    return;
  }
```

**Check:** Keep `ScopedNoDenormals`, the output-channel clearing loop, and the existing `tremolo.process(buffer);` call.

### Step 34: Save dry audio before applying tremolo

**File:** `tremolo_plugin/source/PluginProcessor.cpp`

**Why:** The transition needs an unprocessed copy of the input.

**Where:** Find this anchor:

```cpp
  // apply tremolo
```

Copy these lines immediately **before** it:

```cpp
  bypassTransitionSmoother.setDryBuffer(buffer);
```

### Step 35: Mix the transition after applying tremolo

**File:** `tremolo_plugin/source/PluginProcessor.cpp`

**Why:** The current buffer is wet audio; combine it with the stored dry input using the complementary gain ramps.

**Where:** Find this anchor:

```cpp
  tremolo.process(buffer);
```

Copy these lines immediately **after** it:

```cpp
  bypassTransitionSmoother.mixToWetBuffer(buffer);
```

**Check:** The processing order is save dry → tremolo → mix. Fully bypassed blocks return before these calls, so the LFO pauses while bypassed.

### Step 36: Define the two accessors

**File:** `tremolo_plugin/source/PluginProcessor.cpp`

**Why:** Return the registered parameter references without allocating or creating new parameters.

**Where:** Near the bottom of the file, find the closing `}  // namespace tremolo` **above the global `createPluginFilter()` function**. Insert these definitions immediately before that closing brace:

```cpp
Parameters& PluginProcessor::getParameterRefs() noexcept {
  return parameters;
}

juce::AudioProcessorParameter* PluginProcessor::getBypassParameter()
    const noexcept {
  return &parameters.bypassed;
}
```


**Checkpoint B:** Build again. Load the plugin into a host and use its generic parameter controls or automation to change rate, shape, and bypass. Keep `hasEditor()` returning `false` and `createEditor()` returning `nullptr`; neither method needs editing. The standalone app may not offer convenient controls without an editor.

The callback now updates parameters once per block, advances the LFO once per frame, and applies the same gain to all channels. Do not add allocations, file access, logging, locks, or UI calls to the audio callback.

## Part E — Optional project/preset recall

You can hear and automate the effect without this part. Complete it before relying on saved host projects. Each edit below extends the existing serializer stubs; no file replacement is needed.

### Step 37: Define the serializable parameter representation

**File:** `tremolo_plugin/source/JsonSerializer.cpp`

**Why:** JUCE marshalling converts this versioned representation to and from JSON. It stores the plugin name, rate, bypass, and waveform name without requiring another JSON library.

**Where:** At the top of the file, immediately **before** the existing `namespace tremolo {`, add this anonymous namespace:

```cpp
namespace {
struct SerializableParameters {
  float rate;
  bool bypassed;
  juce::String waveform;

  static constexpr auto marshallingVersion = 1;

  template <typename Archive, typename T>
  static void serialise(Archive& archive, T& p) {
    using namespace juce;

    if (archive.getVersion() != 1) {
      return;
    }

    std::string pluginName = TREMOLO_PLUGIN_NAME;

    archive(named("pluginName", pluginName));

    if (pluginName != TREMOLO_PLUGIN_NAME) {
      return;
    }

    archive(named("modulationRateHz", p.rate), named("bypassed", p.bypassed),
            named("modulationWaveform", p.waveform));
  }
};

SerializableParameters from(const tremolo::Parameters& p) {
  return {
      .rate = p.rate.get(),
      .bypassed = p.bypassed.get(),
      .waveform = p.waveform.getCurrentChoiceName(),
  };
}
}  // namespace
```

### Step 38: Write JSON to the supplied stream

**File:** `tremolo_plugin/source/JsonSerializer.cpp`

**Why:** Convert the current registered values into JSON and write them to JUCE's stream.

**Where:** Find these existing lines:

```cpp
  juce::ignoreUnused(parameters, output);

  // TODO: serialize parameters to the output stream as JSON
```

Replace only those lines with:

```cpp
  const auto json = juce::ToVar::convert(from(parameters));

  if (!json.has_value()) {
    return;
  }

  juce::JSON::writeToStream(output, *json,
                            juce::JSON::FormatOptions{}
                                .withSpacing(juce::JSON::Spacing::multiLine)
                                .withMaxDecimalPlaces(2));
```

### Step 39: Parse and validate saved parameters

**File:** `tremolo_plugin/source/JsonSerializer.cpp`

**Why:** Reject malformed data and unknown waveform names before assigning values to the registered parameters.

**Where:** Find these existing lines:

```cpp
  juce::ignoreUnused(input, parameters);

  // TODO: deserialize parameters from the JSON input stream

  return juce::Result::fail("not implemented");
```

Replace only those lines with:

```cpp
  juce::var parsedResult;
  auto parsingResult =
      juce::JSON::parse(input.readEntireStreamAsString(), parsedResult);

  if (parsingResult.failed()) {
    return parsingResult;
  }

  const auto parsedParameters =
      juce::FromVar::convert<SerializableParameters>(parsedResult);

  if (!parsedParameters.has_value()) {
    return juce::Result::fail(
        "failed to parse parameters from JSON representation");
  }

  const auto modulationWaveformIndex =
      parameters.waveform.choices.indexOf(parsedParameters->waveform);
  if (modulationWaveformIndex < 0) {
    // don't update parameters if modulation waveform name is invalid
    return juce::Result::fail(
        "invalid modulation waveform name; supported values are: " +
        parameters.waveform.choices.joinIntoString(", "));
  }

  parameters.waveform = modulationWaveformIndex;
  parameters.rate = parsedParameters->rate;
  parameters.bypassed = parsedParameters->bypassed;

  return juce::Result::ok();
```

**Check:** Replace the placeholder failure return too. Leave the existing method signature and its closing brace.

### Step 40: Connect host saving to the serializer

**File:** `tremolo_plugin/source/PluginProcessor.cpp`

**Why:** The host provides a memory block in which to store the parameter state.

**Where:** Find these existing lines:

```cpp
  juce::ignoreUnused(destData);

  // TODO: implement state serialization to JSON
```

Replace only those lines with:

```cpp
  juce::MemoryOutputStream outputStream{destData, true};
  JsonSerializer::serialize(parameters, outputStream);
```

**Check:** This goes inside the existing `getStateInformation()` method. Its explanatory comments may remain.

### Step 41: Connect host restore to the serializer

**File:** `tremolo_plugin/source/PluginProcessor.cpp`

**Why:** Restore parameters, return on error, then synchronize the DSP settings without starting new ramps.

**Where:** Find these existing lines:

```cpp
  juce::ignoreUnused(data, sizeInBytes);

  // TODO: implement state deserialization from JSON
```

Replace only those lines with:

```cpp
  juce::MemoryInputStream inputStream{data, static_cast<size_t>(sizeInBytes),
                                      false};
  const auto result = JsonSerializer::deserialize(inputStream, parameters);

  if (result.failed()) {
    // Notify the user that reading parameters failed.
    // Currently, we just write the error message to the standard error stream.
    DBG(result.getErrorMessage());
    return;
  }

  // Skip smoothing to avoid LFO waveform morphing
  // when loading a project or a preset.
  // For example, the default LFO waveform is the sine. If the project or preset
  // has the triangle selected, the user will see a curved triangle slope
  // on load, which is unexpected.
  bypassTransitionSmoother.setBypassForced(parameters.bypassed);
  tremolo.setLfoWaveform(
      static_cast<Tremolo::LfoWaveform>(parameters.waveform.getIndex()),
      ApplySmoothing::no);
  tremolo.setModulationRateHz(parameters.rate, ApplySmoothing::no);
```

**Check:** This goes inside the existing `setStateInformation()` method, not in the audio callback.


This course-level restore path directly updates DSP state and assumes it does not overlap audio processing. A production version should pass restored values to the audio thread. Also, `prepare()` resets bypass to active: if the host prepares after restoration, the next callback transitions toward the current bypass parameter.

### Step 42: Enable the existing serializer tests

**File:** `test/CMakeLists.txt`

**Why:** The tests verify saved JSON, successful restoration, and rejection of invalid waveform names.

**Where:** Inside `add_executable(TremoloCoursePluginTest ...)`, remove the leading `#` from the serializer test entry, so it reads:

```cmake
  source/JsonSerializerTest.cpp
```


## Build and verify

From the repository root, run:

```bash
cd todo
cmake --preset default-with-tests
cmake --build --preset default-with-tests
ctest --preset default-with-tests
```

The first configure may download dependencies. Run the commands from `todo/`; if your terminal is already there, omit `cd todo`. On macOS, you can limit the build to the VST3 and tests with:

```bash
cmake --build --preset default-with-tests --target TremoloCoursePlugin_VST3 TremoloCoursePluginTest
```

The Debug plugin artifacts are under `todo/cmake-build-with-tests/TremoloCoursePlugin_artefacts/Debug/`. The build does not automatically install the plugin into your host's plugin directory.

### What to check

| Check | Expected result |
| --- | --- |
| Sustained input at the default rate | About five volume pulses per second |
| Rate sweep from 0.1 to 20 Hz | Smooth change in pulse speed |
| Sine/triangle changes during playback | Short transition without an abrupt jump |
| Bypass on/off, including reversing mid-fade | Smooth dry/wet transition |
| Identical left/right input | Identical modulation on both channels |
| Save and reopen a project, if Part E is complete | All three controls return to their saved values |

The existing bypass tests contain numerical assertions for gain ramps, cross-block continuation, reversal, and forced bypass. `TremoloTest.cpp` exports waveform WAVs; it does not numerically prove that the shapes or transitions are correct. Inspect `sineLfo.wav`, `triangleLfo.wav`, and `smoothedLfo.wav` beside the test executable, normally in `todo/cmake-build-with-tests/test/`. They contain `0.4 × LFO`, because the test feeds ones and subtracts one from the output.

`PluginProcessorTest.cpp` only verifies construction. It does not validate callback ordering. For stronger verification, add numerical checks for unity-input output between 0.6 and 1.4, identical stereo modulation, continuity across blocks, and processing of the initial bypass-entry block.

You do not need a visualizer, FIFO, scratch-buffer optimization, custom look-and-feel, or custom editor to complete this sound backend.
