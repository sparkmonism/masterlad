#include "MasterProcessor.h"
#include "PluginEditor.h"

namespace
{
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return juce::NormalisableRange<float> (
        lo, hi,
        [] (float s, float e, float v) { return s * std::pow (e / s, v); },
        [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
        [] (float s, float e, float v) { return juce::jlimit (s, e, v); });
}

void addFloat (Layout& l, const juce::String& id, const juce::String& name,
               juce::NormalisableRange<float> range, float def, const juce::String& label = {})
{
    l.add (std::make_unique<APF> (juce::ParameterID { id, 1 }, name, range, def,
                                  juce::AudioParameterFloatAttributes().withLabel (label)));
}

void addBool (Layout& l, const juce::String& id, const juce::String& name, bool def)
{
    l.add (std::make_unique<APB> (juce::ParameterID { id, 1 }, name, def));
}

void addChoice (Layout& l, const juce::String& id, const juce::String& name,
                const juce::StringArray& items, int def)
{
    l.add (std::make_unique<APC> (juce::ParameterID { id, 1 }, name, items, def));
}

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

inline float shape (int mode, float u)
{
    switch (mode)
    {
        default:
        case 0: // Soft
            return std::tanh (u);
        case 1: // Tube: asymmetric, mostly even harmonics
            return std::tanh (u + 0.3f * u * u);
        case 2: // Tape: soft with a little 2nd
        {
            const float t = std::tanh (u);
            return t + 0.2f * t * t;
        }
        case 3: // Hard
        {
            const float a = std::abs (u), a2 = a * a, a4 = a2 * a2, a8 = a4 * a4;
            return u / std::pow (1.0f + a8, 0.125f);
        }
    }
}
} // namespace

//==============================================================================
juce::String MasterProcessor::eqId (int band, const char* s)  { return "eq" + juce::String (band + 1) + "_" + s; }
juce::String MasterProcessor::dynId (int band, const char* s) { return "dyn" + juce::String (band + 1) + "_" + s; }
juce::String MasterProcessor::imgId (int band, const char* s) { return "img" + juce::String (band + 1) + "_" + s; }

Layout MasterProcessor::createLayout()
{
    Layout l;

    addFloat (l, "in_gain", "Input", { -18.0f, 18.0f, 0.01f }, 0.0f, "dB");
    addBool (l, "bypass", "Bypass", false);
    addChoice (l, "os", "Oversampling", { "Off", "2x", "4x", "8x" }, 2);

    addBool (l, "eq_on", "EQ On", true);
    addBool (l, "dyn_on", "Dynamics On", false);
    addBool (l, "exc_on", "Exciter On", false);
    addBool (l, "img_on", "Imager On", true);
    addBool (l, "max_on", "Maximizer On", true);

    // EQ
    addChoice (l, "eq_mode", "EQ Mode", { "Stereo", "Mid", "Side" }, 0);
    const float eqDefF[kEqBands] = { 60.0f, 200.0f, 800.0f, 2500.0f, 6000.0f, 12000.0f };
    for (int b = 0; b < kEqBands; ++b)
    {
        const bool shelf = (b == 0 || b == kEqBands - 1);
        const juce::String n = "EQ " + juce::String (b + 1) + " ";
        addBool (l, eqId (b, "on"), n + "On", true);
        addFloat (l, eqId (b, "freq"), n + "Freq", logRange (20.0f, 20000.0f), eqDefF[b], "Hz");
        addFloat (l, eqId (b, "gain"), n + "Gain", { -18.0f, 18.0f, 0.01f }, 0.0f, "dB");
        addFloat (l, eqId (b, "q"), n + "Q", { 0.2f, 10.0f, 0.0f, 0.45f }, shelf ? 0.71f : 1.0f);
    }

    // Dynamics
    addFloat (l, "dyn_x1", "Dyn Crossover 1", logRange (40.0f, 400.0f), 150.0f, "Hz");
    addFloat (l, "dyn_x2", "Dyn Crossover 2", logRange (200.0f, 5000.0f), 1000.0f, "Hz");
    addFloat (l, "dyn_x3", "Dyn Crossover 3", logRange (1000.0f, 16000.0f), 5000.0f, "Hz");
    for (int b = 0; b < kDynBands; ++b)
    {
        const juce::String n = "Dyn " + juce::String (b + 1) + " ";
        addBool (l, dynId (b, "byp"), n + "Bypass", false);
        addFloat (l, dynId (b, "thr"), n + "Threshold", { -60.0f, 0.0f, 0.1f }, -18.0f, "dB");
        addFloat (l, dynId (b, "ratio"), n + "Ratio", { 1.0f, 20.0f, 0.0f, 0.5f }, 2.0f, ":1");
        addFloat (l, dynId (b, "att"), n + "Attack", logRange (0.1f, 100.0f), 20.0f, "ms");
        addFloat (l, dynId (b, "rel"), n + "Release", logRange (10.0f, 1000.0f), 150.0f, "ms");
        addFloat (l, dynId (b, "gain"), n + "Gain", { -12.0f, 12.0f, 0.01f }, 0.0f, "dB");
    }

    // Exciter
    addFloat (l, "exc_amount", "Exciter Amount", { 0.0f, 100.0f, 0.1f }, 25.0f, "%");
    addFloat (l, "exc_drive", "Exciter Drive", { 0.0f, 30.0f, 0.1f }, 12.0f, "dB");
    addFloat (l, "exc_freq", "Exciter Frequency", logRange (800.0f, 12000.0f), 3000.0f, "Hz");
    addChoice (l, "exc_mode", "Exciter Mode", { "Soft", "Tube", "Tape", "Hard" }, 0);

    // Imager
    addFloat (l, "img_x1", "Imager Crossover 1", logRange (40.0f, 400.0f), 150.0f, "Hz");
    addFloat (l, "img_x2", "Imager Crossover 2", logRange (200.0f, 5000.0f), 1000.0f, "Hz");
    addFloat (l, "img_x3", "Imager Crossover 3", logRange (1000.0f, 16000.0f), 5000.0f, "Hz");
    for (int b = 0; b < kDynBands; ++b)
        addFloat (l, imgId (b, "width"), "Imager " + juce::String (b + 1) + " Width",
                  { 0.0f, 200.0f, 0.1f }, 100.0f, "%");

    // Maximizer
    addFloat (l, "max_gain", "Maximizer Gain", { 0.0f, 20.0f, 0.01f }, 3.0f, "dB");
    addFloat (l, "max_ceiling", "Ceiling", { -6.0f, 0.0f, 0.01f }, -0.3f, "dB");
    addFloat (l, "max_release", "Release", logRange (10.0f, 1000.0f), 150.0f, "ms");
    addBool (l, "max_auto", "Auto Release", true);

    return l;
}

//==============================================================================
MasterProcessor::MasterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    auto g = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };

    ptrs.bypass = g ("bypass");
    ptrs.eqOn = g ("eq_on");   ptrs.dynOn = g ("dyn_on"); ptrs.excOn = g ("exc_on");
    ptrs.imgOn = g ("img_on"); ptrs.maxOn = g ("max_on");
    ptrs.eqMode = g ("eq_mode");
    ptrs.inGain = g ("in_gain");
    ptrs.os = g ("os");

    for (int b = 0; b < kEqBands; ++b)
    {
        ptrs.eqEnabled[b] = g (eqId (b, "on"));
        ptrs.eqF[b] = g (eqId (b, "freq"));
        ptrs.eqG[b] = g (eqId (b, "gain"));
        ptrs.eqQ[b] = g (eqId (b, "q"));
    }

    for (int i = 0; i < 3; ++i)
    {
        ptrs.dynX[i] = g ("dyn_x" + juce::String (i + 1));
        ptrs.imgX[i] = g ("img_x" + juce::String (i + 1));
    }

    for (int b = 0; b < kDynBands; ++b)
    {
        ptrs.dynByp[b] = g (dynId (b, "byp"));
        ptrs.dynThr[b] = g (dynId (b, "thr"));
        ptrs.dynRatio[b] = g (dynId (b, "ratio"));
        ptrs.dynAtt[b] = g (dynId (b, "att"));
        ptrs.dynRel[b] = g (dynId (b, "rel"));
        ptrs.dynGain[b] = g (dynId (b, "gain"));
        ptrs.imgW[b] = g (imgId (b, "width"));
    }

    ptrs.excAmount = g ("exc_amount");
    ptrs.excDrive = g ("exc_drive");
    ptrs.excFreq = g ("exc_freq");
    ptrs.excMode = g ("exc_mode");

    ptrs.maxGain = g ("max_gain");
    ptrs.maxCeil = g ("max_ceiling");
    ptrs.maxRel = g ("max_release");
    ptrs.maxAuto = g ("max_auto");
}

bool MasterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

MasterProcessor::Params MasterProcessor::readParams() const
{
    Params p;
    p.bypass = ptrs.bypass->load() > 0.5f;
    p.eqOn = ptrs.eqOn->load() > 0.5f;
    p.dynOn = ptrs.dynOn->load() > 0.5f;
    p.excOn = ptrs.excOn->load() > 0.5f;
    p.imgOn = ptrs.imgOn->load() > 0.5f;
    p.maxOn = ptrs.maxOn->load() > 0.5f;
    p.eqMode = (int) std::lround (ptrs.eqMode->load());
    p.inGainDb = ptrs.inGain->load();

    for (int b = 0; b < kEqBands; ++b)
    {
        p.eq[b].on = ptrs.eqEnabled[b]->load() > 0.5f;
        p.eq[b].f = ptrs.eqF[b]->load();
        p.eq[b].g = ptrs.eqG[b]->load();
        p.eq[b].q = ptrs.eqQ[b]->load();
    }

    for (int i = 0; i < 3; ++i)
    {
        p.dynX[i] = ptrs.dynX[i]->load();
        p.imgX[i] = ptrs.imgX[i]->load();
    }

    for (int b = 0; b < kDynBands; ++b)
    {
        p.dyn[b].byp = ptrs.dynByp[b]->load() > 0.5f;
        p.dyn[b].thr = ptrs.dynThr[b]->load();
        p.dyn[b].ratio = ptrs.dynRatio[b]->load();
        p.dyn[b].att = ptrs.dynAtt[b]->load();
        p.dyn[b].rel = ptrs.dynRel[b]->load();
        p.dyn[b].gain = ptrs.dynGain[b]->load();
        p.imgW[b] = ptrs.imgW[b]->load() * 0.01f;
    }

    p.excAmount = ptrs.excAmount->load() * 0.01f;
    p.excDrive = ptrs.excDrive->load();
    p.excFreq = ptrs.excFreq->load();
    p.excMode = (int) std::lround (ptrs.excMode->load());
    p.os = juce::jlimit (0, 3, (int) std::lround (ptrs.os->load()));

    p.maxGain = ptrs.maxGain->load();
    p.maxCeil = ptrs.maxCeil->load();
    p.maxRel = ptrs.maxRel->load();
    p.maxAuto = ptrs.maxAuto->load() > 0.5f;
    return p;
}

//==============================================================================
void MasterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = std::max (samplesPerBlock, 1);

    limiter.prepare (sr);
    loudness.prepare (sr);
    for (auto& t : tpMeter) t.reset();

    for (int k = 0; k < 3; ++k)
    {
        oversamplers[(size_t) k] = std::make_unique<juce::dsp::Oversampling<float>> (
            2, (size_t) (k + 1),
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        oversamplers[(size_t) k]->initProcessing ((size_t) kChunk);
        oversamplers[(size_t) k]->reset();
        osLatency[k + 1] = (int) std::lround (oversamplers[(size_t) k]->getLatencyInSamples());
    }
    osLatency[0] = 0;
    lastOs = -1;

    totalLatency = osLatency[3] + limiter.latency;
    setLatencySamples (totalLatency);

    for (auto& d : excDry) d.init (osLatency[3] + 2);
    for (auto& d : excWet) d.init (osLatency[3] + 2);

    int ringSize = 8192;
    while (ringSize < totalLatency + 2 * kChunk + 16)
        ringSize <<= 1;
    ringMask = ringSize - 1;
    for (auto& r : dryRing) r.assign ((size_t) ringSize, 0.0f);
    writeIndex = 0;

    for (auto& ch : eqFilt) for (auto& f : ch) f.reset();
    dynXover.reset();
    imgXover.reset();
    dynGrState.fill (0.0f);
    for (auto& f : excPreHp) f.reset();
    for (auto& f : excPostHp) f.reset();
    excDcX.fill (0.0f);
    excDcY.fill (0.0f);
    limiter.reset();

    corrLL = corrRR = corrLR = corrCoef = 0.0f;

    const auto p = readParams();
    auto init = [this] (juce::SmoothedValue<float>& s, float v, double sec = 0.02)
    {
        s.reset (sr, sec);
        s.setCurrentAndTargetValue (v);
    };
    init (inGainSm, dbToGain (p.inGainDb));
    init (bypassSm, p.bypass ? 1.0f : 0.0f);
    init (eqMix, p.eqOn ? 1.0f : 0.0f);
    init (dynMix, p.dynOn ? 1.0f : 0.0f);
    init (imgMix, p.imgOn ? 1.0f : 0.0f);
    init (maxMix, p.maxOn ? 1.0f : 0.0f);
    init (excMix, p.excOn ? 1.0f : 0.0f);
}

//==============================================================================
template <typename Fn>
void MasterProcessor::runModule (juce::SmoothedValue<float>& mix, bool on,
                                 float* l, float* r, int n, Fn&& fn)
{
    mix.setTargetValue (on ? 1.0f : 0.0f);
    const bool blend = mix.isSmoothing();

    if (! on && ! blend)
        return;

    if (blend)
    {
        std::copy (l, l + n, tmp[0].begin());
        std::copy (r, r + n, tmp[1].begin());
    }

    fn();

    if (blend)
        for (int i = 0; i < n; ++i)
        {
            const float m = mix.getNextValue();
            l[i] = tmp[0][(size_t) i] + m * (l[i] - tmp[0][(size_t) i]);
            r[i] = tmp[1][(size_t) i] + m * (r[i] - tmp[1][(size_t) i]);
        }
}

void MasterProcessor::runEq (float* l, float* r, int n, const Params& p)
{
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto c = mdsp::makeCoeffs (eqType (b), sr, p.eq[b].f, p.eq[b].g, p.eq[b].q);
        eqFilt[0][(size_t) b].set (c);
        eqFilt[1][(size_t) b].set (c);
    }

    bool active[kEqBands];
    for (int b = 0; b < kEqBands; ++b)
        active[b] = p.eq[b].on && std::abs (p.eq[b].g) > 0.005f;

    for (int i = 0; i < n; ++i)
    {
        if (p.eqMode == 0)
        {
            float a = l[i], c = r[i];
            for (int b = 0; b < kEqBands; ++b)
                if (active[b])
                {
                    a = eqFilt[0][(size_t) b].process (a);
                    c = eqFilt[1][(size_t) b].process (c);
                }
            l[i] = a;
            r[i] = c;
        }
        else
        {
            float m = (l[i] + r[i]) * 0.5f, s = (l[i] - r[i]) * 0.5f;
            float& target = (p.eqMode == 1) ? m : s;
            for (int b = 0; b < kEqBands; ++b)
                if (active[b])
                    target = eqFilt[0][(size_t) b].process (target);
            l[i] = m + s;
            r[i] = m - s;
        }
    }
}

void MasterProcessor::runDynamics (float* l, float* r, int n, const Params& p)
{
    float f1 = p.dynX[0], f2 = p.dynX[1], f3 = p.dynX[2];
    mdsp::orderCrossovers (sr, f1, f2, f3);
    dynXover.setFreqs (sr, f1, f2, f3);

    float atk[kDynBands], rel[kDynBands];
    for (int b = 0; b < kDynBands; ++b)
    {
        atk[b] = std::exp (-1.0f / (std::max (0.05f, p.dyn[b].att) * 0.001f * (float) sr));
        rel[b] = std::exp (-1.0f / (std::max (1.0f, p.dyn[b].rel) * 0.001f * (float) sr));
    }

    constexpr float knee = 6.0f;
    float grMax[kDynBands] {};

    for (int i = 0; i < n; ++i)
    {
        float oL[4], oR[4];
        dynXover.ch[0].process (l[i], oL);
        dynXover.ch[1].process (r[i], oR);

        float sumL = 0.0f, sumR = 0.0f;
        for (int b = 0; b < kDynBands; ++b)
        {
            const auto& d = p.dyn[b];
            if (d.byp)
            {
                sumL += oL[b];
                sumR += oR[b];
                continue;
            }

            const float lvlDb = mdsp::toDb (std::max (std::abs (oL[b]), std::abs (oR[b])));
            const float over = lvlDb - d.thr;
            const float slope = 1.0f - 1.0f / d.ratio;

            float target;
            if (2.0f * over < -knee)
                target = 0.0f;
            else if (2.0f * std::abs (over) <= knee)
                target = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
            else
                target = slope * over;

            float& gr = dynGrState[(size_t) b];
            const float coef = target > gr ? atk[b] : rel[b];
            gr = target + coef * (gr - target);

            const float g = std::exp (0.115129255f * (d.gain - gr));
            sumL += oL[b] * g;
            sumR += oR[b] * g;
            grMax[b] = std::max (grMax[b], gr);
        }

        l[i] = sumL;
        r[i] = sumR;
    }

    for (int b = 0; b < kDynBands; ++b)
        mdsp::atomicMax (dynGr[(size_t) b], grMax[b]);
}

void MasterProcessor::runExciter (float* l, float* r, int n, const Params& p)
{
    const int order = p.os;
    if (order != lastOs)
    {
        if (order > 0)
            oversamplers[(size_t) (order - 1)]->reset();
        lastOs = order;
    }

    const int L8 = osLatency[3];
    const int wetPad = L8 - osLatency[order];

    excMix.setTargetValue (p.excOn ? 1.0f : 0.0f);

    const auto hp = mdsp::makeCoeffs (mdsp::FilterType::HighPass, sr, p.excFreq, 0.0, mdsp::kButterQ);
    for (int c = 0; c < 2; ++c)
    {
        excPreHp[(size_t) c].set (hp);
        excPostHp[(size_t) c].set (hp);
    }

    float* ch[2] = { l, r };
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
            exBuf[(size_t) c][(size_t) i] = excPreHp[(size_t) c].process (ch[c][i]);

    const float d = dbToGain (p.excDrive);
    const float invD = 1.0f / d;
    const int mode = p.excMode;

    if (order > 0)
    {
        auto& os = *oversamplers[(size_t) (order - 1)];
        float* ptr[2] = { exBuf[0].data(), exBuf[1].data() };
        juce::dsp::AudioBlock<float> base (ptr, 2, (size_t) n);

        auto up = os.processSamplesUp (juce::dsp::AudioBlock<const float> (base));
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            float* x = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                x[i] = shape (mode, d * x[i]) * invD;
        }
        os.processSamplesDown (base);
    }
    else
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                exBuf[(size_t) c][(size_t) i] = shape (mode, d * exBuf[(size_t) c][(size_t) i]) * invD;
    }

    const float dcR = 1.0f - 6.2831853f * 20.0f / (float) sr;

    for (int i = 0; i < n; ++i)
    {
        const float amt = p.excAmount * excMix.getNextValue();

        for (int c = 0; c < 2; ++c)
        {
            const float w = exBuf[(size_t) c][(size_t) i];
            const float y = w - excDcX[(size_t) c] + dcR * excDcY[(size_t) c];
            excDcX[(size_t) c] = w;
            excDcY[(size_t) c] = y;

            float wet = excPostHp[(size_t) c].process (y);
            wet = excWet[(size_t) c].process (wet, wetPad);
            const float dry = excDry[(size_t) c].process (ch[c][i], L8);
            ch[c][i] = dry + wet * amt;
        }
    }
}

void MasterProcessor::runImager (float* l, float* r, int n, const Params& p)
{
    float f1 = p.imgX[0], f2 = p.imgX[1], f3 = p.imgX[2];
    mdsp::orderCrossovers (sr, f1, f2, f3);
    imgXover.setFreqs (sr, f1, f2, f3);

    for (int i = 0; i < n; ++i)
    {
        float oL[4], oR[4];
        imgXover.ch[0].process (l[i], oL);
        imgXover.ch[1].process (r[i], oR);

        float sumL = 0.0f, sumR = 0.0f;
        for (int b = 0; b < kDynBands; ++b)
        {
            const float m = (oL[b] + oR[b]) * 0.5f;
            const float s = (oL[b] - oR[b]) * 0.5f * p.imgW[b];
            sumL += m + s;
            sumR += m - s;
        }
        l[i] = sumL;
        r[i] = sumR;
    }
}

//==============================================================================
void MasterProcessor::processChunk (float* l, float* r, int n, const Params& p)
{
    const int64_t mask = (int64_t) ringMask;

    // input meters, dry capture for bypass, input gain
    float pl = 0.0f, pr = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        pl = std::max (pl, std::abs (l[i]));
        pr = std::max (pr, std::abs (r[i]));
        dryRing[0][(size_t) ((writeIndex + i) & mask)] = l[i];
        dryRing[1][(size_t) ((writeIndex + i) & mask)] = r[i];
    }
    mdsp::atomicMax (inPeak[0], pl);
    mdsp::atomicMax (inPeak[1], pr);

    inGainSm.setTargetValue (dbToGain (p.inGainDb));
    for (int i = 0; i < n; ++i)
    {
        const float g = inGainSm.getNextValue();
        l[i] *= g;
        r[i] *= g;
    }

    runModule (eqMix, p.eqOn, l, r, n, [&] { runEq (l, r, n, p); });
    runModule (dynMix, p.dynOn, l, r, n, [&] { runDynamics (l, r, n, p); });
    runExciter (l, r, n, p);
    runModule (imgMix, p.imgOn, l, r, n, [&] { runImager (l, r, n, p); });

    // Maximizer
    maxMix.setTargetValue (p.maxOn ? 1.0f : 0.0f);
    const float lowest = limiter.process (l, r, n, dbToGain (p.maxGain), dbToGain (p.maxCeil),
                                          sr, p.maxRel, p.maxAuto, maxMix);
    if (p.maxOn)
        mdsp::atomicMax (maxGr, -mdsp::toDb (lowest));

    // Bypass crossfade (against the latency-aligned input)
    bypassSm.setTargetValue (p.bypass ? 1.0f : 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const float m = bypassSm.getNextValue();
        const size_t idx = (size_t) ((writeIndex + i - totalLatency) & mask);
        l[i] += m * (dryRing[0][idx] - l[i]);
        r[i] += m * (dryRing[1][idx] - r[i]);
    }

    // Output metering
    float ol = 0.0f, orr = 0.0f, tpMax = 0.0f;
    const float ca = std::exp (-1.0f / (0.3f * (float) sr));
    uint32_t sw = specWrite.load (std::memory_order_relaxed);

    for (int i = 0; i < n; ++i)
    {
        ol = std::max (ol, std::abs (l[i]));
        orr = std::max (orr, std::abs (r[i]));
        tpMax = std::max (tpMax, std::max (tpMeter[0].process (l[i]), tpMeter[1].process (r[i])));
        loudness.pushSample (l[i], r[i]);

        corrLL = ca * corrLL + (1.0f - ca) * l[i] * l[i];
        corrRR = ca * corrRR + (1.0f - ca) * r[i] * r[i];
        corrLR = ca * corrLR + (1.0f - ca) * l[i] * r[i];

        specRing[(size_t) (sw & (kSpecRing - 1))].store (0.5f * (l[i] + r[i]), std::memory_order_relaxed);
        ++sw;
    }

    specWrite.store (sw, std::memory_order_release);
    mdsp::atomicMax (outPeak[0], ol);
    mdsp::atomicMax (outPeak[1], orr);
    mdsp::atomicMax (truePeakMax, mdsp::toDb (tpMax));

    momentary.store (loudness.momentary);
    shortTerm.store (loudness.shortTerm);
    integrated.store (loudness.integrated);
    correlation.store (juce::jlimit (-1.0f, 1.0f, corrLR / std::sqrt (corrLL * corrRR + 1.0e-20f)));

    writeIndex += n;
}

void MasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (buffer.getNumChannels() < 2)
        return;

    if (resetRequested.exchange (false))
        loudness.reset();

    const auto p = readParams();
    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (1);
    const int total = buffer.getNumSamples();

    for (int s = 0; s < total; s += kChunk)
        processChunk (l + s, r + s, std::min (kChunk, total - s), p);
}

//==============================================================================
juce::AudioProcessorEditor* MasterProcessor::createEditor()
{
    return new MasterEditor (*this);
}

void MasterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MasterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasterProcessor();
}
