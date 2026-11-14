// Vinyl Studio - Base Jase Plugins
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "dsp/NoiseEngine.h"
#include "dsp/VinylChain.h"
#include "dsp/SlowDown.h"

#ifndef VS_VERSION
 #define VS_VERSION "1.0.0"
#endif

class VinylStudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    VinylStudioProcessor (bool liteVersion, bool monoVersion);
    ~VinylStudioProcessor() override;

    //== audio
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    void reset() override;

    //== info
    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //== programs = factory presets
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    //== state
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //== presets
    juce::StringArray getFactoryPresetNames() const;
    void loadFactoryPreset (int index);
    static juce::File getUserPresetDirectory();
    juce::Array<juce::File> getUserPresets() const;
    bool saveUserPreset (const juce::String& name);
    void loadUserPreset (const juce::File&);
    void toggleAB();
    bool isSlotB() const noexcept { return slotB; }
    juce::String presetName { "Default" };

    //== shared with the editor
    juce::AudioProcessorValueTreeState apvts;
    juce::UndoManager undo { 30000, 60 };
    const bool lite, mono;
    std::atomic<float> vuL { 0 }, vuR { 0 }, platterSpeed { 1 }, cpuLoad { 0 };
    int guiWidth = 0;                         // last editor width, saved with the session
    float param (const char* pid) const { if (auto* p = apvts.getRawParameterValue (pid)) return p->load(); return 0.0f; }
    void setParam (const char* pid, float realValue);
    static juce::String variantName (bool lite, bool mono);

private:
    void timerCallback() override;
    void applySetupDefaults();
    void syncSetup();
    juce::ValueTree captureState();
    void restoreState (const juce::ValueTree&);

    juce::SharedResourcePointer<vs::NoiseLibrary> library;
    vs::NoiseEngine noise;
    vs::VinylChain chain;
    vs::SlowDown slow;
    vs::VuBallistics vu[2];
    juce::SmoothedValue<float> inGain, outGain;
    double fs = 48000.0, duckEnv = 0, duckA = 0;
    int lastProfileKey = -1, lastSetupKey = -1, currentProgram = 0;
    std::atomic<bool> autoResumed { false }, wantResume { false };
    vs::Rng conRng;
    juce::ValueTree slotOther; bool slotB = false;

    struct P { std::atomic<float>* console, *input, *drive, *generation, *turntable, *cartridge, *tonearm, *wowRate, *wowDepth, *flutterRate, *flutterDepth,
               *phaseOn, *phaseLevel, *modHP, *modLP, *noise, *crackle, *clicks, *density, *clicksGain, *slowLength, *slowBeats, *slowFormat, *stop, *autoResume,
               *output, *meterSource, *vuCal, *monitor; } v {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VinylStudioProcessor)
};
