#pragma once
#include <cstdint>
#include <span>

namespace klattalker {

// Klatt-style noise generator: gaussian via xorshift + Marsaglia polar.
// Used for both frication and aspiration sources.
class NoiseGen {
public:
    explicit NoiseGen(uint64_t seed = 0x12345678ABCDEF01ULL);
    void fill(std::span<double> out);
    double next();

private:
    uint64_t state_;
    bool have_spare_ = false;
    double spare_ = 0.0;
    uint64_t xorshift();
};

}  // namespace klattalker
