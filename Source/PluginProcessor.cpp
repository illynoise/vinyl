#include "PluginProcessor.h"
#include "gui/PluginEditor.h"

using namespace vs;

//============================================================================== factory presets
namespace
{
struct PresetValue { const char* pid; float value; };
struct FactoryPreset { const char* name; std::vector<PresetValue> values; };

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> list {
        { "Default", {} },
        { "Pristine Lacquer",  { { id::noise, -6.0f }, { id::crackle, -8.0f }, { id::tonearm, 10.0f } } },
        { "Worn Pressing",     { { id::generation, 1 }, { id::noise, 4.0f }, { id::crackle, 5.0f }, { id::clicks, 45.0f }, { id::density, 40.0f },
                                 { id::tonearm, 35.0f }, { id::wowDepth, 8.0f } } },
        { "DJ Battle Record",  { { id::generation, 1 }, { id::turntable, 1 }, { id::cartridge, 2 }, { id::drive, 35.0f }, { id::crackle, 6.0f },
                                 { id::clicks, 55.0f }, { id::density, 60.0f }, { id::wowDepth, 10.0f }, { id::flutterDepth, 8.0f }, { id::phaseLevel, 12.0f },
                                 { id::slowLength, 0.6f } } },
        { "End of Side",       { { id::generation, 1 }, { id::tonearm, 96.0f }, { id::crackle, 3.0f }, { id::clicks, 35.0f }, { id::density, 30.0f },
                                 { id::phaseLevel, 9.0f } } },
        { "Dusty Sample",      { { id::generation, 1 }, { id::turntable, 1 }, { id::noise, 8.0f }, { id::crackle, 10.0f }, { id::clicks, 60.0f },
                                 { id::density, 75.0f }, { id::drive, 20.0f }, { id::wowDepth, 14.0f }, { id::tonearm, 45.0f }, { id::output, -2.0f } } },
        { "Lo-Fi Bed",         { { id::generation, 1 }, { id::turntable, 1 }, { id::cartridge, 2 }, { id::drive, 55.0f }, { id::noise, 10.0f },
                                 { id::crackle, 8.0f }, { id::tonearm, 70.0f }, { id::wowDepth, 22.0f }, { id::flutterDepth, 18.0f }, { id::output, -3.0f } } },
        { "Tape Stop Spin-Down", { { id::slowLength, 2.5f }, { id::autoResume, 1 }, { id::crackle, 2.0f } } },
    };
    return list;
}
}

//==============================================================================
juce::String VinylStudioProcessor::variantName (bool isLite, bool isMono)
{
    return juce::String ("Vinyl Studio") + (isLite ? " Lite" : "") + (isMono ? " Mono" : "");
}

VinylStudioProcessor::VinylStudioProcessor (bool liteVersion, bool monoVersion)
    : AudioProcessor (BusesProperties().withInput ("Input", monoVersion ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", monoVersion ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undo, "VinylStudio", createLayout (liteVersion)),
      lite (liteVersion), mono (monoVersion)
{
    #define VS_BIND(n) v.n = apvts.getRawParameterValue (id::n);
    VS_BIND (console) VS_BIND (input) VS_BIND (drive) VS_BIND (generation) VS_BIND (turntable) VS_BIND (cartridge) VS_BIND (tonearm)
    VS_BIND (wowRate) VS_BIND (wowDepth) VS_BIND (flutterRate) VS_BIND (flutterDepth) VS_BIND (phaseOn) VS_BIND (phaseLevel) VS_BIND (modHP) VS_BIND (modLP)
    VS_BIND (noise) VS_BIND (crackle) VS_BIND (clicks) VS_BIND (density) VS_BIND (clicksGain) VS_BIND (slowLength) VS_BIND (slowBeats) VS_BIND (slowFormat)
    VS_BIND (stop) VS_BIND (autoResume) VS_BIND (output) VS_BIND (meterSource) VS_BIND (vuCal) VS_BIND (monitor)
    #undef VS_BIND
    syncSetup();
    startTimerHz (30);
}

VinylStudioProcessor::~VinylStudioProcessor() { stopTimer(); }

const juce::String VinylStudioProcessor::getName() const { return variantName (lite, mono); }

bool VinylStudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto want = mono ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    return l.getMainInputChannelSet() == want && l.getMainOutputChannelSet() == want;
}

//============================================================================== audio
void VinylStudioProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    noise.prepare (fs, library.operator->(), (uint64_t) juce::Random::getSystemRandom().nextInt64() | 1u);
    lastProfileKey = -1;
    slow.prepare (fs);
    for (auto& m : vu) m.prepare (fs);
    inGain.reset (fs, 0.03); outGain.reset (fs, 0.03);
    inGain.setCurrentAndTargetValue ((float) dbToGain (v.input->load()));
    outGain.setCurrentAndTargetValue ((float) dbToGain (v.output->load()));
    duckA = std::exp (-1.0 / (fs * 0.15)); duckEnv = 0;
    conRng.seed (4242);
    if (! lite) { chain.prepare (fs); setLatencySamples (chain.latencySamples()); }
    else setLatencySamples (0);
}

void VinylStudioProcessor::reset()
{
    noise.reset(); slow.reset(); if (! lite) chain.reset();
    duckEnv = 0;
}

void VinylStudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int total = buffer.getNumSamples(), numCh = buffer.getNumChannels();
    if (numCh == 0 || total == 0) return;
    for (int c = getTotalNumInputChannels(); c < numCh; ++c) buffer.clear (c, 0, total);

    //-- parameters for this block
    const bool full = ! lite;
    const int gen = full ? (int) v.generation->load() : 0, tt = full ? (int) v.turntable->load() : 0, cart = full ? (int) v.cartridge->load() : 0;
    const int key = gen * 100 + tt * 10 + cart;
    const NoiseProfile prof = noiseProfile (gen, tt, cart);
    if (key != lastProfileKey) { lastProfileKey = key; noise.setProfile (prof); }      // one of 12 noise profiles

    const float driveDb = full ? v.drive->load() * 0.24f : 0.0f;                       // up to +24 dB into the chain, taken off at the output
    inGain.setTargetValue ((float) dbToGain (v.input->load()));
    outGain.setTargetValue ((float) dbToGain (v.output->load() - driveDb));
    const float driveGain = (float) dbToGain (driveDb);

    NoiseEngine::Controls nc;
    const float nz = v.noise->load(), cr = v.crackle->load();
    nc.noiseGain = nz <= kNoiseMinDb + 0.01f ? 0.0f : (float) dbToGain (nz);
    nc.crackleGain = cr <= kNoiseMinDb + 0.01f ? 0.0f : (float) dbToGain (cr);
    nc.clickRate = NoiseEngine::densityToRate (v.density->load());
    nc.clickGain = nc.clickRate > 0 ? NoiseEngine::clicksLevelToGain (v.clicks->load()) * (full ? (float) dbToGain (v.clicksGain->load()) : 1.0f) : 0.0f;
    nc.rumbleGain = nc.noiseGain;                                                      // rumble follows the Noise level and the turntable

    VinylChain::Params cp;
    if (full)
    {
        cp.console = v.console->load() > 0.5f; cp.phaseOn = v.phaseOn->load() > 0.5f;
        cp.generation = gen; cp.turntable = tt; cp.cartridge = cart;
        cp.tonearm = v.tonearm->load() * 0.01f;
        cp.wowRate = v.wowRate->load(); cp.wowDepth = v.wowDepth->load();
        cp.flutterRate = v.flutterRate->load(); cp.flutterDepth = v.flutterDepth->load();
        cp.phaseLevelDb = v.phaseLevel->load(); cp.modHP = v.modHP->load(); cp.modLP = juce::jmax (v.modLP->load(), v.modHP->load() * 1.05f);
        cp.noiseTiltDb = prof.tiltDb;
    }

    //-- slow down: length in seconds or in beats of the host tempo
    double length = v.slowLength->load();
    if (v.slowFormat->load() > 0.5f)
    {
        double bpm = 120.0;
        if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto b = pos->getBpm()) bpm = *b;
        length = kBeatCounts[juce::jlimit (0, 5, (int) v.slowBeats->load())] * 60.0 / juce::jmax (20.0, bpm);
    }
    const bool stopParam = v.stop->load() > 0.5f;
    if (! stopParam) autoResumed.store (false);
    const bool stop = stopParam && ! autoResumed.load();
    const double spinTau = tt == 1 ? 0.12 : 0.45;                                      // direct drive gets back to speed much faster

    const bool meterIn = v.meterSource->load() < 0.5f;
    const int monitor = full ? (int) v.monitor->load() : 0;
    const float conNoise = (full && cp.console) ? 4.5e-5f : 0.0f;                      // console noise floor, about -90 dBFS rms

    float* in0 = buffer.getWritePointer (0);
    float* in1 = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int pos = 0; pos < total;)
    {
        const int n = juce::jmin ((int) NoiseEngine::maxChunk, total - pos);
        float l[NoiseEngine::maxChunk], r[NoiseEngine::maxChunk], nl[NoiseEngine::maxChunk], nr[NoiseEngine::maxChunk];
        for (int i = 0; i < n; ++i)
        {
            const float g = inGain.getNextValue();
            l[i] = in0[pos + i] * g; r[i] = (in1 != nullptr ? in1[pos + i] : in0[pos + i]) * g;
            if (meterIn) { vu[0].push (l[i]); vu[1].push (r[i]); }
            const double a = std::max (std::abs (l[i]), std::abs (r[i]));
            duckEnv = a > duckEnv ? a : a + duckA * (duckEnv - a);
        }
        nc.duck = 1.0f - 0.2f * (float) juce::jmin (1.0, duckEnv * 4.0);               // crackle dips ~2 dB under loud programme

        if (full)
        {
            for (int i = 0; i < n; ++i) { l[i] = l[i] * driveGain + conNoise * (float) conRng.bip(); r[i] = r[i] * driveGain + conNoise * (float) conRng.bip(); }
            chain.update (cp);
            chain.processRecord (l, r, n);
        }
        noise.process (nl, nr, n, nc);
        if (full) chain.colourNoise (nl, nr, n);
        const float ng = full ? driveGain : 1.0f;      // keep the noise level independent of Drive (the output trim undoes it)
        for (int i = 0; i < n; ++i) { l[i] += nl[i] * ng; r[i] += nr[i] * ng; }
        if (full) chain.processPlayback (l, r, n);

        slow.process (l, r, n, stop, length, spinTau);

        for (int i = 0; i < n; ++i)
        {
            const float g = outGain.getNextValue();
            float a = l[i] * g, b = r[i] * g;
            switch (monitor) { case 1: a = b = 0.5f * (a + b); break; case 2: b = a; break; case 3: a = b; break; default: break; }
            if (! meterIn) { vu[0].push (a); vu[1].push (b); }
            if (in1 != nullptr) { in0[pos + i] = a; in1[pos + i] = b; }
            else in0[pos + i] = 0.5f * (a + b);
        }
        pos += n;
    }

    if (slow.consumeReached() && v.autoResume->load() > 0.5f) { autoResumed.store (true); wantResume.store (true); }
    platterSpeed.store (slow.speedOut.load(), std::memory_order_relaxed);
    vuL.store (vu[0].value(), std::memory_order_relaxed); vuR.store (vu[1].value(), std::memory_order_relaxed);
    const double took = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    cpuLoad.store ((float) (0.95 * cpuLoad.load() + 0.05 * took / (total / fs)), std::memory_order_relaxed);
}

//============================================================================== message-thread housekeeping
void VinylStudioProcessor::setParam (const char* pid, float realValue)
{
    if (auto* p = apvts.getParameter (pid))
    {
        const float norm = p->convertTo0to1 (realValue);
        if (std::abs (norm - p->getValue()) > 1e-6f)
        {
            p->beginChangeGesture(); p->setValueNotifyingHost (norm); p->endChangeGesture();
        }
    }
}

void VinylStudioProcessor::timerCallback()
{
    // Auto Resume: release the Stop button once the platter has come to rest
    if (wantResume.exchange (false)) setParam (id::stop, 0.0f);
    // wow / flutter / phase-distortion defaults follow the Generation, Turntable and Cartridge setup
    if (! lite)
    {
        const int key = (int) v.generation->load() * 100 + (int) v.turntable->load() * 10 + (int) v.cartridge->load();
        if (key != lastSetupKey) { lastSetupKey = key; applySetupDefaults(); }
    }
}

void VinylStudioProcessor::syncSetup()
{
    if (! lite) lastSetupKey = (int) v.generation->load() * 100 + (int) v.turntable->load() * 10 + (int) v.cartridge->load();
}

void VinylStudioProcessor::applySetupDefaults()
{
    if (lite) return;
    const auto d = setupDefaults ((int) v.generation->load(), (int) v.turntable->load(), (int) v.cartridge->load());
    setParam (id::wowRate, d.wowRate); setParam (id::flutterRate, d.flutterRate);
    setParam (id::phaseLevel, d.phaseLevel); setParam (id::modHP, d.modHP); setParam (id::modLP, d.modLP);
}

//============================================================================== programs / presets
int VinylStudioProcessor::getNumPrograms() { return (int) factoryPresets().size(); }
const juce::String VinylStudioProcessor::getProgramName (int i) { return juce::isPositiveAndBelow (i, getNumPrograms()) ? factoryPresets()[(size_t) i].name : juce::String(); }
void VinylStudioProcessor::setCurrentProgram (int i) { if (juce::isPositiveAndBelow (i, getNumPrograms())) loadFactoryPreset (i); }

juce::StringArray VinylStudioProcessor::getFactoryPresetNames() const
{
    juce::StringArray a;
    for (auto& p : factoryPresets()) a.add (p.name);
    return a;
}

void VinylStudioProcessor::loadFactoryPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) factoryPresets().size())) return;
    const auto& preset = factoryPresets()[(size_t) index];
    undo.beginNewTransaction ("Load preset");
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != id::stop) rp->setValueNotifyingHost (rp->getDefaultValue());
    auto apply = [&] (bool setupOnly)
    {
        for (auto& pv : preset.values)
        {
            const bool isSetup = juce::String (pv.pid) == id::generation || juce::String (pv.pid) == id::turntable || juce::String (pv.pid) == id::cartridge;
            if (isSetup == setupOnly) if (auto* p = apvts.getParameter (pv.pid)) p->setValueNotifyingHost (p->convertTo0to1 (pv.value));
        }
    };
    apply (true); applySetupDefaults(); syncSetup(); apply (false);
    currentProgram = index; presetName = preset.name;
}

juce::File VinylStudioProcessor::getUserPresetDirectory()
{
   #if JUCE_MAC
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Presets/Base Jase Plugins/Vinyl Studio");
   #elif JUCE_WINDOWS
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Base Jase Plugins/Vinyl Studio/Presets");
   #else
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".config/Base Jase Plugins/Vinyl Studio/Presets");
   #endif
    return dir;
}

juce::Array<juce::File> VinylStudioProcessor::getUserPresets() const
{
    auto files = getUserPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.vspreset");
    files.sort();
    return files;
}

juce::ValueTree VinylStudioProcessor::captureState()
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", presetName, nullptr);
    state.setProperty ("variant", getName(), nullptr);
    return state;
}

void VinylStudioProcessor::restoreState (const juce::ValueTree& state)
{
    if (! state.isValid()) return;
    const bool wasStopped = v.stop->load() > 0.5f;
    apvts.replaceState (state.createCopy());
    presetName = state.getProperty ("presetName", presetName).toString();
    // switches and selectors report exactly the stored position again (a host may have left
    // them at an in-between value that maps to the same position)
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const float snapped = rp->convertTo0to1 (rp->convertFrom0to1 (rp->getValue()));
            if (rp->isDiscrete() && std::abs (snapped - rp->getValue()) > 1e-6f) rp->setValueNotifyingHost (snapped);
        }
    syncSetup();          // a restored state keeps its own wow / flutter / phase values
    juce::ignoreUnused (wasStopped);
}

bool VinylStudioProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto dir = getUserPresetDirectory();
    if (! dir.createDirectory()) return false;
    presetName = clean;
    if (auto xml = captureState().createXml()) return xml->writeTo (dir.getChildFile (clean + ".vspreset"));
    return false;
}

void VinylStudioProcessor::loadUserPreset (const juce::File& f)
{
    if (auto xml = juce::XmlDocument::parse (f))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (state.hasType (apvts.state.getType()))
        {
            state.setProperty ("presetName", f.getFileNameWithoutExtension(), nullptr);
            // presets never leave the record stopped
            for (auto child : state) if (child.getProperty ("id").toString() == id::stop) child.setProperty ("value", 0.0f, nullptr);
            restoreState (state);
        }
    }
}

void VinylStudioProcessor::toggleAB()
{
    auto current = captureState();
    if (slotOther.isValid()) restoreState (slotOther);
    slotOther = current;
    slotB = ! slotB;
}

//============================================================================== session state
void VinylStudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = captureState();
    state.setProperty ("guiWidth", guiWidth, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void VinylStudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (state.hasType (apvts.state.getType()))
        {
            guiWidth = state.getProperty ("guiWidth", guiWidth);
            restoreState (state);
        }
    }
}

juce::AudioProcessorEditor* VinylStudioProcessor::createEditor() { return new VinylStudioEditor (*this); }

//==============================================================================
#if ! VS_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
   #ifndef VS_LITE
    #define VS_LITE 0
   #endif
   #ifndef VS_MONO
    #define VS_MONO 0
   #endif
    return new VinylStudioProcessor (VS_LITE != 0, VS_MONO != 0);
}
#endif
