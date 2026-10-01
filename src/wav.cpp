#include "klattalker/wav.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace klattalker {

namespace {
struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t file_size;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;        // PCM
    uint16_t num_channels = 1;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t data_size;
};
}  // namespace

bool write_wav_mono(const std::string& path,
                    std::span<const float> samples,
                    int sample_rate) {
    const size_t n = samples.size();
    WavHeader h;
    h.sample_rate = static_cast<uint32_t>(sample_rate);
    h.byte_rate = static_cast<uint32_t>(sample_rate * 1 * 2);
    h.block_align = static_cast<uint16_t>(1 * 2);
    h.data_size = static_cast<uint32_t>(n * 2);
    h.file_size = static_cast<uint32_t>(36 + h.data_size);

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    if (std::fwrite(&h, sizeof(h), 1, f) != 1) {
        std::fclose(f);
        return false;
    }

    std::vector<int16_t> pcm(n);
    for (size_t i = 0; i < n; ++i) {
        float s = std::clamp(samples[i], -1.0f, 1.0f);
        pcm[i] = static_cast<int16_t>(s * 32767.0f);
    }
    if (std::fwrite(pcm.data(), sizeof(int16_t), n, f) != n) {
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    return true;
}

}  // namespace klattalker
