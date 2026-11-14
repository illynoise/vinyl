#include "NoiseLibrary.h"
#include "Filters.h"
#include "BinaryData.h"

namespace vs
{
NoiseLibrary::NoiseLibrary()
{
    load (sets[0], BinaryData::noise_burgh_bin,   BinaryData::noise_burgh_binSize,   BinaryData::hiss_burgh_flac,   BinaryData::hiss_burgh_flacSize);
    load (sets[1], BinaryData::noise_needle_bin,  BinaryData::noise_needle_binSize,  BinaryData::hiss_needle_flac,  BinaryData::hiss_needle_flacSize);
    load (sets[2], BinaryData::noise_noise02_bin, BinaryData::noise_noise02_binSize, BinaryData::hiss_noise02_flac, BinaryData::hiss_noise02_flacSize);
    load (sets[3], BinaryData::noise_asmr_bin,    BinaryData::noise_asmr_binSize,    BinaryData::hiss_asmr_flac,    BinaryData::hiss_asmr_flacSize);
    buildSynth();
}

void NoiseLibrary::load (SampleSet& s, const char* bin, int binSize, const char* flac, int flacSize)
{
    juce::MemoryInputStream in (bin, (size_t) binSize, false);
    char magic[4]; in.read (magic, 4);
    jassert (std::memcmp (magic, "VSNL", 4) == 0);
    in.readInt();                                   // version
    in.readInt();                                   // sample rate (48000)
    s.mono = in.readInt() != 0;
    s.rotationPeriod = in.readFloat();
    s.loopScale = in.readFloat();
    in.read (&s.grain, sizeof (LayerStats));
    in.read (&s.click, sizeof (LayerStats));
    for (auto* table : { &s.grains, &s.clicks })
    {
        const int n = in.readInt();
        table->resize ((size_t) n);
        if (n > 0) in.read (table->data(), n * (int) sizeof (Event));
    }
    const auto remaining = (size_t) (in.getTotalLength() - in.getPosition());
    s.pcm.resize (remaining / sizeof (int16_t) + 8, 0);
    in.read (s.pcm.data(), (int) remaining);

    juce::FlacAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatReader> r (fmt.createReaderFor (new juce::MemoryInputStream (flac, (size_t) flacSize, false), true));
    jassert (r != nullptr);
    if (r == nullptr) return;
    s.loopCh = (int) r->numChannels;
    s.loopLen = (int) r->lengthInSamples;
    juce::AudioBuffer<int> tmp (s.loopCh, s.loopLen);
    r->read (tmp.getArrayOfWritePointers(), s.loopCh, 0, s.loopLen, false);      // left-justified 32-bit ints
    s.loop.resize ((size_t) (s.loopLen * s.loopCh));
    for (int c = 0; c < s.loopCh; ++c)
        for (int i = 0; i < s.loopLen; ++i)
            s.loop[(size_t) (i * s.loopCh + c)] = (int16_t) (tmp.getSample (c, i) >> 16);
}

void NoiseLibrary::buildSynth()
{
    const double fs = 48000.0;
    const int n = 32, len = 480;
    Rng rng; rng.seed (77);
    synth.mono = true;
    synth.pcm.assign ((size_t) (n * len * 2 + 8), 0);
    Biquad riaa; riaa.riaaPlayback (fs);
    for (int k = 0; k < n; ++k)
    {
        Biquad res; res.lowpass (fs, 15000.0 + 5000.0 * rng.uni(), 2.0 + 4.0 * rng.uni());
        riaa.reset();
        std::vector<double> y ((size_t) len, 0.0);
        const int second = 2 + rng.range (8);
        const double secondAmp = rng.uni() < 0.4 ? -(0.4 + 0.6 * rng.uni()) : 0.0;     // scratch edge: bipolar doublet
        double pk = 1e-9;
        for (int i = 0; i < len; ++i)
        {
            const double x = (i == 2 ? 1.0 : 0.0) + (i == 2 + second ? secondAmp : 0.0);
            y[(size_t) i] = riaa.process (res.process (x, 0), 0) * std::min (1.0, (len - i) / 200.0);
            pk = std::max (pk, std::abs (y[(size_t) i]));
        }
        for (int i = 0; i < len; ++i)
        {
            const auto v = (int16_t) std::lround (y[(size_t) i] / pk * 32000.0);
            synth.pcm[(size_t) ((k * len + i) * 2)] = v;
            synth.pcm[(size_t) ((k * len + i) * 2 + 1)] = v;
        }
        synth.clicks.push_back ({ (uint32_t) (k * len), (uint32_t) len, 1.0f, 45.0f, 1.0f / 32000.0f });
    }
}
} // namespace vs
