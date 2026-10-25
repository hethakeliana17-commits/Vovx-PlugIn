// VocalEngine.h
// Real-time vocal pitch correction + vocal preamp. NO dependency on JUCE.
//
// Signal path (per sample):
//   input gain -> high-pass -> [pitch detector (YIN)] -> pitch-synchronous shifter
//   -> tube-style drive -> 3-band EQ (body / presence / air) -> compressor -> output gain
//
// How the pitch correction works
//   1. The detector estimates the singer's pitch every ~5 ms (YIN algorithm on a down-sampled copy).
//   2. The pitch is snapped to the nearest note of the chosen key and scale.
//   3. The difference (in semitones) is smoothed by the "retune speed" and turned into a pitch ratio.
//   4. A pitch-synchronous two-tap delay-line shifter applies the ratio. The two taps are always exactly one
//      pitch period apart, so the crossfade between them is phase-coherent (no flanging) on voiced sound.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace vocal
{
constexpr float kPi = 3.14159265358979f;
constexpr int   kMaxChannels = 2;

// ---------------------------------------------------------------------------------------------
struct Params
{
    // Pitch correction
    bool  correct   = true;
    int   key       = 0;          // 0 = C ... 11 = B
    int   scale     = 1;          // 0 Chromatic, 1 Major, 2 Minor, 3 Major Pent, 4 Minor Pent, 5 Dorian
    float retuneMs  = 20.0f;      // 0 = instant "hard tune" ... 400 = very gentle
    float amount    = 1.0f;       // 0..1
    float tuningHz  = 440.0f;     // pitch of A4
    int   range     = 1;          // 0 Low (55-500 Hz), 1 Normal (75-800 Hz), 2 High (120-1100 Hz)

    // Preamp
    float inGainDb  = 0.0f;
    float hpfHz     = 80.0f;      // <= 21 means off
    float drive     = 0.2f;       // 0..1
    float bodyDb    = 0.0f;       // low shelf @ 120 Hz
    float presenceDb = 0.0f;      // peak @ 3.5 kHz
    float airDb     = 0.0f;       // high shelf @ 10 kHz
    float comp      = 0.0f;       // 0..1 (0 = off)
    float outGainDb = 0.0f;
};

inline unsigned scaleMask (int scale)
{
    auto m = [] (std::initializer_list<int> notes) { unsigned r = 0; for (int n : notes) r |= 1u << n; return r; };
    switch (scale)
    {
        case 0:  return 0xFFFu;
        case 1:  return m ({ 0, 2, 4, 5, 7, 9, 11 });
        case 2:  return m ({ 0, 2, 3, 5, 7, 8, 10 });
        case 3:  return m ({ 0, 2, 4, 7, 9 });
        case 4:  return m ({ 0, 3, 5, 7, 10 });
        default: return m ({ 0, 2, 3, 5, 7, 9, 10 });
    }
}

// ---------------------------------------------------------------------------------------------
class Biquad
{
public:
    void reset() { for (int c = 0; c < kMaxChannels; ++c) z1[c] = z2[c] = 0.0f; }

    float process (float x, int ch)
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }

    void highpass (double sr, float f, float q)
    {
        f = clampFreq (sr, f);
        const float w = 2.0f * kPi * f / (float) sr, cw = std::cos (w), alpha = std::sin (w) / (2.0f * q);
        set ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }

    void peaking (double sr, float f, float q, float dB)
    {
        f = clampFreq (sr, f);
        const float A = std::pow (10.0f, dB / 40.0f);
        const float w = 2.0f * kPi * f / (float) sr, cw = std::cos (w), alpha = std::sin (w) / (2.0f * q);
        set (1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
    }

    void lowShelf (double sr, float f, float dB)
    {
        f = clampFreq (sr, f);
        const float A = std::pow (10.0f, dB / 40.0f), sA = std::sqrt (A);
        const float w = 2.0f * kPi * f / (float) sr, cw = std::cos (w), alpha = std::sin (w) / 2.0f * std::sqrt (2.0f);
        set (A * ((A + 1) - (A - 1) * cw + 2 * sA * alpha), 2 * A * ((A - 1) - (A + 1) * cw),
             A * ((A + 1) - (A - 1) * cw - 2 * sA * alpha), (A + 1) + (A - 1) * cw + 2 * sA * alpha,
             -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - 2 * sA * alpha);
    }

    void highShelf (double sr, float f, float dB)
    {
        f = clampFreq (sr, f);
        const float A = std::pow (10.0f, dB / 40.0f), sA = std::sqrt (A);
        const float w = 2.0f * kPi * f / (float) sr, cw = std::cos (w), alpha = std::sin (w) / 2.0f * std::sqrt (2.0f);
        set (A * ((A + 1) + (A - 1) * cw + 2 * sA * alpha), -2 * A * ((A - 1) + (A + 1) * cw),
             A * ((A + 1) + (A - 1) * cw - 2 * sA * alpha), (A + 1) - (A - 1) * cw + 2 * sA * alpha,
             2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - 2 * sA * alpha);
    }

private:
    static float clampFreq (double sr, float f) { return std::min (std::max (f, 10.0f), 0.45f * (float) sr); }

    void set (float B0, float B1, float B2, float A0, float A1, float A2)
    {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }

    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[kMaxChannels] = { 0, 0 }, z2[kMaxChannels] = { 0, 0 };
};

// ---------------------------------------------------------------------------------------------
// YIN pitch detector running on a copy of the signal down-sampled to about 12 kHz.
class PitchDetector
{
public:
    struct Result { bool voiced = false; float f0 = 0.0f; float confidence = 0.0f; };

    void prepare (double sr)
    {
        decim   = std::max (1, (int) std::lround (sr / 12000.0));
        fs      = (float) (sr / decim);
        winLen  = (int) (0.022f * fs);
        tauCap  = (int) (fs / 50.0f) + 2;
        ringSize = winLen + tauCap + 4;
        hop     = std::max (16, (int) (0.005f * fs));
        ring.assign ((size_t) ringSize, 0.0f);
        lin.assign ((size_t) ringSize, 0.0f);
        diff.assign ((size_t) tauCap + 2, 0.0f);
        cmnd.assign ((size_t) tauCap + 2, 1.0f);
        setRange (1);
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        wr = 0; acc = 0.0f; cnt = 0; sinceHop = 0;
    }

    void setRange (int r)
    {
        static const float lo[3] = { 55.0f, 75.0f, 120.0f };
        static const float hi[3] = { 500.0f, 800.0f, 1100.0f };
        r = std::min (std::max (r, 0), 2);
        fmin = lo[r]; fmax = hi[r];
    }

    // Feed one sample. Returns true when a new estimate was written to 'out' (about every 5 ms).
    bool push (float x, Result& out)
    {
        acc += x;
        if (++cnt < decim) return false;
        ring[(size_t) wr] = acc / (float) decim;
        wr = (wr + 1) % ringSize;
        acc = 0.0f; cnt = 0;
        if (++sinceHop < hop) return false;
        sinceHop = 0;
        analyse (out);
        return true;
    }

private:
    void analyse (Result& out)
    {
        out = Result();
        for (int k = 0; k < ringSize; ++k) lin[(size_t) k] = ring[(size_t) ((wr + k) % ringSize)];

        const int tauMax = std::min ((int) (fs / fmin), tauCap);
        const int tauMin = std::max (2, (int) (fs / fmax));
        const int s0 = ringSize - (winLen + tauMax);
        const float* x = lin.data() + s0;

        float energy = 0.0f;
        for (int j = 0; j < winLen; ++j) energy += x[tauMax + j] * x[tauMax + j];
        if (std::sqrt (energy / (float) winLen) < 0.003f) return;     // too quiet (below about -50 dB)

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float s = 0.0f;
            for (int j = 0; j < winLen; ++j) { const float d = x[j] - x[j + tau]; s += d * d; }
            diff[(size_t) tau] = s;
        }

        float running = 0.0f;
        cmnd[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += diff[(size_t) tau];
            cmnd[(size_t) tau] = running > 1.0e-12f ? diff[(size_t) tau] * (float) tau / running : 1.0f;
        }

        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < 0.2f)
            {
                while (tau + 1 < tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            float m = 1.0e9f;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (cmnd[(size_t) tau] < m) { m = cmnd[(size_t) tau]; best = tau; }
            if (best < 0 || m > 0.3f) return;
        }

        float tauF = (float) best;
        if (best > tauMin && best < tauMax)
        {
            const float a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
            const float den = a - 2.0f * b + c;
            if (std::fabs (den) > 1.0e-9f) tauF += 0.5f * (a - c) / den;
        }
        if (tauF < 1.0f) return;

        out.voiced = true;
        out.f0 = fs / tauF;
        out.confidence = 1.0f - cmnd[(size_t) best];
    }

    int decim = 4, winLen = 360, tauCap = 242, ringSize = 610, hop = 60, wr = 0, cnt = 0, sinceHop = 0;
    float fs = 12000.0f, fmin = 75.0f, fmax = 800.0f, acc = 0.0f;
    std::vector<float> ring, lin, diff, cmnd;
};

// ---------------------------------------------------------------------------------------------
// Pitch-synchronous shifter. Two read taps exactly one period apart, crossfaded with a raised-cosine window.
class PitchShifter
{
public:
    void prepare (double sr, int channels)
    {
        sampleRate = sr;
        numCh = std::min (channels, kMaxChannels);
        minDelay = (float) (0.022 * sr);
        const int need = (int) (minDelay + 2.0 * sr / 50.0 + 64.0);
        int size = 1024;
        while (size < need) size <<= 1;
        mask = size - 1;
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        wi = 0; d = 0.0f;
    }

    float getMinDelay() const { return minDelay; }
    void reset() { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f); d = 0.0f; }

    // in/out: one value per channel. ratio: pitch ratio (1 = unchanged). period: pitch period in samples.
    void process (const float* in, float* out, float ratio, float period, bool settle)
    {
        for (int c = 0; c < numCh; ++c) buf[(size_t) c][(size_t) wi] = in[c];

        d += 1.0f - ratio;
        if (settle) d -= d * 0.002f;          // glide back to the "no shift" position when there is nothing to track
        while (d >= period) d -= period;
        while (d < 0.0f)    d += period;

        const float u  = d / period;
        const float wB = 0.5f * (1.0f - std::cos (kPi * u));
        const float wA = 1.0f - wB;

        for (int c = 0; c < numCh; ++c)
            out[c] = wA * read (buf[(size_t) c], minDelay + d) + wB * read (buf[(size_t) c], minDelay + d + period);

        wi = (wi + 1) & mask;
    }

private:
    float read (const std::vector<float>& b, float delay) const
    {
        const int i = (int) delay;
        const float f = delay - (float) i;
        const float ym1 = b[(size_t) ((wi - (i - 1)) & mask)];
        const float y0  = b[(size_t) ((wi - i) & mask)];
        const float y1  = b[(size_t) ((wi - (i + 1)) & mask)];
        const float y2  = b[(size_t) ((wi - (i + 2)) & mask)];
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }

    double sampleRate = 48000.0;
    int numCh = 2, mask = 4095, wi = 0;
    float minDelay = 1056.0f, d = 0.0f;
    std::array<std::vector<float>, kMaxChannels> buf;
};

// ---------------------------------------------------------------------------------------------
class Engine
{
public:
    void prepare (double sr, int channels)
    {
        sampleRate = sr;
        numCh = std::max (1, std::min (channels, kMaxChannels));
        detector.prepare (sr);
        shifter.prepare (sr, numCh);
        smoothCoef = (float) (1.0 - std::exp (-1.0 / (0.005 * sr)));
        compAtk = (float) (1.0 - std::exp (-1.0 / (0.005 * sr)));
        compRel = (float) (1.0 - std::exp (-1.0 / (0.120 * sr)));
        periodSmooth = (float) (1.0 - std::exp (-1.0 / (0.020 * sr)));
        period = (float) (sr / 150.0);
        reset();
        filtersValid = false;
        setParams (params);
    }

    void reset()
    {
        detector.reset(); shifter.reset();
        hpf.reset(); body.reset(); presence.reset(); air.reset();
        corrTarget = corrSmooth = 0.0f;
        holdHops = 0; histCount = 0; havePrev = false;
        compEnv = 0.0f; compGr = 0.0f;
        uiDetected = -1.0f; uiTarget = -1.0f;
        inGainSm = std::pow (10.0f, params.inGainDb / 20.0f);
        outGainSm = std::pow (10.0f, params.outGainDb / 20.0f);
    }

    void setParams (const Params& p)
    {
        params = p;
        detector.setRange (p.range);
        maskBits = scaleMask (p.scale);

        // Rebuild filters only when something changed (keeps the audio thread cheap).
        if (! filtersValid || p.hpfHz != lastHpf || p.bodyDb != lastBody || p.presenceDb != lastPresence || p.airDb != lastAir)
        {
            filtersValid = true;
            lastHpf = p.hpfHz; lastBody = p.bodyDb; lastPresence = p.presenceDb; lastAir = p.airDb;
            hpfOn = p.hpfHz > 21.0f;
            hpf.highpass (sampleRate, p.hpfHz, 0.7071f);
            body.lowShelf (sampleRate, 120.0f, p.bodyDb);
            presence.peaking (sampleRate, 3500.0f, 0.8f, p.presenceDb);
            air.highShelf (sampleRate, 10000.0f, p.airDb);
        }

        retuneCoef = p.retuneMs <= 0.5f ? 1.0f : (float) (1.0 - std::exp (-1.0 / (p.retuneMs * 0.001 * sampleRate)));
        compThreshold = -8.0f - p.comp * 22.0f;
        compRatio = 2.0f + p.comp * 4.0f;
        compMakeup = p.comp > 0.0f ? -compThreshold * (1.0f - 1.0f / compRatio) * 0.4f : 0.0f;
    }

    // Total delay the plugin adds, so a host can compensate.
    int latencySamples() const { return (int) (shifter.getMinDelay() + 0.006 * sampleRate); }

    float detectedMidi() const { return uiDetected; }    // -1 when no pitch is found
    float targetMidi() const   { return uiTarget; }

    void process (float* const* channels, int nch, int numSamples)
    {
        nch = std::max (1, std::min (nch, numCh));
        const float inTarget  = std::pow (10.0f, params.inGainDb / 20.0f);
        const float outTarget = std::pow (10.0f, params.outGainDb / 20.0f);

        for (int i = 0; i < numSamples; ++i)
        {
            inGainSm  += (inTarget  - inGainSm)  * smoothCoef;
            outGainSm += (outTarget - outGainSm) * smoothCoef;

            float x[kMaxChannels] = { 0, 0 };
            float mono = 0.0f;
            for (int c = 0; c < nch; ++c)
            {
                float s = channels[c][i] * inGainSm;
                if (hpfOn) s = hpf.process (s, c);
                x[c] = s;
                mono += s;
            }
            mono /= (float) nch;

            PitchDetector::Result est;
            if (detector.push (mono, est)) handleEstimate (est);

            const float coef = (params.correct && holdHops > 0) ? retuneCoef : std::max (retuneCoef, 0.002f * 4.0f);
            corrSmooth += (corrTarget - corrSmooth) * coef;
            const float ratio = std::min (std::max (std::exp2 (corrSmooth / 12.0f), 0.5f), 2.0f);
            const bool settle = ! params.correct || holdHops <= 0;

            float y[kMaxChannels] = { 0, 0 };
            shifter.process (x, y, ratio, period, settle);

            // Compressor level detector (peak over channels, measured after the shifter)
            float peak = 0.0f;
            for (int c = 0; c < nch; ++c) peak = std::max (peak, std::fabs (y[c]));
            compEnv += (peak - compEnv) * (peak > compEnv ? compAtk : compRel);
            float gainDb = 0.0f;
            if (params.comp > 0.0f)
            {
                const float levelDb = 20.0f * std::log10 (compEnv + 1.0e-9f);
                const float over = levelDb - compThreshold;
                const float targetGr = over > 0.0f ? -over * (1.0f - 1.0f / compRatio) : 0.0f;
                compGr += (targetGr - compGr) * (targetGr < compGr ? compAtk : compRel);
                gainDb = compGr + compMakeup;
            }
            const float compGain = std::pow (10.0f, gainDb / 20.0f);

            for (int c = 0; c < nch; ++c)
            {
                float s = y[c];
                if (params.drive > 0.001f) s = saturate (s);
                s = body.process (s, c);
                s = presence.process (s, c);
                s = air.process (s, c);
                s *= compGain * outGainSm;
                if (! std::isfinite (s)) s = 0.0f;
                channels[c][i] = s;
            }
        }
    }

private:
    float saturate (float s) const
    {
        const float g = 1.0f + params.drive * 4.0f;
        const float bias = 0.12f * params.drive;
        const float t = std::tanh (bias);
        const float unity = g * (1.0f - t * t);
        return (std::tanh (g * s + bias) - t) / unity;
    }

    bool isAllowed (int note) const
    {
        const int pc = (((note - params.key) % 12) + 12) % 12;
        return ((maskBits >> pc) & 1u) != 0u;
    }

    int pickTarget (float m)
    {
        const int base = (int) std::floor (m);
        int best = base; float bestDist = 1.0e9f;
        for (int n = base - 6; n <= base + 7; ++n)
        {
            if (! isAllowed (n)) continue;
            const float dist = std::fabs ((float) n - m);
            if (dist < bestDist) { bestDist = dist; best = n; }
        }
        // Hysteresis: stay on the previous note unless another note is clearly closer.
        if (havePrev && isAllowed (prevTarget) && std::fabs (m - (float) prevTarget) < bestDist + 0.15f)
            best = prevTarget;
        prevTarget = best; havePrev = true;
        return best;
    }

    void handleEstimate (const PitchDetector::Result& est)
    {
        if (est.voiced && est.confidence > 0.7f)
        {
            float m = 69.0f + 12.0f * std::log2 (est.f0 / params.tuningHz);
            hist[(size_t) (histPos % 3)] = m; ++histPos; histCount = std::min (histCount + 1, 3);
            if (histCount == 3)
            {
                const float a = hist[0], b = hist[1], c = hist[2];
                m = std::max (std::min (a, b), std::min (std::max (a, b), c));    // median of three
            }

            const float f0m = params.tuningHz * std::exp2 ((m - 69.0f) / 12.0f);
            const float targetPeriod = std::min (std::max ((float) (sampleRate / f0m), (float) (sampleRate / 1200.0)),
                                                 (float) (sampleRate / 50.0));
            period += (targetPeriod - period) * std::min (1.0f, periodSmooth * 256.0f);   // ~5 ms steps

            const int target = pickTarget (m);
            corrTarget = params.correct ? ((float) target - m) * params.amount : 0.0f;
            holdHops = 14;                                  // keep correcting for ~70 ms if voicing flickers
            uiDetected = m; uiTarget = (float) target;
        }
        else if (holdHops > 0)
        {
            --holdHops;
        }
        else
        {
            corrTarget = 0.0f;
            histCount = 0; havePrev = false;
            uiDetected = -1.0f; uiTarget = -1.0f;
        }
    }

    Params params;
    double sampleRate = 48000.0;
    int numCh = 2;

    PitchDetector detector;
    PitchShifter shifter;
    Biquad hpf, body, presence, air;
    bool hpfOn = true, filtersValid = false;
    float lastHpf = 0.0f, lastBody = 0.0f, lastPresence = 0.0f, lastAir = 0.0f;

    unsigned maskBits = 0xAB5u;
    int prevTarget = 60; bool havePrev = false;
    std::array<float, 3> hist { { 0, 0, 0 } };
    int histPos = 0, histCount = 0, holdHops = 0;

    float corrTarget = 0.0f, corrSmooth = 0.0f, retuneCoef = 0.01f;
    float period = 320.0f, periodSmooth = 0.001f;
    float smoothCoef = 0.01f, inGainSm = 1.0f, outGainSm = 1.0f;

    float compEnv = 0.0f, compGr = 0.0f, compAtk = 0.01f, compRel = 0.001f;
    float compThreshold = -8.0f, compRatio = 2.0f, compMakeup = 0.0f;

    float uiDetected = -1.0f, uiTarget = -1.0f;
};

} // namespace vocal
