#pragma once
#include <cstddef>
#include <span>

namespace klattalker {

// Klatt 2-pole resonator. The recursion is
//     y[n] = A*x[n] + B*y[n-1] + C*y[n-2]
// with the standard Klatt-1980 normalization A = 1 - B - C, which gives
// unity gain at DC and a per-formant peak that stays roughly flat in the
// cascade.
//
// All process() variants accept time-varying (freq, bw) and update state
// across calls so consecutive blocks join seamlessly.
class Resonator {
public:
    explicit Resonator(int sample_rate);
    void reset();

    // Per-sample time-varying form.
    void process(std::span<const double> input,
                 std::span<const double> freq_hz,
                 std::span<const double> bw_hz,
                 std::span<double> output);

    // Fixed (freq, bw) form — convenient when used inside the parallel
    // branch where each resonator's center is just a phoneme target.
    void process_static(std::span<const double> input,
                        double freq_hz,
                        double bw_hz,
                        std::span<double> output);

private:
    int fs_;
    double y1_ = 0.0;
    double y2_ = 0.0;
};

// 2-zero antiresonator — inverse of the 2-pole recursion. Used to cancel
// the unwanted pole introduced by the nasal branch.
class AntiResonator {
public:
    explicit AntiResonator(int sample_rate);
    void reset();
    void process(std::span<const double> input,
                 std::span<const double> freq_hz,
                 std::span<const double> bw_hz,
                 std::span<double> output);
    void process_static(std::span<const double> input,
                        double freq_hz,
                        double bw_hz,
                        std::span<double> output);

private:
    int fs_;
    double x1_ = 0.0;
    double x2_ = 0.0;
};

}  // namespace klattalker
