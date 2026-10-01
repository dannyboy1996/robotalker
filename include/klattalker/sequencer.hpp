#pragma once
#include "klattalker/data.hpp"
#include "klattalker/inflection.hpp"
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace klattalker {

struct PhonemeItem {
    std::string key;
    double duration_ms = -1.0;   // -1 -> use target default
    bool stressed = false;
    bool word_final = false;     // last phoneme of a word
    bool phrase_final = false;   // last phoneme before a phrase pause
    bool is_sentence_break = false;   // a sil for "."/"?"/"!" (not ",")
    // For sentence-break sils only: the kind of contour the *preceding*
    // sentence should have used (Statement for ".", Question for "?",
    // Statement-with-exclaim for "!"). Defaults to Statement.
    ContourKind sentence_end_kind = ContourKind::Statement;

    // Mandarin Chinese tone (1..5) on the syllable's nucleus vowel:
    //   1 = T1 high level (55)
    //   2 = T2 rising (35)
    //   3 = T3 dipping (214 / 21 in connected speech)
    //   4 = T4 falling (51)
    //   5 = T5 neutral
    // 0 = no tone information / not Mandarin. Only set on the syllable
    // nucleus, not on every phoneme.
    int tone = 0;
    // Citation form indicator for T3 only. Distinguishes the full
    // dipping-rising 214 (true) from the half-3 reduced 21 (false). T3
    // sandhi rules promote half-3 to the natural choice in connected
    // speech; full 214 is reserved for utterance-final / isolation.
    bool tone_citation = false;

    // Japanese mora nucleus marker -- the vowel (or moraic n) that
    // bears the mora-level pitch. The Japanese F0 model uses these to
    // step through H/L per mora.
    bool mora_nucleus = false;
    // Per-mora pitch (0 = low, 1 = high). Meaningful only when
    // mora_nucleus is true.
    int mora_pitch = 0;
    // Long-vowel marker. Set when the IPA stream had a ː after this
    // vowel (German /aː/ "Staat" vs /a/ "Stadt"; Italian /eː/ is rare
    // but allophonic). The duration model multiplies inherent duration
    // by ~1.6 when set so long and short vowels actually contrast.
    bool is_long = false;
};

struct ControlTracks {
    // shape: [n_formants, n_samples]
    std::vector<std::vector<double>> formant_freqs;
    std::vector<std::vector<double>> formant_bws;
    // [n_par_resonators, n_samples] in linear amplitude (10^(dB/20))
    std::vector<std::vector<double>> parallel_amps;
    // per-sample scalars
    std::vector<double> bypass_amp;
    std::vector<double> voicing;
    std::vector<double> frication;
    std::vector<double> aspiration;
    std::vector<double> nasal;
    // Variable nasal pole + zero (antiformant) trajectories. Hardcoded
    // constants in the engine were wrong for /m/ vs /n/ vs /ŋ/ which
    // differ in antiformant frequency, and for Polish nasal vowels.
    std::vector<double> nasal_pole_freq;
    std::vector<double> nasal_pole_bw;
    std::vector<double> nasal_zero_freq;
    std::vector<double> nasal_zero_bw;
    std::vector<uint8_t> voiced_mask;  // 0/1 (vector<bool> isn't span-able)

    // timing
    std::size_t n_samples = 0;
    double total_s = 0.0;
    std::vector<double> stress_times_s;
    // start times of each sentence (in seconds). Always begins with 0;
    // additional entries land at the boundary AFTER each sentence-ending
    // pause. Used by the inflection models to reset declination /
    // re-trigger phrase commands.
    std::vector<double> sentence_starts_s;
    // Same length as sentence_starts_s; encodes whether each sentence
    // should end with a statement-fall, question-rise, or stay flat.
    std::vector<ContourKind> sentence_contours;

    // Per-item start sample index, parallel to the items vector the
    // sequencer was called with. Length = items.size() + 1, last entry
    // is total n_samples. The tonal F0 model uses this to map syllable
    // tone markers (carried on PhonemeItem.tone) to sample ranges.
    std::vector<std::size_t> phoneme_start_samples;
    // Mandarin tones aligned with phoneme_start_samples (length =
    // items.size()). Non-tone phonemes carry 0.
    std::vector<int> phoneme_tones;
    // Parallel array; only meaningful when phoneme_tones[i] == 3.
    std::vector<uint8_t> phoneme_tone_citation;

    // Japanese mora-level pitch. Sample positions of mora nuclei
    // (vowels and moraic n) in order, plus the H/L pitch each carries.
    std::vector<std::size_t> mora_nucleus_samples;
    std::vector<uint8_t> mora_nucleus_pitch;     // 0 = L, 1 = H
};

// Build per-sample control tracks from a phoneme list at the given frame
// rate (default 5 ms). The engine consumes these directly.
ControlTracks render_tracks(const std::vector<PhonemeItem>& items,
                            Sex sex,
                            Variant variant = Variant::USEnglish,
                            int sample_rate = 22050,
                            double frame_ms = 5.0);

}  // namespace klattalker
