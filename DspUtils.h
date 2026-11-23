#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

// Small DSP building blocks shared by the Masterlad processor and editor.
namespace mdsp
{
constexpr double kPi = 3.14159265358979323846;

//==============================================================================
// Filters
//==============================================================================
struct Coeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

enum class FilterType { LowShelf, Bell, HighShelf, LowPass, HighPass, AllPass };

// RBJ audio-EQ-cookbook biquads. gainDb only matters for shelves and bells.
inline Coeffs makeCoeffs (FilterType type, double fs, double f, double gainDb, double q)
{
    f = juce::jlimit (10.0, 0.49 * fs, f);
    q = std::max (q, 0.05);

    const double w0 = 2.0 * kPi * f / fs;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * q);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double tsa = 2.0 * std::sqrt (A) * alpha;

    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (type)
    {
        case FilterType::Bell:
            b0 = 1.0 + alpha * A;  b1 = -2.0 * cw;  b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;  a1 = -2.0 * cw;  a2 = 1.0 - alpha / A;
            break;
        case FilterType::LowShelf:
            b0 = A * ((A + 1) - (A - 1) * cw + tsa);
            b1 = 2.0 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - tsa);
            a0 = (A + 1) + (A - 1) * cw + tsa;
            a1 = -2.0 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - tsa;
            break;
        case FilterType::HighShelf:
            b0 = A * ((A + 1) + (A - 1) * cw + tsa);
            b1 = -2.0 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - tsa);
            a0 = (A + 1) - (A - 1) * cw + tsa;
            a1 = 2.0 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - tsa;
            break;
        case FilterType::LowPass:
            b0 = (1.0 - cw) * 0.5;  b1 = 1.0 - cw;  b2 = (1.0 - cw) * 0.5;
            a0 = 1.0 + alpha;       a1 = -2.0 * cw; a2 = 1.0 - alpha;
            break;
        case FilterType::HighPass:
            b0 = (1.0 + cw) * 0.5;  b1 = -(1.0 + cw);  b2 = (1.0 + cw) * 0.5;
            a0 = 1.0 + alpha;       a1 = -2.0 * cw;    a2 = 1.0 - alpha;
            break;
        case FilterType::AllPass:
            b0 = 1.0 - alpha;  b1 = -2.0 * cw;  b2 = 1.0 + alpha;
            a0 = 1.0 + alpha;  a1 = -2.0 * cw;  a2 = 1.0 - alpha;
            break;
    }

    Coeffs c;
    c.b0 = b0 / a0;  c.b1 = b1 / a0;  c.b2 = b2 / a0;
    c.a1 = a1 / a0;  c.a2 = a2 / a0;
    return c;
}

// Magnitude of the biquad at angular frequency w (radians per sample).
inline double magnitudeAt (const Coeffs& c, double w)
{
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    return std::abs ((c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2));
}

// Transposed direct form II with double precision state, so low-frequency
// filters stay clean.
struct Biquad
{
    Coeffs c;
    double z1 = 0.0, z2 = 0.0;

    void set (const Coeffs& n) { c = n; }
    void reset() { z1 = z2 = 0.0; }

    float process (float xf)
    {
        const double x = xf;
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }
};

// Linkwitz-Riley 4th order section: two identical Butterworth biquads.
struct Lr4
{
    Biquad a, b;

    void set (const Coeffs& c) { a.set (c); b.set (c); }
    void reset() { a.reset(); b.reset(); }
    float process (float x) { return b.process (a.process (x)); }
};

constexpr double kButterQ = 0.70710678118654752;

//==============================================================================
// Four band Linkwitz-Riley crossover. The bands sum back to an all-pass, so a
// flat setting is transparent.
//==============================================================================
struct SplitChannel
{
    Lr4 lp1, hp1, lp2, hp2, lp3, hp3;
    Biquad ap2lo, ap3lo, ap3b1;

    void reset()
    {
        lp1.reset(); hp1.reset(); lp2.reset(); hp2.reset(); lp3.reset(); hp3.reset();
        ap2lo.reset(); ap3lo.reset(); ap3b1.reset();
    }

    void process (float x, float* o)
    {
        const float low = lp1.process (x);
        const float r1  = hp1.process (x);
        const float b1  = lp2.process (r1);
        const float r2  = hp2.process (r1);

        o[0] = ap3lo.process (ap2lo.process (low));
        o[1] = ap3b1.process (b1);
        o[2] = lp3.process (r2);
        o[3] = hp3.process (r2);
    }
};

struct Crossover
{
    std::array<SplitChannel, 2> ch;

    void reset() { ch[0].reset(); ch[1].reset(); }

    void setFreqs (double fs, double f1, double f2, double f3)
    {
        const auto lp1 = makeCoeffs (FilterType::LowPass,  fs, f1, 0.0, kButterQ);
        const auto hp1 = makeCoeffs (FilterType::HighPass, fs, f1, 0.0, kButterQ);
        const auto lp2 = makeCoeffs (FilterType::LowPass,  fs, f2, 0.0, kButterQ);
        const auto hp2 = makeCoeffs (FilterType::HighPass, fs, f2, 0.0, kButterQ);
        const auto lp3 = makeCoeffs (FilterType::LowPass,  fs, f3, 0.0, kButterQ);
        const auto hp3 = makeCoeffs (FilterType::HighPass, fs, f3, 0.0, kButterQ);
        const auto ap2 = makeCoeffs (FilterType::AllPass,  fs, f2, 0.0, kButterQ);
        const auto ap3 = makeCoeffs (FilterType::AllPass,  fs, f3, 0.0, kButterQ);

        for (auto& c : ch)
        {
            c.lp1.set (lp1); c.hp1.set (hp1);
            c.lp2.set (lp2); c.hp2.set (hp2);
            c.lp3.set (lp3); c.hp3.set (hp3);
            c.ap2lo.set (ap2); c.ap3lo.set (ap3); c.ap3b1.set (ap3);
        }
    }
};

// Keeps three crossover frequencies ordered and inside the audible range.
inline void orderCrossovers (double fs, float& f1, float& f2, float& f3)
{
    const float top = (float) (0.45 * fs);
    f1 = juce::jlimit (20.0f, top / 1.25f / 1.25f, f1);
    f2 = juce::jlimit (f1 * 1.25f, top / 1.25f, f2);
    f3 = juce::jlimit (f2 * 1.25f, top, f3);
}

//==============================================================================
// Simple delay line (power-of-two ring).
//==============================================================================
struct Delay
{
    std::vector<float> buf;
    int mask = 0, w = 0;

    void init (int maxDelay)
    {
        int size = 16;
        while (size < maxDelay + 2)
            size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        w = 0;
    }

    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }

    float process (float x, int d)
    {
        buf[(size_t) w] = x;
        const float y = buf[(size_t) ((w - d) & mask)];
        w = (w + 1) & mask;
        return y;
    }
};

//==============================================================================
// Inter-sample ("true") peak estimate: 4x polyphase windowed-sinc interpolation.
// process() returns the largest |value| around the sample that is kDelay samples old.
//==============================================================================
struct TruePeak
{
    static constexpr int kTaps = 32;
    static constexpr int kCentre = kTaps / 2 - 1; // index of the sample the interpolation starts from
    static constexpr int kDelay = kTaps / 2;

    using Table = std::array<std::array<float, kTaps>, 3>;

    static const Table& table()
    {
        static const Table t = []
        {
            Table out {};
            for (int p = 0; p < 3; ++p)
            {
                const double frac = (p + 1) / 4.0;
                double sum = 0.0;
                std::array<double, kTaps> h {};
                for (int k = 0; k < kTaps; ++k)
                {
                    const double x = (k - kCentre) - frac;
                    const double s = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (kPi * x) / (kPi * x);
                    const double win = 0.42 + 0.5 * std::cos (kPi * x / (kTaps / 2.0)) + 0.08 * std::cos (2.0 * kPi * x / (kTaps / 2.0));
                    h[(size_t) k] = s * win;
                    sum += h[(size_t) k];
                }
                for (int k = 0; k < kTaps; ++k)
                    out[(size_t) p][(size_t) k] = (float) (h[(size_t) k] / sum);
            }
            return out;
        }();
        return t;
    }

    std::array<float, 2 * kTaps> buf {};
    int pos = 0;

    void reset() { buf.fill (0.0f); pos = 0; }

    float process (float x)
    {
        const auto& t = table();
        buf[(size_t) pos] = x;
        buf[(size_t) (pos + kTaps)] = x;

        const float* w = buf.data() + pos + 1; // oldest .. newest
        float peak = std::abs (w[kCentre]);

        for (int p = 0; p < 3; ++p)
        {
            float acc = 0.0f;
            for (int k = 0; k < kTaps; ++k)
                acc += t[(size_t) p][(size_t) k] * w[k];
            peak = std::max (peak, std::abs (acc));
        }

        pos = (pos + 1) & (kTaps - 1);
        return peak;
    }
};

//==============================================================================
// Look-ahead true-peak limiter (stereo linked). Fixed latency = A - 1 + 8.
//==============================================================================
struct LookaheadLimiter
{
    int A = 96;
    int latency = 0;

    std::array<Delay, 2> delay;
    std::array<TruePeak, 2> tp;
    std::vector<float> minRing, avgRing;
    int mpos = 0, apos = 0;
    double avgSum = 0.0;
    float gSmooth = 1.0f, prevPk = 0.0f;

    void prepare (double fs)
    {
        A = std::max (8, (int) std::lround (0.002 * fs));
        latency = A - 1 + TruePeak::kDelay;
        delay[0].init (latency + 2);
        delay[1].init (latency + 2);
        minRing.assign ((size_t) A, 1.0f);
        avgRing.assign ((size_t) A, 1.0f);
        reset();
    }

    void reset()
    {
        delay[0].reset(); delay[1].reset();
        tp[0].reset(); tp[1].reset();
        std::fill (minRing.begin(), minRing.end(), 1.0f);
        std::fill (avgRing.begin(), avgRing.end(), 1.0f);
        mpos = apos = 0;
        avgSum = (double) A;
        gSmooth = 1.0f;
        prevPk = 0.0f;
    }

    // Returns the lowest gain applied in the block (1 = no limiting).
    float process (float* l, float* r, int n, float drive, float ceiling, double fs,
                   float releaseMs, bool autoRelease, juce::SmoothedValue<float>& mix)
    {
        const float relBase = std::exp (-1.0f / (std::max (1.0f, releaseMs) * 0.001f * (float) fs));
        float lowest = 1.0f;
        const float target = ceiling * 0.983f; // 0.15 dB margin for inter-sample estimation error

        for (int i = 0; i < n; ++i)
        {
            const float inL = l[i] * drive, inR = r[i] * drive;

            float pk = std::max (tp[0].process (inL), tp[1].process (inR));
            const float pkNow = pk;
            pk = std::max (pk, prevPk);
            prevPk = pkNow;

            const float gr = pk > target ? target / pk : 1.0f;

            minRing[(size_t) mpos] = gr;
            if (++mpos >= A) mpos = 0;
            float minf = 1.0f;
            for (int k = 0; k < A; ++k)
                minf = std::min (minf, minRing[(size_t) k]);

            avgSum += (double) minf - (double) avgRing[(size_t) apos];
            avgRing[(size_t) apos] = minf;
            if (++apos >= A) apos = 0;
            const float s = (float) (avgSum / (double) A);

            if (s <= gSmooth)
                gSmooth = s;
            else
            {
                float coef = relBase;
                if (autoRelease)
                {
                    const float depth = juce::jlimit (0.0f, 1.0f, (1.0f - gSmooth) * 2.0f);
                    const float factor = 0.25f + 0.75f * depth;
                    coef = std::exp (-1.0f / (std::max (1.0f, releaseMs * factor) * 0.001f * (float) fs));
                }
                gSmooth = s + (gSmooth - s) * coef;
            }
            lowest = std::min (lowest, gSmooth);

            const float dl = delay[0].process (l[i], latency);
            const float dr = delay[1].process (r[i], latency);

            const float outL = juce::jlimit (-ceiling, ceiling, dl * drive * gSmooth);
            const float outR = juce::jlimit (-ceiling, ceiling, dr * drive * gSmooth);

            const float m = mix.getNextValue();
            l[i] = dl + m * (outL - dl);
            r[i] = dr + m * (outR - dr);
        }

        return lowest;
    }
};

//==============================================================================
// ITU-R BS.1770-4 / EBU R128 loudness (momentary, short-term, integrated).
//==============================================================================
struct Loudness
{
    std::array<Biquad, 2> shelf, hpf; // per channel

    int subLen = 4800, subCount = 0;
    double acc = 0.0;

    std::array<double, 30> ring {};
    int ringPos = 0, ringFill = 0;

    static constexpr int kBins = 1001; // -70 .. +30 LUFS in 0.1 steps
    std::array<double, kBins> histE {};
    std::array<double, kBins> histN {};

    float momentary = -120.0f, shortTerm = -120.0f, integrated = -120.0f;

    void prepare (double fs)
    {
        subLen = (int) std::lround (fs * 0.1);

        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            Coeffs c;
            c.b0 = (Vh + Vb * K / Q + K * K) / a0;
            c.b1 = 2.0 * (K * K - Vh) / a0;
            c.b2 = (Vh - Vb * K / Q + K * K) / a0;
            c.a1 = 2.0 * (K * K - 1.0) / a0;
            c.a2 = (1.0 - K / Q + K * K) / a0;
            shelf[0].set (c); shelf[1].set (c);
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (kPi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            Coeffs c;
            c.b0 = 1.0; c.b1 = -2.0; c.b2 = 1.0;
            c.a1 = 2.0 * (K * K - 1.0) / a0;
            c.a2 = (1.0 - K / Q + K * K) / a0;
            hpf[0].set (c); hpf[1].set (c);
        }
        reset();
    }

    void reset()
    {
        for (auto& b : shelf) b.reset();
        for (auto& b : hpf) b.reset();
        acc = 0.0;
        subCount = 0;
        ring.fill (0.0);
        ringPos = ringFill = 0;
        histE.fill (0.0);
        histN.fill (0.0);
        momentary = shortTerm = integrated = -120.0f;
    }

    static double toLufs (double z) { return z > 1.0e-12 ? -0.691 + 10.0 * std::log10 (z) : -120.0; }

    void pushSample (float l, float r)
    {
        const float kl = hpf[0].process (shelf[0].process (l));
        const float kr = hpf[1].process (shelf[1].process (r));
        acc += (double) kl * kl + (double) kr * kr;

        if (++subCount < subLen)
            return;

        ring[(size_t) ringPos] = acc / (double) subLen;
        ringPos = (ringPos + 1) % 30;
        ringFill = std::min (ringFill + 1, 30);
        acc = 0.0;
        subCount = 0;

        auto meanOfLast = [this] (int count)
        {
            count = std::min (count, ringFill);
            double s = 0.0;
            for (int i = 1; i <= count; ++i)
                s += ring[(size_t) ((ringPos - i + 60) % 30)];
            return count > 0 ? s / count : 0.0;
        };

        const double z400 = meanOfLast (4);
        momentary = (float) toLufs (z400);
        shortTerm = (float) toLufs (meanOfLast (30));

        if (ringFill >= 4)
        {
            const double l400 = toLufs (z400);
            if (l400 > -70.0)
            {
                const int idx = juce::jlimit (0, kBins - 1, (int) std::floor ((l400 + 70.0) * 10.0));
                histE[(size_t) idx] += z400;
                histN[(size_t) idx] += 1.0;
            }

            double sumE = 0.0, sumN = 0.0;
            for (int i = 0; i < kBins; ++i) { sumE += histE[(size_t) i]; sumN += histN[(size_t) i]; }

            if (sumN > 0.0)
            {
                const double thr = toLufs (sumE / sumN) - 10.0;
                const int start = juce::jlimit (0, kBins, (int) std::ceil ((thr + 70.0) * 10.0));
                double e = 0.0, nn = 0.0;
                for (int i = start; i < kBins; ++i) { e += histE[(size_t) i]; nn += histN[(size_t) i]; }
                integrated = nn > 0.0 ? (float) toLufs (e / nn) : -120.0f;
            }
        }
    }
};

//==============================================================================
inline void atomicMax (std::atomic<float>& a, float v)
{
    float cur = a.load (std::memory_order_relaxed);
    while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
}

inline float toDb (float lin) { return 20.0f * std::log10 (std::max (lin, 1.0e-6f)); }

} // namespace mdsp
