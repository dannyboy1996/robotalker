#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace klattalker {

// KLSYN80 voice source: impulse train at each glottal closure instant,
// passed through the "glottal resonator" RGS that gives the
// characteristic ~ -12 dB/oct rolloff (Klatt 1980, JASA 67(3)).
//
// Klatt's RGS sits at a low cutoff (~100 Hz) and isn't a normal 2-pole
// resonator -- the Klatt A = 1-B-C normalization collapses to A = 0 when
// the resonant frequency is at DC, so the impulse never enters the filter.
// We instead implement RGS as two cascaded one-pole LPFs at the cutoff,
// which gives the correct -12 dB/oct slope and unity DC gain.
class Klsyn80Source {
public:
    Klsyn80Source(int sample_rate,
                  double rgs_cutoff_hz = 100.0,
                  double jitter = 0.0);
    void reset();

    void render(std::span<const double> f0_track,
                std::span<double> output);

private:
    int fs_;
    double rgs_cutoff_;
    double jitter_;
    double lp1_ = 0.0;         // first one-pole state
    double lp2_ = 0.0;         // second one-pole state
    int next_impulse_ = 0;
    uint64_t rng_ = 0xABCDEF0123456789ULL;
    double rand01();
};

// KLSYN88 voice source: KLGLOTT88 polynomial flow pulse with first-
// difference radiation. Parameters follow Klatt & Klatt 1990:
//   open_quotient (OQ) — fraction of period the glottis is open
//   spectral_tilt_db (TL) — extra rolloff at 3 kHz (modal=0, breathy>0)
//   flutter (FL) — low-frequency F0 modulation depth (0..1)
//   diplophonia (DI) — pulse-amplitude asymmetry on odd/even periods (0..1)
class Klsyn88Source {
public:
    Klsyn88Source(int sample_rate,
                  double open_quotient = 0.60,
                  double spectral_tilt_db = 0.0,
                  double flutter = 0.0,
                  double diplophonia = 0.0,
                  double jitter = 0.005,
                  double shimmer = 0.02);
    void reset();
    void render(std::span<const double> f0_track,
                std::span<double> output);

    // Klatt's parwav.c gates frication noise to half-amplitude during the
    // closed phase of the glottal cycle ("nper > nmod" check). We expose
    // a per-sample 0-or-1 mask the engine can read after render() so the
    // noise path can apply the same gating to its own buffer.
    std::span<const uint8_t> noise_gate() const { return noise_gate_; }

    // True for each sample where the glottis is in its OPEN phase --
    // used by the engine to inject breathiness noise (parwav.c's
    // "voice += amp_breth * nrand" when nper < nopen).
    std::span<const uint8_t> open_phase_mask() const { return open_phase_; }

private:
    int fs_;
    double oq_, tl_, fl_, di_;
    double jitter_, shimmer_;
    uint64_t rng_ = 0x13579BDF2468ACE0ULL;
    double phase_ = 1e9;
    double period_ = 0.0;
    double pulse_amp_ = 1.0;
    int period_index_ = 0;
    double last_flow_ = 0.0;
    double lp_state_ = 0.0;
    double lp_a_ = 0.0;
    // 4x oversampling decimation low-pass (two one-pole cascade) state
    double os_lp1_ = 0.0;
    double os_lp2_ = 0.0;
    std::vector<uint8_t> noise_gate_;
    std::vector<uint8_t> open_phase_;
    double rand01();
    double randn();
    void recompute_period(double f0);
};

}  // namespace klattalker
