// Standalone test for the vocal engine (no JUCE needed).
//   g++ -O2 -std=c++17 -I . render_test.cpp -o render_test && ./render_test
// Generates synthetic "singing" (harmonic tones with vibrato, off-key notes), runs it through the engine,
// measures the result with an independent pitch estimator, and writes demo_before.wav / demo_after.wav.
#include "VocalEngine.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>

using namespace vocal;
using Buf = std::vector<float>;

static const double kTwoPi = 6.283185307179586;

// ---------- signal helpers ----------
static Buf makeVoice (double sr, double seconds, const std::function<double (double)>& f0Of, float amp = 0.4f, float noise = 0.002f)
{
    Buf x ((size_t) (seconds * sr));
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
    double phase = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / sr, f = f0Of (t);
        phase += kTwoPi * f / sr;
        double s = 0.0;
        for (int h = 1; h <= 30 && h * f < 5000.0; ++h) s += std::sin (h * phase) / (double) h;
        const float fade = (float) std::min (1.0, t / 0.01) * (float) std::min (1.0, (seconds - t) / 0.01);
        x[i] = (float) (s * 0.55) * amp * fade + noise * uni (rng);
    }
    return x;
}

static double centsToRatio (double c) { return std::pow (2.0, c / 1200.0); }
static double hzToMidi (double f) { return 69.0 + 12.0 * std::log2 (f / 440.0); }

static Buf run (const Buf& in, double sr, const Params& p, int channels = 1, Engine* keep = nullptr)
{
    Engine localEngine;
    Engine& e = keep ? *keep : localEngine;
    e.prepare (sr, channels);
    e.setParams (p);
    e.reset();
    Buf out = in;
    Buf r = in;                                   // second channel (copy) when stereo
    const size_t block = 512;
    for (size_t pos = 0; pos < in.size(); pos += block)
    {
        const int n = (int) std::min (block, in.size() - pos);
        float* ch[2] = { out.data() + pos, r.data() + pos };
        e.process (ch, channels, n);
    }
    return out;
}

// Independent pitch estimator: normalized autocorrelation, first strong peak.
static double measureF0 (const float* x, int n, double sr, double fmin, double fmax)
{
    double mean = 0.0; for (int i = 0; i < n; ++i) mean += x[i]; mean /= n;
    const int t0 = (int) (sr / fmax), t1 = (int) (sr / fmin);
    std::vector<double> r ((size_t) t1 + 2, 0.0);
    double r0 = 0.0; for (int i = 0; i < n; ++i) r0 += (x[i] - mean) * (x[i] - mean);
    if (r0 < 1e-9) return 0.0;
    double best = -1.0;
    for (int t = t0; t <= t1; ++t)
    {
        double s = 0.0; for (int i = 0; i + t < n; ++i) s += (x[i] - mean) * (x[i + t] - mean);
        r[(size_t) t] = s / r0; best = std::max (best, r[(size_t) t]);
    }
    if (best < 0.5) return 0.0;
    int bt = t0;
    for (int t = t0 + 1; t < t1; ++t)
        if (r[(size_t) t] >= 0.9 * best && r[(size_t) t] >= r[(size_t) t - 1] && r[(size_t) t] >= r[(size_t) t + 1]) { bt = t; break; }
    double tf = bt;
    const double a = r[(size_t) bt - 1], b = r[(size_t) bt], c = r[(size_t) bt + 1], den = a - 2 * b + c;
    if (std::fabs (den) > 1e-12) tf += 0.5 * (a - c) / den;
    return sr / tf;
}

static double rms (const Buf& x, size_t a, size_t b)
{
    double s = 0.0; for (size_t i = a; i < b && i < x.size(); ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) (b - a));
}

// Share of the signal's energy that sits at harmonics of f0 (1.0 = perfectly periodic, clean tone).
static double harmonicPurity (const float* x, int N, double sr, double f0)
{
    std::vector<double> w ((size_t) N);
    double tot = 0.0;
    std::vector<double> xs ((size_t) N);
    for (int i = 0; i < N; ++i) { w[(size_t) i] = 0.5 - 0.5 * std::cos (kTwoPi * i / N); xs[(size_t) i] = w[(size_t) i] * x[i]; tot += xs[(size_t) i] * xs[(size_t) i]; }
    double harm = 0.0;
    for (int h = 1; h * f0 < 6000.0; ++h)
    {
        const int kc = (int) std::lround (h * f0 * N / sr);
        for (int k = kc - 3; k <= kc + 3; ++k)
        {
            double re = 0, im = 0;
            for (int i = 0; i < N; ++i) { const double a = kTwoPi * (double) k * i / N; re += xs[(size_t) i] * std::cos (a); im -= xs[(size_t) i] * std::sin (a); }
            harm += re * re + im * im;
        }
    }
    return 2.0 * harm / ((double) N * tot);
}

static void writeWav (const char* path, const Buf& x, int sr)
{
    FILE* f = std::fopen (path, "wb"); if (! f) return;
    const uint32_t bytes = (uint32_t) (x.size() * 2);
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + bytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (1); w32 ((uint32_t) sr); w32 ((uint32_t) sr * 2); w16 (2); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (bytes);
    for (float s : x) { const int16_t v = (int16_t) std::lrintf (std::fmax (-1.f, std::fmin (1.f, s)) * 32767.f); std::fwrite (&v, 2, 1, f); }
    std::fclose (f);
}

// ---------- test bookkeeping ----------
static int failures = 0;
static void check (bool ok, const char* fmt, double a = 0, double b = 0, double c = 0)
{
    char msg[200]; std::snprintf (msg, sizeof msg, fmt, a, b, c);
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", msg);
    if (! ok) ++failures;
}

static Params hardTuneC()                  // instant correction to C major, preamp neutral
{
    Params p; p.key = 0; p.scale = 1; p.retuneMs = 0.0f; p.amount = 1.0f;
    p.hpfHz = 20.0f; p.drive = 0.0f; p.comp = 0.0f; return p;
}

// Median pitch error (cents) of the output in a time slice relative to a target frequency.
static double centsOff (const Buf& y, double sr, double t0, double t1, double targetHz)
{
    std::vector<double> errs;
    for (double t = t0; t + 0.085 < t1; t += 0.05)
    {
        const double f = measureF0 (y.data() + (size_t) (t * sr), (int) (0.085 * sr), sr, 70, 900);
        if (f > 0) errs.push_back (1200.0 * std::log2 (f / targetHz));
    }
    if (errs.empty()) return 999.0;
    std::sort (errs.begin(), errs.end());
    return errs[errs.size() / 2];
}

int main()
{
    const double rates[] = { 48000.0, 44100.0, 96000.0 };

    for (double sr : rates)
    {
        std::printf ("\n=== sample rate %.0f Hz ===\n", sr);
        const double A3 = 220.0;

        // 1) In tune: should be transparent
        {
            Buf in = makeVoice (sr, 2.5, [&] (double) { return A3; });
            Buf out = run (in, sr, hardTuneC());
            const double off = centsOff (out, sr, 1.0, 2.3, A3);
            const double ratioDb = 20 * std::log10 (rms (out, (size_t) (1.0 * sr), (size_t) (2.3 * sr)) / rms (in, (size_t) (1.0 * sr), (size_t) (2.3 * sr)));
            const int N = 16384; const size_t s0 = (size_t) (1.2 * sr);
            const double pIn = harmonicPurity (in.data() + s0, N, sr, A3), pOut = harmonicPurity (out.data() + s0, N, sr, A3);
            check (std::fabs (off) < 3.0, "in-tune A3 stays in tune (%.2f cents off)", off);
            check (std::fabs (ratioDb) < 1.0, "in-tune level unchanged (%.2f dB)", ratioDb);
            check (pOut > pIn - 0.03, "in-tune purity %.3f (input %.3f)", pOut, pIn);
        }

        // 2) 40 cents flat -> corrected
        {
            Buf in = makeVoice (sr, 2.5, [&] (double) { return A3 * centsToRatio (-40); });
            Buf out = run (in, sr, hardTuneC());
            const double off = centsOff (out, sr, 1.0, 2.3, A3);
            const int N = 16384; const size_t s0 = (size_t) (1.2 * sr);
            const double pOut = harmonicPurity (out.data() + s0, N, sr, A3);
            check (std::fabs (off) < 5.0, "40 cents flat corrected to A3 (%.2f cents off)", off);
            check (pOut > 0.9, "corrected tone is clean: harmonic purity %.3f", pOut);
        }

        // 3) 60 cents sharp -> corrected
        {
            Buf in = makeVoice (sr, 2.5, [&] (double) { return A3 * centsToRatio (60); });
            Buf out = run (in, sr, hardTuneC());
            const double off = centsOff (out, sr, 1.0, 2.3, A3);
            check (std::fabs (off) < 5.0, "60 cents sharp corrected to A3 (%.2f cents off)", off);
        }

        // 4) Vibrato +-50 cents at 5.5 Hz: hard tune flattens it
        {
            Buf in = makeVoice (sr, 3.0, [&] (double t) { return A3 * centsToRatio (50 * std::sin (kTwoPi * 5.5 * t)); });
            Buf out = run (in, sr, hardTuneC());
            std::vector<double> errs;
            for (double t = 1.0; t + 0.06 < 2.8; t += 0.04)
            {
                const double f = measureF0 (out.data() + (size_t) (t * sr), (int) (0.06 * sr), sr, 70, 900);
                if (f > 0) errs.push_back (1200 * std::log2 (f / A3));
            }
            double mean = 0, var = 0; for (double e : errs) mean += e; mean /= (double) errs.size();
            for (double e : errs) { var += (e - mean) * (e - mean); }
            var = std::sqrt (var / (double) errs.size());
            check (var < 15.0, "vibrato +-50c flattened: remaining spread %.1f cents (input ~35)", var);
        }

        // 5) Off-key melody in C major with a natural retune speed
        {
            const double notes[4] = { 261.6256, 329.6276, 391.9954, 440.0 };     // C4 E4 G4 A4
            const double offs[4]  = { 30, -25, 40, -35 };
            Buf in = makeVoice (sr, 2.8, [&] (double t) { const int k = std::min (3, (int) (t / 0.7)); return notes[k] * centsToRatio (offs[k]); });
            Params p = hardTuneC(); p.retuneMs = 20.0f;
            Buf out = run (in, sr, p);
            double worst = 0;
            for (int k = 0; k < 4; ++k)
            {
                const double e = centsOff (out, sr, 0.7 * k + 0.3, 0.7 * k + 0.68, notes[k]);
                worst = std::max (worst, std::fabs (e));
            }
            check (worst < 10.0, "melody C4 E4 G4 A4 sung off-key lands on the notes (worst %.1f cents)", worst);
            if (sr == 48000.0) { writeWav ("demo_before.wav", in, 48000); writeWav ("demo_after.wav", out, 48000); }
        }

        // 6) Amount 50% -> half the correction
        {
            Buf in = makeVoice (sr, 2.5, [&] (double) { return A3 * centsToRatio (-40); });
            Params p = hardTuneC(); p.amount = 0.5f;
            Buf out = run (in, sr, p);
            const double off = centsOff (out, sr, 1.0, 2.3, A3);
            check (std::fabs (off + 20.0) < 6.0, "amount 50%%: 40 cents flat becomes %.1f cents (expect -20)", off);
        }

        // 7) Correction switched off leaves pitch alone
        {
            Buf in = makeVoice (sr, 2.0, [&] (double) { return A3 * centsToRatio (-40); });
            Params p = hardTuneC(); p.correct = false;
            Buf out = run (in, sr, p);
            const double off = centsOff (out, sr, 0.8, 1.8, A3);
            check (std::fabs (off + 40.0) < 4.0, "correction off: pitch untouched (%.1f cents)", off);
        }

        // 8) Different key: A minor note C#4 voice sung in A major vs C minor
        {
            const double f = 277.18 * centsToRatio (-30);        // C#4, 30 cents flat
            Buf in = makeVoice (sr, 2.0, [&] (double) { return f; });
            Params p = hardTuneC(); p.key = 9; p.scale = 1;      // A major: C# is in the scale
            Buf out = run (in, sr, p);
            check (std::fabs (centsOff (out, sr, 0.8, 1.8, 277.18)) < 5.0, "A major: C#4 stays C#4 (%.1f cents off)", centsOff (out, sr, 0.8, 1.8, 277.18));
            p.key = 0; p.scale = 2;                              // C minor: C# not allowed -> C4 or D4
            Buf out2 = run (in, sr, p);
            const double fm = [&] { double v = measureF0 (out2.data() + (size_t) (1.0 * sr), (int) (0.1 * sr), sr, 70, 900); return v; }();
            const double midi = hzToMidi (fm);
            check (std::fabs (midi - std::round (midi)) < 0.1 && (int) std::lround (midi) % 12 != 1, "C minor: C#4 moved to an allowed note (midi %.2f)", midi);
        }
    }

    // 9) Noise / silence / stereo / extremes (48 kHz)
    {
        const double sr = 48000.0;
        std::mt19937 rng (3); std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
        Buf noise ((size_t) (2 * sr)); for (auto& s : noise) s = 0.1f * uni (rng);
        Buf out = run (noise, sr, hardTuneC());
        bool finite = true; for (float s : out) finite = finite && std::isfinite (s);
        const double db = 20 * std::log10 (rms (out, (size_t) sr, (size_t) (2 * sr)) / rms (noise, (size_t) sr, (size_t) (2 * sr)));
        check (finite && std::fabs (db) < 1.5, "noise passes through at the same level (%.2f dB)", db);

        Buf silence ((size_t) (sr), 0.0f);
        Buf outS = run (silence, sr, hardTuneC());
        check (rms (outS, 0, outS.size()) == 0.0, "silence in -> silence out");

        Buf in = makeVoice (sr, 1.5, [&] (double) { return 220.0 * centsToRatio (-40); });
        Buf outM = run (in, sr, hardTuneC(), 1);
        Params extreme = hardTuneC(); extreme.drive = 1; extreme.comp = 1; extreme.bodyDb = 12; extreme.presenceDb = 12; extreme.airDb = 12; extreme.inGainDb = 24; extreme.outGainDb = 12;
        Buf outE = run (in, sr, extreme, 2);
        bool fin = true; double pk = 0; for (float s : outE) { fin = fin && std::isfinite (s); pk = std::max (pk, (double) std::fabs (s)); }
        check (fin && pk < 20.0, "extreme settings stay finite (peak %.2f)", pk);
        (void) outM;
    }

    // 10) Preamp: neutral response, HPF, EQ bands (correction off so only the preamp acts)
    {
        const double sr = 48000.0;
        auto toneGain = [&] (double f, const Params& p)
        {
            Buf in (( size_t) (1.5 * sr)); for (size_t i = 0; i < in.size(); ++i) in[i] = 0.1f * (float) std::sin (kTwoPi * f * (double) i / sr);
            Buf out = run (in, sr, p);
            return 20 * std::log10 (rms (out, (size_t) sr, (size_t) (1.4 * sr)) / rms (in, (size_t) sr, (size_t) (1.4 * sr)));
        };
        Params n; n.correct = false; n.hpfHz = 20; n.drive = 0; n.comp = 0;
        check (std::fabs (toneGain (1000, n)) < 0.3, "preamp neutral: 1 kHz gain %.2f dB", toneGain (1000, n));
        Params h = n; h.hpfHz = 80;
        check (toneGain (30, h) < -10.0 && std::fabs (toneGain (1000, h)) < 0.5, "high-pass 80 Hz: 30 Hz %.1f dB, 1 kHz %.2f dB", toneGain (30, h), toneGain (1000, h));
        Params b = n; b.bodyDb = 6;     check (toneGain (60, b) > 4.5, "body +6 dB: 60 Hz gain %.1f dB", toneGain (60, b));
        Params pr = n; pr.presenceDb = 6; check (std::fabs (toneGain (3500, pr) - 6.0) < 1.0, "presence +6 dB: 3.5 kHz gain %.1f dB", toneGain (3500, pr));
        Params a = n; a.airDb = 6;      check (toneGain (14000, a) > 4.5, "air +6 dB: 14 kHz gain %.1f dB", toneGain (14000, a));
        Params g = n; g.inGainDb = 6; g.outGainDb = -3; check (std::fabs (toneGain (1000, g) - 3.0) < 0.4, "in +6 / out -3 dB = +3 dB (%.2f)", toneGain (1000, g));
        Params d = n; d.drive = 0.6f;   check (toneGain (1000, d) < 0.0 && toneGain (1000, d) > -6.0, "drive adds soft saturation (gain %.2f dB)", toneGain (1000, d));

        // compressor reduces level differences
        Params c = n; c.comp = 0.7f;
        auto loud = [&] (float amp) { Buf in ((size_t) (2 * sr)); for (size_t i = 0; i < in.size(); ++i) in[i] = amp * (float) std::sin (kTwoPi * 440.0 * (double) i / sr);
                                      Buf out = run (in, sr, c); return rms (out, (size_t) (1.5 * sr), (size_t) (1.9 * sr)); };
        const double spreadIn = 0.5 / 0.05, spreadOut = loud (0.5f) / loud (0.05f);
        check (spreadOut < spreadIn * 0.7, "compressor narrows a %.0f:1 level spread to %.1f:1", spreadIn, spreadOut);
    }

    // 11) Latency and stereo behaviour
    {
        const double sr = 48000.0;
        Buf in = makeVoice (sr, 1.0, [&] (double) { return 330.0; });
        Params p = hardTuneC(); p.correct = false;
        Buf out = run (in, sr, p);
        int bestLag = 0; double best = -1e9;
        for (int lag = 0; lag < 2000; ++lag)
        {
            double s = 0; for (size_t i = 10000; i < 12000; ++i) s += (double) in[i - (size_t) lag] * out[i];
            if (s > best) { best = s; bestLag = lag; }
        }
        Engine e; e.prepare (sr, 2);
        check (std::abs (bestLag - e.latencySamples()) <= (int) (0.007 * sr), "delay measured %.0f samples, reported %.0f", bestLag, e.latencySamples());
    }

    // 12) CPU use
    {
        const double sr = 48000.0;
        Buf in = makeVoice (sr, 10.0, [&] (double t) { return 200.0 * centsToRatio (50 * std::sin (kTwoPi * 5.0 * t)); });
        Params p = hardTuneC(); p.comp = 0.5f; p.drive = 0.3f; p.presenceDb = 3;
        const auto t0 = std::chrono::steady_clock::now();
        Buf out = run (in, sr, p, 2);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("INFO  10 s of stereo audio processed in %.2f s (%.1f%% of one CPU core)\n", secs, 100.0 * secs / 10.0);
        check (secs < 5.0, "fast enough for real time (%.2f s for 10 s)", secs);
    }

    std::printf (failures == 0 ? "\nAll tests passed.\n" : "\n%d test(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
