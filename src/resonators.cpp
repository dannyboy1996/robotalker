#include "klattalker/resonators.hpp"
#include <cmath>
#include <numbers>

namespace klattalker {

namespace {
constexpr double kPi = std::numbers::pi_v<double>;
}

// ----- Resonator -----------------------------------------------------------

Resonator::Resonator(int sample_rate) : fs_(sample_rate) {}

void Resonator::reset() {
    y1_ = 0.0;
    y2_ = 0.0;
}

void Resonator::process(std::span<const double> input,
                        std::span<const double> freq_hz,
                        std::span<const double> bw_hz,
                        std::span<double> output) {
    const size_t n = input.size();
    double y1 = y1_;
    double y2 = y2_;
    for (size_t i = 0; i < n; ++i) {
        const double C = -std::exp(-2.0 * kPi * bw_hz[i] / fs_);
        const double B = 2.0 * std::exp(-kPi * bw_hz[i] / fs_)
                         * std::cos(2.0 * kPi * freq_hz[i] / fs_);
        const double A = 1.0 - B - C;
        const double y = A * input[i] + B * y1 + C * y2;
        output[i] = y;
        y2 = y1;
        y1 = y;
    }
    y1_ = y1;
    y2_ = y2;
}

void Resonator::process_static(std::span<const double> input,
                               double freq_hz,
                               double bw_hz,
                               std::span<double> output) {
    const size_t n = input.size();
    const double C = -std::exp(-2.0 * kPi * bw_hz / fs_);
    const double B = 2.0 * std::exp(-kPi * bw_hz / fs_)
                     * std::cos(2.0 * kPi * freq_hz / fs_);
    const double A = 1.0 - B - C;
    double y1 = y1_;
    double y2 = y2_;
    for (size_t i = 0; i < n; ++i) {
        const double y = A * input[i] + B * y1 + C * y2;
        output[i] = y;
        y2 = y1;
        y1 = y;
    }
    y1_ = y1;
    y2_ = y2;
}

// ----- AntiResonator -------------------------------------------------------

AntiResonator::AntiResonator(int sample_rate) : fs_(sample_rate) {}

void AntiResonator::reset() {
    x1_ = 0.0;
    x2_ = 0.0;
}

void AntiResonator::process(std::span<const double> input,
                            std::span<const double> freq_hz,
                            std::span<const double> bw_hz,
                            std::span<double> output) {
    const size_t n = input.size();
    double x1 = x1_;
    double x2 = x2_;
    for (size_t i = 0; i < n; ++i) {
        const double C = -std::exp(-2.0 * kPi * bw_hz[i] / fs_);
        const double B = 2.0 * std::exp(-kPi * bw_hz[i] / fs_)
                         * std::cos(2.0 * kPi * freq_hz[i] / fs_);
        const double Anorm = 1.0 / (1.0 - B - C);
        const double y = Anorm * (input[i] - B * x1 - C * x2);
        output[i] = y;
        x2 = x1;
        x1 = input[i];
    }
    x1_ = x1;
    x2_ = x2;
}

void AntiResonator::process_static(std::span<const double> input,
                                   double freq_hz,
                                   double bw_hz,
                                   std::span<double> output) {
    const size_t n = input.size();
    const double C = -std::exp(-2.0 * kPi * bw_hz / fs_);
    const double B = 2.0 * std::exp(-kPi * bw_hz / fs_)
                     * std::cos(2.0 * kPi * freq_hz / fs_);
    const double Anorm = 1.0 / (1.0 - B - C);
    double x1 = x1_;
    double x2 = x2_;
    for (size_t i = 0; i < n; ++i) {
        const double y = Anorm * (input[i] - B * x1 - C * x2);
        output[i] = y;
        x2 = x1;
        x1 = input[i];
    }
    x1_ = x1;
    x2_ = x2;
}

}  // namespace klattalker
