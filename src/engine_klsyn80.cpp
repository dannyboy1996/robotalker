// KLSYN80: Klatt 1980 cascade/parallel formant synthesizer
// (Klatt 1980, "Software for a cascade/parallel formant synthesizer",
//  JASA 67(3), p. 971).
//
// Signal flow:
//
//   VOICE SOURCE
//     impulse train at glottal closure -> RGS (glottal resonator) -> *AV
//   ASPIRATION
//     gaussian noise -> *AH  (mixed into the voice path)
//   FRICATION / BURST
//     gaussian noise -> *AF (drives the parallel branch)
//
//   CASCADE BRANCH (sonorants):
//     voice+aspir input goes through *two* parallel chains:
//       oral:  R1 -> R2 -> R3 -> R4 -> R5
//       nasal: RNP -> RNZ -> R1' -> R2' -> R3' -> R4' -> R5'
//     crossfaded by T.nasal (0 = pure oral, 1 = pure nasal). This lets
//     the sequencer turn the nasal pole/zero pair in and out cleanly
//     without state-toggling artefacts.
//
//   PARALLEL BRANCH (fricatives / stop bursts):
//     AF*noise drives six parallel resonators tuned to fixed centres
//       A1 @ 320, A2 @ 1000, A3 @ 2200, A4 @ 3500, A5 @ 4500, A6 @ 5500.
//     plus a bypass scalar AB. The per-phoneme target's parallel_db column
//     supplies A1..A6, AB.
//
//   OUTPUT = cascade_mixed + parallel_sum, normalized + DC-blocked.

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

class Klsyn80 : public Engine {
public:
    Klsyn80(int fs, const VoiceQuality& vq)
        : fs_(fs),
          bw_scale_(vq.bandwidth_scale),
          formant_scale_(vq.formant_scale),
          output_gain_(vq.output_gain),
          source_(fs),
          noise_(0xCAFEBABE0BADDECDULL),
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

        // ----- glottal voice + noise --------------------------------------
        std::vector<double> voice(n);
        source_.render(f0, voice);
        for (std::size_t i = 0; i < n; ++i) voice[i] *= T.voicing[i];

        std::vector<double> noise(n);
        noise_.fill(noise);
        // gentle pre-emphasis brightens the noise
        for (std::size_t i = n; i-- > 1; ) noise[i] = noise[i] - 0.97 * noise[i - 1];

        std::vector<double> excitation(n);
        for (std::size_t i = 0; i < n; ++i)
            excitation[i] = voice[i] + 0.5 * T.aspiration[i] * noise[i];

        // Apply per-speaker bandwidth and formant scaling.
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

        // ----- cascade: oral chain ----------------------------------------
        std::vector<double> chain_oral = excitation;   // copy
        std::vector<double> buf(n);
        for (int j = 0; j < N_FORMANTS; ++j) {
            cascade_oral_[j].process(chain_oral, freq_for(j),
                                     bw_for(j), buf);
            chain_oral.swap(buf);
        }

        // ----- cascade: nasal chain (RNP -> RNZ -> R1..R5') --------------
        // Pre-smooth the nasal pole/zero tracks (20 ms tau) -- the
        // AntiResonator's 1/(1-B-C) gain is sensitive to coefficient
        // jumps and produces audible clicks at /m/ /n/ /ŋ/ boundaries.
        std::vector<double> sm_npf(n), sm_npb(n), sm_nzf(n), sm_nzb(n);
        {
            const double a = std::exp(-1.0 / (0.020 * fs_));
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

        // crossfade oral <-> nasal by T.nasal, with 10 ms LP smoothing on
        // the mix ratio. Oral and nasal cascades have differing levels
        // (the nasal branch adds a pole+zero) so an abrupt ratio change
        // at /m/ /n/ /ŋ/ boundaries produces an audible amplitude step.
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

        // ----- parallel branch (fricatives / stop bursts) ----------------
        std::vector<double> par_in(n);
        for (std::size_t i = 0; i < n; ++i) par_in[i] = T.frication[i] * noise[i];

        // Drop any resonator centre within 200 Hz of Nyquist (avoids
        // aliasing at fs=11025).
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

        // ----- sum, normalize, DC-block ----------------------------------
        std::vector<double> mix(n);
        for (std::size_t i = 0; i < n; ++i) mix[i] = cascade_mixed[i] + par_sum[i];

        // DECtalk-style fixed-gain + hard clip (see klsyn88 comment).
        constexpr double kFixedGain = 0.12;
        const double gain = kFixedGain * output_gain_;
        for (auto& v : mix) {
            v *= gain;
            if (v > 1.0) v = 1.0;
            else if (v < -1.0) v = -1.0;
        }

        // first-order HPF (~30 Hz)
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
    Klsyn80Source source_;
    NoiseGen noise_;
    Resonator nasal_pole_;
    AntiResonator nasal_zero_;
    std::vector<Resonator> cascade_oral_;
    std::vector<Resonator> cascade_nasal_;
    std::vector<Resonator> parallel_;
};

}  // namespace

std::unique_ptr<Engine> make_klsyn80(int sample_rate, const VoiceQuality& vq) {
    return std::make_unique<Klsyn80>(sample_rate, vq);
}

}  // namespace klattalker
