#include "klattalker/noise.hpp"
#include <cmath>

namespace klattalker {

NoiseGen::NoiseGen(uint64_t seed) : state_(seed ? seed : 0x9E3779B97F4A7C15ULL) {}

uint64_t NoiseGen::xorshift() {
    uint64_t x = state_;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    state_ = x;
    return x;
}

double NoiseGen::next() {
    if (have_spare_) {
        have_spare_ = false;
        return spare_;
    }
    // Marsaglia polar method
    double u, v, s;
    do {
        u = static_cast<double>(xorshift()) / static_cast<double>(UINT64_MAX);
        v = static_cast<double>(xorshift()) / static_cast<double>(UINT64_MAX);
        u = u * 2.0 - 1.0;
        v = v * 2.0 - 1.0;
        s = u * u + v * v;
    } while (s >= 1.0 || s == 0.0);
    const double factor = std::sqrt(-2.0 * std::log(s) / s);
    spare_ = v * factor;
    have_spare_ = true;
    return u * factor;
}

void NoiseGen::fill(std::span<double> out) {
    for (auto& x : out) x = next();
}

}  // namespace klattalker
