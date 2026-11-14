# Vinyl Studio - Base Jase Plugins
cmake_minimum_required(VERSION 3.22)

# macOS: universal binaries (Apple silicon + Intel). Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(VinylStudio VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(VS_COPY_AFTER_BUILD "Install the plugins into the user plugin folders after every build" ON)
option(VS_BUILD_TESTS "Build the offline test / benchmark tool" ON)
set(VS_VARIANTS "Stereo;Mono;StereoLite;MonoLite" CACHE STRING "Variants to build")

# JUCE: use ./JUCE if present, otherwise fetch it
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# sample library + logo, embedded so the plugin has no external files
juce_add_binary_data(VinylStudioAssets SOURCES
    Assets/noise_burgh.bin Assets/noise_needle.bin Assets/noise_noise02.bin Assets/noise_asmr.bin
    Assets/hiss_burgh.flac Assets/hiss_needle.flac Assets/hiss_noise02.flac Assets/hiss_asmr.flac
    Assets/basejase_logo.svg)
set_target_properties(VinylStudioAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(VS_SOURCES
    Source/PluginProcessor.cpp
    Source/dsp/NoiseLibrary.cpp Source/dsp/NoiseEngine.cpp Source/dsp/VinylChain.cpp
    Source/gui/Look.cpp Source/gui/Controls.cpp Source/gui/LatheScene.cpp Source/gui/PluginEditor.cpp)

if(APPLE)
    set(VS_FORMATS VST3 AU Standalone)      # AU is macOS only
else()
    set(VS_FORMATS VST3 Standalone)
endif()

function(vs_configure target)
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=0 JUCE_USE_MP3AUDIOFORMAT=0 JUCE_REPORT_APP_USAGE=0
        VS_VERSION="${PROJECT_VERSION}")
    target_link_libraries(${target}
        PRIVATE VinylStudioAssets juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
endfunction()

# name / product / 4-char plugin code / bundle suffix / lite / mono
function(vs_add_variant name product code suffix lite mono)
    juce_add_plugin(${name}
        COMPANY_NAME "Base Jase Plugins"
        COMPANY_WEBSITE "https://basejaseplugins.com"
        BUNDLE_ID "com.basejaseplugins.vinylstudio${suffix}"
        PLUGIN_MANUFACTURER_CODE Bjsp
        PLUGIN_CODE ${code}
        PRODUCT_NAME "${product}"
        PLUGIN_NAME "${product}"
        DESCRIPTION "Vinyl emulation by Base Jase Plugins"
        FORMATS ${VS_FORMATS}
        IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES Fx Distortion Mastering
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD ${VS_COPY_AFTER_BUILD})
    target_sources(${name} PRIVATE ${VS_SOURCES})
    target_compile_definitions(${name} PUBLIC VS_LITE=${lite} VS_MONO=${mono})
    vs_configure(${name})
endfunction()

if("Stereo" IN_LIST VS_VARIANTS)
    vs_add_variant(VinylStudio         "Vinyl Studio"           Bjv1 ""         0 0)
endif()
if("Mono" IN_LIST VS_VARIANTS)
    vs_add_variant(VinylStudioMono     "Vinyl Studio Mono"      Bjv2 "mono"     0 1)
endif()
if("StereoLite" IN_LIST VS_VARIANTS)
    vs_add_variant(VinylStudioLite     "Vinyl Studio Lite"      Bjv3 "lite"     1 0)
endif()
if("MonoLite" IN_LIST VS_VARIANTS)
    vs_add_variant(VinylStudioLiteMono "Vinyl Studio Lite Mono" Bjv4 "litemono" 1 1)
endif()

if(VS_BUILD_TESTS)
    juce_add_console_app(vs_tests PRODUCT_NAME "vs_tests")
    target_sources(vs_tests PRIVATE tests/vs_tests.cpp ${VS_SOURCES})
    target_compile_definitions(vs_tests PUBLIC VS_NO_PLUGIN_ENTRY=1 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Vinyl Studio"
        JucePlugin_Build_Standalone=1 JucePlugin_VersionString="${PROJECT_VERSION}")
    vs_configure(vs_tests)
endif()
