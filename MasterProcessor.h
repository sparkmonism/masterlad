#pragma once

#include "DspUtils.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

// Masterlad (Songcraft Audio): a mastering chain in one plugin.
//
//   Input gain -> EQ -> Multiband Dynamics -> Exciter -> Imager -> Maximizer -> meters
//
// Latency is constant whatever the settings: the exciter's oversampler is
// padded to its 8x latency and the maximizer always runs its look-ahead.
class MasterProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kEqBands = 6;
    static constexpr int kDynBands = 4;
    static constexpr int kChunk = 512;

    MasterProcessor();
    ~MasterProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Masterlad"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Parameter ids
    static juce::String eqId (int band, const char* suffix);   // band 0..5 -> "eq1_freq"
    static juce::String dynId (int band, const char* suffix);  // band 0..3 -> "dyn1_thr"
    static juce::String imgId (int band, const char* suffix);  // band 0..3 -> "img1_width"

    // EQ band filter type (band 0 = low shelf, 5 = high shelf, others bells)
    static mdsp::FilterType eqType (int band)
    {
        return band == 0 ? mdsp::FilterType::LowShelf
             : band == kEqBands - 1 ? mdsp::FilterType::HighShelf
                                    : mdsp::FilterType::Bell;
    }

    juce::AudioProcessorValueTreeState apvts;

    //==========================================================================
    // Meter data, written by the audio thread and read by the editor.
    // Peaks and gain reduction are "max since last read": the editor swaps them to 0.
    std::array<std::atomic<float>, 2> inPeak {}, outPeak {};
    std::array<std::atomic<float>, kDynBands> dynGr {};   // dB, positive
    std::atomic<float> maxGr { 0.0f };                    // dB, positive
    std::atomic<float> momentary { -120.0f }, shortTerm { -120.0f }, integrated { -120.0f };
    std::atomic<float> truePeakMax { -120.0f };           // dBTP since reset
    std::atomic<float> correlation { 0.0f };

    void resetLoudness() { resetRequested.store (true); truePeakMax.store (-120.0f); }

    // Output spectrum feed: the most recent samples of the (mono) output.
    static constexpr int kSpecRing = 8192;
    std::array<std::atomic<float>, kSpecRing> specRing {};
    std::atomic<uint32_t> specWrite { 0 };

    int getReportedLatency() const { return totalLatency; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct Params
    {
        bool bypass = false, eqOn = true, dynOn = false, excOn = false, imgOn = true, maxOn = true;
        int eqMode = 0;
        float inGainDb = 0.0f;

        struct EqBand { bool on; float f, g, q; } eq[kEqBands] {};
        float dynX[3] {};
        struct DynBand { bool byp; float thr, ratio, att, rel, gain; } dyn[kDynBands] {};

        float excAmount = 0.25f, excDrive = 12.0f, excFreq = 3000.0f;
        int excMode = 0, os = 2;

        float imgX[3] {};
        float imgW[kDynBands] {};

        float maxGain = 3.0f, maxCeil = -0.3f, maxRel = 150.0f;
        bool maxAuto = true;
    };

    struct Ptrs
    {
        std::atomic<float> *bypass, *eqOn, *dynOn, *excOn, *imgOn, *maxOn, *eqMode, *inGain;
        std::atomic<float> *eqEnabled[kEqBands], *eqF[kEqBands], *eqG[kEqBands], *eqQ[kEqBands];
        std::atomic<float> *dynX[3];
        std::atomic<float> *dynByp[kDynBands], *dynThr[kDynBands], *dynRatio[kDynBands],
                           *dynAtt[kDynBands], *dynRel[kDynBands], *dynGain[kDynBands];
        std::atomic<float> *excAmount, *excDrive, *excFreq, *excMode, *os;
        std::atomic<float> *imgX[3], *imgW[kDynBands];
        std::atomic<float> *maxGain, *maxCeil, *maxRel, *maxAuto;
    };

    Params readParams() const;
    void processChunk (float* l, float* r, int n, const Params& p);

    void runEq (float* l, float* r, int n, const Params& p);
    void runDynamics (float* l, float* r, int n, const Params& p);
    void runExciter (float* l, float* r, int n, const Params& p);
    void runImager (float* l, float* r, int n, const Params& p);

    template <typename Fn>
    void runModule (juce::SmoothedValue<float>& mix, bool on, float* l, float* r, int n, Fn&& fn);

    Ptrs ptrs {};

    double sr = 44100.0;
    int maxBlock = kChunk;

    // chunk scratch
    std::array<std::array<float, kChunk>, 2> tmp {}, exBuf {};

    // input gain, bypass and module mixes
    juce::SmoothedValue<float> inGainSm, bypassSm, eqMix, dynMix, imgMix, maxMix, excMix;

    // EQ
    std::array<std::array<mdsp::Biquad, kEqBands>, 2> eqFilt;

    // Dynamics
    mdsp::Crossover dynXover;
    std::array<float, kDynBands> dynGrState {};

    // Exciter
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers;
    int osLatency[4] { 0, 0, 0, 0 };
    int lastOs = -1;
    std::array<mdsp::Lr4, 2> excPreHp, excPostHp;
    std::array<mdsp::Delay, 2> excDry, excWet;
    std::array<float, 2> excDcX {}, excDcY {};

    // Imager
    mdsp::Crossover imgXover;

    // Maximizer
    mdsp::LookaheadLimiter limiter;

    // Bypass alignment
    std::array<std::vector<float>, 2> dryRing;
    int ringMask = 0;
    int64_t writeIndex = 0;
    int totalLatency = 0;

    // Metering
    std::array<mdsp::TruePeak, 2> tpMeter;
    mdsp::Loudness loudness;
    std::atomic<bool> resetRequested { false };
    float corrLL = 0.0f, corrRR = 0.0f, corrLR = 0.0f, corrCoef = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterProcessor)
};
