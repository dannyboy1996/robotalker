#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace klattalker {

// Write a mono 16-bit PCM WAV. Samples in [-1, 1] float; clipped on overflow.
// Returns true on success.
bool write_wav_mono(const std::string& path,
                    std::span<const float> samples,
                    int sample_rate);

}  // namespace klattalker
