project(TremoloCoursePluginTest)

set(TREMOLO_TEST_INPUT_WAV "" CACHE FILEPATH "Optional mono/stereo WAV to render through the tremolo tests")
if(TREMOLO_TEST_INPUT_WAV)
  get_filename_component(TREMOLO_TEST_INPUT_WAV "${TREMOLO_TEST_INPUT_WAV}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
  if(NOT EXISTS "${TREMOLO_TEST_INPUT_WAV}" OR IS_DIRECTORY "${TREMOLO_TEST_INPUT_WAV}")
    message(FATAL_ERROR "TREMOLO_TEST_INPUT_WAV must name an existing WAV file: ${TREMOLO_TEST_INPUT_WAV}")
  endif()
endif()
configure_file(AudioTestConfig.h.in generated/AudioTestConfig.h @ONLY)

# Signals to CMake that we want to run tests from this directory.
enable_testing()

# Adds googletest.
cpmaddpackage(
  NAME
    GOOGLETEST
  GITHUB_REPOSITORY
    google/googletest
  VERSION
    1.16.0
  SOURCE_DIR
    ${LIB_DIR}/googletest
  OPTIONS
    "INSTALL_GTEST OFF"
    "gtest_force_shared_crt ON"
)

cpmaddpackage(
  NAME
    WOLFSOUND_DSP_UTILS
  GITHUB_REPOSITORY
    JanWilczek/wolfsound-dsp-utils
  VERSION
    0.4.0
  SOURCE_DIR
    ${LIB_DIR}/wolfsound-dsp-utils
  OPTIONS
    "BUILD_TESTS OFF"
)

# Creates the test console application.
add_executable(TremoloCoursePluginTest
  source/BypassTransitionSmootherTest.cpp
  source/PluginProcessorTest.cpp
  # source/JsonSerializerTest.cpp
  source/TremoloAudioTest.cpp
)

target_include_directories(TremoloCoursePluginTest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Configuration options that apply to the JUCE sources in the test target
target_compile_definitions(TremoloCoursePluginTest
  PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # We need to re-export these definitions to the test project, because the tremolo_plugin module
    # does not define them
    JucePlugin_Manufacturer="$<TARGET_PROPERTY:TremoloCoursePlugin,JUCE_COMPANY_NAME>"
    JucePlugin_Name="$<TARGET_PROPERTY:TremoloCoursePlugin,JUCE_PLUGIN_NAME>"
    JucePlugin_VersionString="$<TARGET_PROPERTY:TremoloCoursePlugin,JUCE_VERSION>"
)

# Thanks to the fact that we link against the gtest_main library, we don't have to write the main function ourselves.
target_link_libraries(
  TremoloCoursePluginTest
  PRIVATE
    tremolo::tremolo_plugin
    GTest::gtest_main
    wolfsound::wolfsound_dsp_utils
)

# Adds googletest-specific CMake commands at our disposal.
include(GoogleTest)

# Add all tests defined with googletest to the CMake metadata
# so that these tests are run upon a call to ctest in the test
# projects' binary directory.
gtest_discover_tests(TremoloCoursePluginTest DISCOVERY_TIMEOUT 60 DISCOVERY_MODE PRE_TEST)

