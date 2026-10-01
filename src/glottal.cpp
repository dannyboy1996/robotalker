#include "klattalker/glottal.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace klattalker {
namespace {
constexpr double kPi = std::numbers::pi_v<double>;

uint64_t xorshift(uint64_t& s) {
    uint64_t x = s ? s : 0xDEADBEEFCAFEBABEULL;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    s = x;
    return x;
}
}  // namespace

// ---------------- KLSYN80 source ------------------------------------------

Klsyn80Source::Klsyn80Source(int sample_rate, double rgs_cutoff_hz,
                             double jitter)
    : fs_(sample_rate),
      rgs_cutoff_(rgs_cutoff_hz),
      jitter_(jitter) {}

void Klsyn80Source::reset() {
    lp1_ = lp2_ = 0.0;
    next_impulse_ = 0;
}

double Klsyn80Source::rand01() {
    return static_cast<double>(xorshift(rng_))
           / static_cast<double>(UINT64_MAX);
}

void Klsyn80Source::render(std::span<const double> f0_track,
                           std::span<double> output) {
    // Klatt 1980 IMPULSIVE voice source, ported from parwav.c. Each
    // glottal closure instant fires a unit impulse, which then passes
    // through the RGS (glottal resonator) -- a critically-damped 2-pole
    // at a low cutoff that gives the characteristic ~-12 dB/oct
    // glottal-spectrum rolloff. Klatt's recursion is
    //
    //     y[n] = A*x[n] + B*y[n-1] + C*y[n-2]
    //     C = -exp(-2*pi*BW/fs)
    //     B =  2*exp(-pi*BW/fs) * cos(2*pi*F/fs)
    //     A =  1 - B - C
    //
    // with F=0 the resonance sits at DC, giving an integrator-like
    // low-pass. We use F=rgs_cutoff/2 to avoid the A->0 degeneracy at
    // F=0 (Klatt's code worked at fixed-point precision, ours doesn't).
    const double F = rgs_cutoff_ * 0.5;
    const double BW = rgs_cutoff_;
    const double C = -std::exp(-2.0 * kPi * BW / fs_);
    const double B =  2.0 * std::exp(-kPi * BW / fs_)
                          * std::cos(2.0 * kPi * F / fs_);
    const double A =  1.0 - B - C;
    // Calibrate so a unit impulse through the resonator peaks at ~1.0
    // in the output. Klatt's amptable handles this via AV scaling;
    // we just match it numerically here.
    const double impulse_amp = 1.0 / std::max(A, 1e-9) * 0.1;

    const std::size_t n = f0_track.size();
    int impulse = next_impulse_;
    // We re-purpose lp1_/lp2_ as the 2-pole resonator's y[n-1]/y[n-2].
    double y1 = lp1_;
    double y2 = lp2_;
    for (std::size_t i = 0; i < n; ++i) {
        const double f0 = f0_track[i];
        double x = 0.0;
        if (f0 <= 0.0) {
            impulse = static_cast<int>(i);
        } else if (static_cast<int>(i) >= impulse) {
            x = impulse_amp;
            double period = fs_ / f0;
            if (jitter_ > 0.0) {
                period *= 1.0 + jitter_ * (rand01() * 2.0 - 1.0);
            }
            impulse += std::max(2, static_cast<int>(period + 0.5));
        }
        const double y = A * x + B * y1 + C * y2;
        output[i] = y;
        y2 = y1;
        y1 = y;
    }
    lp1_ = y1;
    lp2_ = y2;
    next_impulse_ = impulse - static_cast<int>(n);
    if (next_impulse_ < 0) next_impulse_ = 0;
}

// ---------------- KLSYN88 source ------------------------------------------

Klsyn88Source::Klsyn88Source(int sample_rate, double open_quotient,
                             double spectral_tilt_db, double flutter,
                             double diplophonia, double jitter,
                             double shimmer)
    : fs_(sample_rate), oq_(open_quotient), tl_(spectral_tilt_db),
      fl_(flutter), di_(diplophonia), jitter_(jitter), shimmer_(shimmer) {
    // Translate spectral_tilt_db (attenuation at 3 kHz) into a one-pole
    // LP coefficient. We want |H(w)| = T at w = 2*pi*3000/fs, where
    //    H(z) = (1-a) / (1 - a z^-1)
    //    |H(w)|^2 = (1-a)^2 / (1 - 2a cos w + a^2)
    // Solving for a gives the closed form:
    //    a^2 + 2 p a + 1 = 0    where  p = (T^2 cos w - 1) / (1 - T^2)
    //    a = -p - sqrt(p^2 - 1)    (the root < 1)
    if (tl_ > 0.0) {
        const double T = std::pow(10.0, -tl_ / 20.0);
        const double w = 2.0 * kPi * 3000.0 / fs_;
        const double cw = std::cos(w);
        const double T2 = T * T;
        if (T2 < 1.0) {
            const double p = (T2 * cw - 1.0) / (1.0 - T2);
            const double disc = p * p - 1.0;
            if (disc > 0.0) {
                double a = -p - std::sqrt(disc);
                a = std::clamp(a, 0.0, 0.99);
                lp_a_ = a;
            }
        }
    }
}

void Klsyn88Source::reset() {
    phase_ = 1e9;
    period_ = 0.0;
    pulse_amp_ = 1.0;
    period_index_ = 0;
    last_flow_ = 0.0;
    lp_state_ = 0.0;
    os_lp1_ = 0.0;
    os_lp2_ = 0.0;
}

double Klsyn88Source::rand01() {
    return static_cast<double>(xorshift(rng_))
           / static_cast<double>(UINT64_MAX);
}

double Klsyn88Source::randn() {
    // Box-Muller, one sample
    double u, v, s;
    do {
        u = rand01() * 2.0 - 1.0;
        v = rand01() * 2.0 - 1.0;
        s = u * u + v * v;
    } while (s >= 1.0 || s == 0.0);
    return u * std::sqrt(-2.0 * std::log(s) / s);
}

void Klsyn88Source::recompute_period(double f0) {
    if (f0 <= 0.0) {
        period_ = 0.0;
        return;
    }
    double f0_eff = f0;
    // flutter: low-frequency ~5 Hz modulation
    if (fl_ > 0.0) {
        const double t = static_cast<double>(period_index_)
                         * std::max(period_, 1.0) / fs_;
        f0_eff *= 1.0 + fl_ * 0.02 * std::sin(2.0 * kPi * 5.0 * t);
    }
    period_ = fs_ / f0_eff;
    if (jitter_ > 0.0) period_ *= 1.0 + jitter_ * randn();
    if (period_ < 2.0) period_ = 2.0;
    pulse_amp_ = 1.0 + shimmer_ * randn();
    if (di_ > 0.0 && (period_index_ % 2 == 1)) {
        pulse_amp_ *= (1.0 - di_);
    }
}

void Klsyn88Source::render(std::span<const double> f0_track,
                           std::span<double> output) {
    // 4x-oversampled KLGLOTT88, mirroring parwav.c's
    //   for (n4=0; n4<4; n4++) { ... }
    // structure. We run the polynomial-derivative source at 4*fs (so
    // each output sample requires 4 inner iterations) then decimate
    // back to fs via a two-stage one-pole low-pass at fs/4. This
    // dramatically reduces per-cycle quantization noise at high F0,
    // which is what most "buzzy female voice" complaints trace to.
    //
    // We also populate the noise_gate_ / open_phase_ masks for the
    // engine's noise-AM and breathiness paths.

    constexpr int OS = 4;
    const std::size_t n = f0_track.size();
    noise_gate_.assign(n, 1);
    open_phase_.assign(n, 0);

    // Decimation LP coefficient: two-stage cascade at cutoff = fs/4
    // (output Nyquist for the OS factor). a = exp(-2*pi*fc/(OS*fs))
    // with fc = fs/4 gives a = exp(-pi/8).
    const double lp_a = std::exp(-kPi / 8.0);
    const double lp_g = 1.0 - lp_a;

    for (std::size_t i = 0; i < n; ++i) {
        const double f0 = f0_track[i];
        if (f0 <= 0.0) {
            phase_ = 1e9;
            output[i] = 0.0;
            os_lp1_ = 0.0;
            os_lp2_ = 0.0;
            continue;
        }

        // We sample the open-phase status at the centre of the output
        // sample to populate the masks (the engine doesn't need
        // sub-sample resolution for them).
        bool any_open_this_sample = false;

        for (int sub = 0; sub < OS; ++sub) {
            if (phase_ >= period_ || period_ <= 0.0) {
                recompute_period(f0);
                phase_ = 0.0;
                ++period_index_;
            }
            double sub_sample = 0.0;
            const double T_open = oq_ * period_;
            const bool in_open_phase = (phase_ < T_open) && (T_open > 0.0);
            if (in_open_phase) {
                any_open_this_sample = true;
                const double t_sec = phase_ / fs_;
                const double T_sec = T_open / fs_;
                const double a = T_sec;
                const double deriv = 2.0 * a * t_sec - 3.0 * t_sec * t_sec;
                const double scale = 3.0 * pulse_amp_ / (T_sec * T_sec);
                sub_sample = scale * deriv;
            }
            // Decimation low-pass cascade (anti-aliasing for downsample)
            os_lp1_ = lp_g * sub_sample + lp_a * os_lp1_;
            os_lp2_ = lp_g * os_lp1_ + lp_a * os_lp2_;
            phase_ += 1.0 / OS;
        }

        double sample = os_lp2_;
        // Spectral tilt (breathy voice) applied once per output sample
        if (lp_a_ > 0.0) {
            lp_state_ = (1.0 - lp_a_) * sample + lp_a_ * lp_state_;
            sample = lp_state_;
        }
        output[i] = sample;
        noise_gate_[i] = any_open_this_sample ? 1 : 0;
        open_phase_[i] = any_open_this_sample ? 1 : 0;
    }
}

}  // namespace klattalker
