#include "AudioTestConfig.h"
#include "TestUtils.h"
#include <gtest/gtest.h>
#include <tremolo_plugin/tremolo_plugin.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

namespace tremolo {
namespace {
constexpr auto blockSize = 512;
constexpr float minGain = 0.6f;
constexpr float maxGain = 1.4f;
constexpr float epsilon = 1e-7f;
using TremoloAudioTest = testing::TestWithParam<Tremolo::LfoWaveform>;
enum class RenderMode { fixed, transition };

void openWav(const juce::File& file,
             std::unique_ptr<juce::AudioFormatReader>& reader) {
  auto inputStream = std::make_unique<juce::FileInputStream>(file);
  ASSERT_TRUE(inputStream->openedOk()) << file.getFullPathName();
  juce::WavAudioFormat format;
  reader.reset(format.createReaderFor(inputStream.release(), true));
  ASSERT_NE(reader, nullptr) << "Cannot decode WAV: " << file.getFullPathName();
}

void prepareOutputFile(Tremolo::LfoWaveform waveform, RenderMode mode,
                       juce::File& output) {
  const juce::File directory{getFileOutputPath("rendered")};
  ASSERT_TRUE(directory.createDirectory().wasOk());
  const auto startsWithSine = waveform == Tremolo::LfoWaveform::sine;
  const char* outFileName = startsWithSine ? "sine.wav" : "triangle.wav";
  if (mode == RenderMode::transition) {
    outFileName = startsWithSine ? "sine_to_triangle.wav" : "triangle_to_sine.wav";
  }
  output = directory.getChildFile(outFileName);
}

void createWavWriter(const juce::File& file,
                     const juce::AudioFormatReader& input,
                     std::unique_ptr<juce::AudioFormatWriter>& writer) {
  std::unique_ptr<juce::OutputStream> stream =
      file.createOutputStream();
  ASSERT_NE(stream, nullptr) << "Cannot open output: " << file.getFullPathName();
  const auto options =
      juce::AudioFormatWriterOptions{}
          .withSampleRate(input.sampleRate)
          .withNumChannels(static_cast<int>(input.numChannels))
          .withBitsPerSample(32)
          .withSampleFormat(
              juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
  juce::WavAudioFormat format;
  writer = format.createWriterFor(stream, options);
  ASSERT_NE(writer, nullptr) << "Cannot create WAV writer.";
}

void validateProcessedBlock(const juce::AudioBuffer<float>& dry,
                            const juce::AudioBuffer<float>& wet,
                            bool& inputHasSignal, bool& outputHasSignal) {
  for (int channel = 0; channel < wet.getNumChannels(); ++channel) {
    for (int frame = 0; frame < wet.getNumSamples(); ++frame) {
      const auto before = dry.getSample(channel, frame);
      const auto after = wet.getSample(channel, frame);
      ASSERT_LE(std::abs(after), maxGain * std::abs(before) + epsilon);
      ASSERT_GE(std::abs(after) + epsilon, minGain * std::abs(before));
      inputHasSignal = inputHasSignal || before != 0.f;
      outputHasSignal = outputHasSignal || after != 0.f;
    }
  }
}

void processWav(juce::AudioFormatReader& reader,
                juce::AudioFormatWriter& writer,
                Tremolo::LfoWaveform waveform, RenderMode mode) {
  Tremolo effect;
  effect.prepare(reader.sampleRate, blockSize);
  effect.setModulationRateHz(5.f, ApplySmoothing::no);
  effect.setLfoWaveform(waveform, ApplySmoothing::no);
  const auto channels = static_cast<int>(reader.numChannels);
  juce::AudioBuffer<float> dry{channels, blockSize};
  auto inputHasSignal = false;
  auto outputHasSignal = false;
  const auto switchFrame = reader.lengthInSamples / 2;
  for (juce::int64 offset = 0; offset < reader.lengthInSamples;) {
    if (mode == RenderMode::transition && offset == switchFrame) {
      const auto target = waveform == Tremolo::LfoWaveform::sine
                              ? Tremolo::LfoWaveform::triangle
                              : Tremolo::LfoWaveform::sine;
      effect.setLfoWaveform(target);
    }
    auto framesRemaining = reader.lengthInSamples - offset;
    if (mode == RenderMode::transition && offset < switchFrame) {
      // End this block exactly at the switch, even inside a 512-frame block.
      framesRemaining = std::min(framesRemaining, switchFrame - offset);
    }
    const auto frames = static_cast<int>(
        std::min<juce::int64>(blockSize, framesRemaining));
    dry.setSize(channels, frames);
    ASSERT_TRUE(reader.read(&dry, 0, frames, offset, true, true))
        << "WAV read failed at frame " << offset;
    auto wet = dry;
    effect.process(wet);
    ASSERT_NO_FATAL_FAILURE(
        validateProcessedBlock(dry, wet, inputHasSignal, outputHasSignal));
    ASSERT_TRUE(writer.writeFromAudioSampleBuffer(wet, 0, frames))
        << "WAV write failed at frame " << offset;
    offset += frames;
  }
  ASSERT_EQ(inputHasSignal, outputHasSignal);
}

void verifyRenderedWav(const juce::File& output,
                       const juce::AudioFormatReader& input) {
  std::unique_ptr<juce::AudioFormatReader> rendered;
  ASSERT_NO_FATAL_FAILURE(openWav(output, rendered));
  EXPECT_EQ(rendered->lengthInSamples, input.lengthInSamples);
  EXPECT_EQ(rendered->numChannels, input.numChannels);
  EXPECT_DOUBLE_EQ(rendered->sampleRate, input.sampleRate);
  EXPECT_TRUE(rendered->usesFloatingPointData);
}

void renderInputWav(Tremolo::LfoWaveform waveform, RenderMode mode) {
  if (juce::String{test::inputWavPath}.isEmpty()) {
    GTEST_SKIP() << "Configure with -DTREMOLO_TEST_INPUT_WAV=/absolute/path/input.wav";
  }

  const juce::File input{test::inputWavPath};
  std::unique_ptr<juce::AudioFormatReader> reader;
  ASSERT_NO_FATAL_FAILURE(openWav(input, reader));
  ASSERT_TRUE(reader->numChannels == 1 || reader->numChannels == 2)
      << "Use a mono or stereo WAV.";
  if (mode == RenderMode::transition) {
    ASSERT_GE(reader->lengthInSamples, 2)
        << "A transition render needs at least two frames.";
  }

  juce::File output;
  ASSERT_NO_FATAL_FAILURE(prepareOutputFile(waveform, mode, output));
  // Commit the finished render only after all processing and writes succeed.
  juce::TemporaryFile temporary{output};
  std::unique_ptr<juce::AudioFormatWriter> writer;
  ASSERT_NO_FATAL_FAILURE(createWavWriter(temporary.getFile(), *reader, writer));
  ASSERT_NO_FATAL_FAILURE(processWav(*reader, *writer, waveform, mode));
  ASSERT_TRUE(writer->flush()) << "Cannot flush output WAV.";
  writer.reset();
  ASSERT_TRUE(temporary.overwriteTargetFileWithTemporary());

  ASSERT_NO_FATAL_FAILURE(verifyRenderedWav(output, *reader));
  std::cout << "Rendered 5 Hz tremolo: " << output.getFullPathName() << '\n';
}

TEST_P(TremoloAudioTest, RendersInputWav) {
  ASSERT_NO_FATAL_FAILURE(renderInputWav(GetParam(), RenderMode::fixed));
}

TEST_P(TremoloAudioTest, RendersInputWavWithLfoTransition) {
  ASSERT_NO_FATAL_FAILURE(renderInputWav(GetParam(), RenderMode::transition));
}

INSTANTIATE_TEST_SUITE_P(
    Waveforms, TremoloAudioTest,
    testing::Values(Tremolo::LfoWaveform::sine, Tremolo::LfoWaveform::triangle),
    [](const testing::TestParamInfo<Tremolo::LfoWaveform>& waveformInfo) {
      return waveformInfo.param == Tremolo::LfoWaveform::sine ? "Sine" : "Triangle";
    });
}  // namespace
}  // namespace tremolo
