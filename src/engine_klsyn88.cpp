// KLSYN88: Klatt & Klatt 1990 update
// (Klatt & Klatt 1990, "Analysis, synthesis, and perception of voice
//  quality variations among male and female talkers", JASA 87(2)).
//
// Differences from KLSYN80:
//   * Voice source is the KLGLOTT88 polynomial flow pulse, not impulse+RGS.
//     Adds OQ (open quotient), TL (spectral tilt), FL (flutter), DI
//     (diplophonia) — the modal/breathy/creaky knobs.
//   * Aspiration is modulated by the voicing pulse so breathy phonation
//     gets noise concentrated during the open phase (rather than
//     constantly leaking).
//   * Sinusoidal-voicing path AVS — adds a high-passed copy of the
//     voicing signal to the parallel branch to keep high-frequency
//     energy from dropping during breathy/quiet voicing. Here we route
//     a fraction of the voicing through the bypass path.
//
// Cascade + parallel branch topology is identical to klsyn80, so we
// reuse the same data tables.

#include "klattalker/engine.hpp"
#include "klattalker/glottal.hpp"
#include "klattalker/noise.hpp"
#include "klattalker/resonators.hpp"
#include "klattalker/data.hpp"
#include "klattalker/synth.hpp"   // for VoiceQuality
#include <algorithm>
#include <cmath>

namespace klattalker {

namespace {

constexpr int N_FORMANTS = 5;
constexpr int N_PARALLEL = 6;
constexpr double kParallelCentres[N_PARALLEL] = {320.0, 1000.0, 2200.0,
                                                  3500.0, 4500.0, 5500.0};
constexpr double kParallelBws[N_PARALLEL] =     {100.0,  120.0,  150.0,
                                                  200.0,  250.0,  300.0};

constexpr double kAVSGain   = 0.15;     // sinusoidal-voicing aux gain

class Klsyn88 : public Engine {
public:
    Klsyn88(int fs, const VoiceQuality& vq)
        : fs_(fs),
          bw_scale_(vq.bandwidth_scale),
          formant_scale_(vq.formant_scale),
          output_gain_(vq.output_gain),
          source_(fs, vq.open_quotient, vq.spectral_tilt_db,
                  vq.flutter, vq.diplophonia),
          noise_(0xC0FFEE7AB1E0123ULL),
          nasal_pole_(fs),
          nasal_zero_(fs) {
        cascade_oral_.reserve(N_FORMANTS);
        cascade_nasal_.reserve(N_FORMANTS);
        parallel_.reserve(N_PARALLEL);
        for (int i = 0; i < N_FORMANTS; ++i) {
            cascade_oral_.emplace_back(fs);
            cascade_nasal_.emplace_back(fs);
        }
        for (int i = 0; i < N_PARALLEL; ++i) parallel_.emplace_back(fs);
    }

    std::vector<float> render(const ControlTracks& T,
                              std::span<const double> f0) override {
        const std::size_t n = T.n_samples;

        // ----- KLGLOTT88 voice source --------------------------------------
        std::vector<double> voice(n);
        source_.render(f0, voice);
        for (std::size_t i = 0; i < n; ++i) voice[i] *= T.voicing[i];

        // ----- noise sources ----------------------------------------------
        std::vector<double> noise(n);
        noise_.fill(noise);
        for (std::size_t i = n; i-- > 1; ) noise[i] = noise[i] - 0.97 * noise[i - 1];

        // Klatt parwav.c trick: during voicing, halve the frication noise
        // in the closed phase of the glottal cycle.
        auto noise_gate = source_.noise_gate();
        for (std::size_t i = 0; i < n; ++i) {
            const bool voiced = T.voicing[i] > 0.05;
            if (voiced && i < noise_gate.size() && noise_gate[i] == 0) {
                noise[i] *= 0.5;
            }
        }
        auto open_phase = source_.open_phase_mask();
        std::vector<double> excitation(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double asp = T.aspiration[i];
            const bool in_open = i < open_phase.size() && open_phase[i];
            const double breath = (T.voicing[i] > 0.05 && in_open)
                ? 0.10 * noise[i] : 0.0;
            excitation[i] = voice[i] + asp * 0.5 * noise[i] + breath;
        }

        // Apply per-speaker bandwidth and formant scaling. Bandwidth
        // scale widens formants for female / breathy voices; formant
        // scale shifts centre frequencies for vocal-tract-length
        // differences (Josh child = 1.20x, John deep male = 0.93x).
        std::vector<std::vector<double>> bws_scaled(N_FORMANTS);
        std::vector<std::vector<double>> freqs_scaled(N_FORMANTS);
        if (std::abs(bw_scale_ - 1.0) > 1e-6) {
            for (int j = 0; j < N_FORMANTS; ++j) {
                bws_scaled[j].resize(n);
                for (std::size_t i = 0; i < n; ++i)
                    bws_scaled[j][i] = T.formant_bws[j][i] * bw_scale_;
            }
        }
        if (std::abs(formant_scale_ - 1.0) > 1e-6) {
            for (int j = 0; j < N_FORMANTS; ++j) {
                freqs_scaled[j].resize(n);
                for (std::size_t i = 0; i < n; ++i)
                    freqs_scaled[j][i] = T.formant_freqs[j][i] * formant_scale_;
            }
        }
        auto bw_for = [&](int j) -> std::span<const double> {
            return bws_scaled[j].empty()
                ? std::span<const double>(T.formant_bws[j])
                : std::span<const double>(bws_scaled[j]);
        };
        auto freq_for = [&](int j) -> std::span<const double> {
            return freqs_scaled[j].empty()
                ? std::span<const double>(T.formant_freqs[j])
                : std::span<const double>(freqs_scaled[j]);
        };

        // ----- cascade oral chain -----------------------------------------
        std::vector<double> chain_oral = excitation;
        std::vector<double> buf(n);
        for (int j = 0; j < N_FORMANTS; ++j) {
            cascade_oral_[j].process(chain_oral, freq_for(j),
                                     bw_for(j), buf);
            chain_oral.swap(buf);
        }

        // ----- cascade nasal chain ----------------------------------------
        // Use the per-frame nasal pole/zero tracks. The AntiResonator's
        // normalisation factor 1/(1-B-C) is sensitive to coefficient
        // jumps (it can swing by 20-100x between phonemes with different
        // antiformant frequencies), so we pre-smooth those tracks with a
        // 20 ms one-pole LP. Without this, /m/ /n/ /ŋ/ transitions
        // produce sample-level click transients of magnitude ~0.6 that
        // are clearly audible as nasal "ticks".
        std::vector<double> sm_npf(n), sm_npb(n), sm_nzf(n), sm_nzb(n);
        {
            const double a = std::exp(-1.0 / (0.020 * fs_));   // tau = 20 ms
            double npf_s = T.nasal_pole_freq[0];
            double npb_s = T.nasal_pole_bw[0];
            double nzf_s = T.nasal_zero_freq[0];
            double nzb_s = T.nasal_zero_bw[0];
            for (std::size_t i = 0; i < n; ++i) {
                npf_s = (1 - a) * T.nasal_pole_freq[i] + a * npf_s;
                npb_s = (1 - a) * T.nasal_pole_bw[i]   + a * npb_s;
                nzf_s = (1 - a) * T.nasal_zero_freq[i] + a * nzf_s;
                nzb_s = (1 - a) * T.nasal_zero_bw[i]   + a * nzb_s;
                sm_npf[i] = npf_s; sm_npb[i] = npb_s;
                sm_nzf[i] = nzf_s; sm_nzb[i] = nzb_s;
            }
        }
        nasal_pole_.process(excitation, sm_npf, sm_npb, buf);
        std::vector<double> chain_nasal(n);
        nasal_zero_.process(buf, sm_nzf, sm_nzb, chain_nasal);
        for (int j = 0; j < N_FORMANTS; ++j) {
            cascade_nasal_[j].process(chain_nasal, freq_for(j),
                                      bw_for(j), buf);
            chain_nasal.swap(buf);
        }

        // Smooth the nasal-amount crossfade signal with a 10 ms tau LP.
        // The oral and nasal cascades have differing output amplitudes
        // (the nasal branch has an extra pole+zero), so a fast change in
        // crossfade ratio produces an audible amplitude step at /m/ /n/
        // /ŋ/ boundaries -- the slight click the user reported.
        std::vector<double> sm_nasal(n);
        {
            const double a = std::exp(-1.0 / (0.010 * fs_));
            double s = T.nasal[0];
            for (std::size_t i = 0; i < n; ++i) {
                s = (1 - a) * T.nasal[i] + a * s;
                sm_nasal[i] = s;
            }
        }
        std::vector<double> cascade_mixed(n);
        for (std::size_t i = 0; i < n; ++i)
            cascade_mixed[i] = (1.0 - sm_nasal[i]) * chain_oral[i]
                              + sm_nasal[i] * chain_nasal[i];

        // ----- parallel branch --------------------------------------------
        std::vector<double> par_in(n);
        for (std::size_t i = 0; i < n; ++i) par_in[i] = T.frication[i] * noise[i];

        // At low sample rates a parallel resonator centred above
        // Nyquist (e.g. A6 at 5500 Hz when fs=11025) aliases violently.
        // Skip any centre within 200 Hz of Nyquist.
        const double nyquist_margin = fs_ * 0.5 - 200.0;

        std::vector<double> par_sum(n, 0.0);
        std::vector<double> par_out(n);
        for (int j = 0; j < N_PARALLEL; ++j) {
            if (kParallelCentres[j] >= nyquist_margin) continue;
            parallel_[j].process_static(par_in, kParallelCentres[j],
                                        kParallelBws[j], par_out);
            for (std::size_t i = 0; i < n; ++i)
                par_sum[i] += T.parallel_amps[j][i] * par_out[i];
        }
        for (std::size_t i = 0; i < n; ++i)
            par_sum[i] += T.bypass_amp[i] * par_in[i];

        // ----- AVS (sinusoidal-voicing aux into parallel) -----------------
        // Klatt&Klatt 1990 used this to compensate for breathy voicing's
        // HF energy loss. We add a small fraction of the voicing through
        // a high-pass differentiator so the parallel branch picks up
        // any glottal HF energy.
        double prev_v = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double hp = voice[i] - prev_v;
            prev_v = voice[i];
            par_sum[i] += kAVSGain * T.voicing[i] * hp;
        }

        // ----- sum, fixed-gain calibration, hard clip ---------------------
        //
        // Match DECtalk's vtmio output stage: each cascade/parallel
        // resonator has its own per-speaker amplitude (we encode this
        // via the per-phoneme parallel_db tables and the speaker's
        // bandwidth_scale). After summing we apply a single fixed gain
        // and hard-clip to [-1, 1] -- exactly the
        //    if (out > 8191) out = 8191; else if (out < -8192) out = -8192;
        //    iwave[ns] = out << 2;
        // pattern from dectalk/src/dapi/src/vtm/vtm_i.c. No adaptive
        // 95th-percentile normalization, no tanh soft limit.
        constexpr double kFixedGain = 0.12;
        const double gain = kFixedGain * output_gain_;
        std::vector<double> mix(n);
        for (std::size_t i = 0; i < n; ++i) {
            double v = (cascade_mixed[i] + par_sum[i]) * gain;
            if (v > 1.0) v = 1.0;
            else if (v < -1.0) v = -1.0;
            mix[i] = v;
        }

        const double a = 0.995;
        double prev_in = 0.0, prev_out = 0.0;
        std::vector<float> y(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double cur = mix[i];
            const double yi = a * (prev_out + cur - prev_in);
            y[i] = static_cast<float>(yi);
            prev_in = cur;
            prev_out = yi;
        }
        return y;
    }

private:
    int fs_;
    double bw_scale_;
    double formant_scale_;
    double output_gain_;
    Klsyn88Source source_;
    NoiseGen noise_;
    Resonator nasal_pole_;
    AntiResonator nasal_zero_;
    std::vector<Resonator> cascade_oral_;
    std::vector<Resonator> cascade_nasal_;
    std::vector<Resonator> parallel_;
};

}  // namespace

std::unique_ptr<Engine> make_klsyn88(int sample_rate, const VoiceQuality& vq) {
    return std::make_unique<Klsyn88>(sample_rate, vq);
}

// Engine factory — defined here so both engines are visible.
std::unique_ptr<Engine> make_klsyn80(int sample_rate, const VoiceQuality& vq);

std::unique_ptr<Engine> make_engine(EngineKind kind, int sample_rate,
                                    const VoiceQuality& vq) {
    if (kind == EngineKind::Klsyn80) return make_klsyn80(sample_rate, vq);
    return make_klsyn88(sample_rate, vq);
}

}  // namespace klattalker
