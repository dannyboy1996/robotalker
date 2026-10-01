#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace klattalker {

// Sentence-level F0 contour kinds driven by terminal punctuation:
//   Statement   -- "." -- declarative declination + small terminal fall
//   Question    -- "?" -- terminal rise in the last 250 ms
//   Exclamation -- "!" -- starts at maximum F0, drops linearly to minimum
//   Flat        -- explicit override; just declination with no terminal
enum class ContourKind { Statement, Question, Flat, Exclamation };
enum class InflectionKind { Fujisaki, KlattRules, Tonal, Japanese };

// ---- Klatt F0 rules -----------------------------------------------------
//
// References:
//   Klatt (1973) "Discrimination of fundamental frequency contours in
//                synthetic speech: Implications for models of pitch
//                perception", JASA 53(1) pp.8-16.
//   Klatt (1987) "Review of text-to-speech conversion for English",
//                JASA 82(3) §IV.C ("Fundamental frequency contour").
//
// The model:
//
//   1. Linear declination in semitones across the phrase. Default slope
//      is ~10 semitones from start to end (Cohen & 't Hart's reference).
//   2. Per-stressed-syllable accent: a bell-shaped peak ~3 semitones
//      above the declination line, FWHM ~ 200 ms.
//   3. Sentence boundary:
//        statement -> additional fall of ~3 semitones in the last 200 ms
//        question  -> rise of ~5 semitones in the last 200 ms
//
// Implemented in *semitone* space, which is closer to perceptual pitch
// than linear Hz — and matches what Klatt argued for in the 1973 paper.
struct KlattF0Model {
    double base_f0 = 90.0;                  // baseline at phrase start (Hz)
    double declination_st = 10.0;           // total ST drop across phrase
    double accent_peak_st = 3.0;            // accent prominence above line
    double accent_fwhm_ms = 200.0;
    double terminal_fall_st = 3.0;          // statement: ST drop at end
    double terminal_rise_st = 5.0;          // question: ST rise at end
    double terminal_window_ms = 200.0;

    void render(int sample_rate,
                std::span<const uint8_t> voiced_mask,
                std::span<const double> stress_times_s,
                std::span<const double> sentence_starts_s,
                std::span<const ContourKind> sentence_contours,
                ContourKind contour,
                std::span<double> f0_out) const;
};

// ---- Fujisaki -----------------------------------------------------------
//
// The only intonation model. Faithful to Sato/Kameoka/Kashino conventions
// (hitonanode/FujisakiEstimation):
//
//   impulse_response(omega, t) = omega^2 * t * exp(-omega * t)        t > 0
//   step_response(omega, t)    = 1 - (1 + omega*t) * exp(-omega*t)    t > 0
//
// Synthesis (per-sample log-F0):
//
//   ln F0(t) = mu_b
//            + sum_phrase  Ap * impulse_response(omega_p, t - T0)
//            + sum_accent  Aa * (step_response(omega_a, t - T1)
//                                - step_response(omega_a, t - T2))
//
// where mu_b = ln(baseline_f0_hz). Typical parameter ranges
// (Fujisaki 1984, Mixdorff 2000):
//
//   omega_p (alpha) : 2 .. 3   rad/s    — phrase command speed
//   omega_a (beta)  : 15 .. 25 rad/s    — accent command speed
//   Ap              : 0.1 .. 0.5        — phrase magnitude (log-F0)
//   Aa              : 0.1 .. 0.3        — accent magnitude (log-F0)
enum class FujisakiCommandType { Phrase, Accent };

struct FujisakiCommand {
    FujisakiCommandType type = FujisakiCommandType::Phrase;
    double onset = 0.0;             // T0 for phrase, T1 for accent (seconds)
    double offset = 0.0;            // ignored for phrase, T2 for accent
    double integrated_amplitude = 0.0;   // Ap or Aa
    double omega = 3.0;             // alpha or beta (rad/s)
};

struct FujisakiModel {
    double baseline_f0 = 70.0;      // F_b in Hz
    double default_phrase_omega = 3.0;
    double default_accent_omega = 20.0;
    double default_phrase_Ap = 0.30;
    double default_accent_Aa = 0.20;

    void render(int sample_rate,
                std::span<const uint8_t> voiced_mask,
                std::span<const FujisakiCommand> commands,
                std::span<double> f0_out) const;
};

// Auto-derive a command set when the caller doesn't supply one:
//   * Opening phrase impulse at t = 0.
//   * One accent rectangle per stress mark, centred on the mark,
//     accent_width_s wide.
//   * Questions: a second phrase impulse near the end (~0.35 s before
//     utterance end) at 0.6 * Ap, producing the characteristic
//     log-F0 rise.
std::vector<FujisakiCommand> default_fujisaki_commands(
    std::span<const double> stress_times_s,
    std::span<const double> sentence_starts_s,
    std::span<const ContourKind> sentence_contours,
    double total_s,
    ContourKind fallback_contour,
    const FujisakiModel& model,
    double accent_width_s = 0.25);

// ---- Mandarin tonal F0 model --------------------------------------------
//
// Lexical tone in Mandarin Chinese is encoded as Chao tone letters --
// 5 = highest pitch in speaker range, 1 = lowest. Each syllable carries
// one of:
//   T1 (55) high level
//   T2 (35) mid-rising
//   T3 (214 citation, 21 half-3 in connected speech) low dipping
//   T4 (51) high falling
//   T5 (neutral) -- depends on preceding tone, drifts toward mid
//
// The model walks the input items, finds each toned syllable's nucleus,
// stretches the corresponding Chao contour over its sample range, and
// interpolates smoothly between syllables. Outside toned syllables
// (silence, unvoiced segments) F0 stays at the previous tone's end.
//
// References:
//   Chao Y-R 1930 "A system of tone letters", Le Maître Phonétique
//   Xu Y 1997 "Contextual tonal variations in Mandarin", J.Phonetics
//   Lin H 2007 "The Sounds of Chinese" §4 (tonal coarticulation rules)
struct TonalF0Model {
    double midline_f0 = 130.0;     // F0 at Chao level 3 (Hz)
    double range_st   = 14.0;      // total semitone range 5 -> 1

    void render(int sample_rate,
                std::span<const uint8_t> voiced_mask,
                std::span<const std::size_t> phoneme_start_samples,
                std::span<const int> phoneme_tones,
                std::span<const uint8_t> phoneme_tone_citation,
                std::span<double> f0_out) const;
};

// ---- Japanese F0 model --------------------------------------------------
//
// Japanese is mora-timed with lexical pitch accent (H/L pattern per
// word). Without a per-word accent dictionary we can't do real
// pitch-accent modeling, so this is a phrase-level approximation:
//
//   * Each sentence (phrase between . / ? / !) starts low.
//   * Sharp rise (~5 ST) over the first 150 ms -- the typical
//     "first-mora low, second-mora high" Tokyo pattern in declension.
//   * Gentle declination (~4 ST) across the rest of the sentence.
//   * Terminal contour:
//       Statement   -> small fall (~2 ST) over last 200 ms
//       Question    -> sharp rise (~6 ST) over last 250 ms
//       Exclamation -> sharp fall (~5 ST) over last 200 ms
//
// References:
//   Pierrehumbert & Beckman 1988 "Japanese tone structure" (MIT Press)
//   Maekawa 1997 "Effects of focus on duration and vowel formant
//     frequency in Japanese"
struct JapaneseF0Model {
    double base_f0 = 110.0;
    double initial_rise_st = 5.0;
    double initial_rise_ms = 150.0;
    double declination_st = 3.0;       // gentler -- downstep handles most
    double terminal_fall_st = 1.5;
    double terminal_rise_st = 6.0;
    double exclam_fall_st = 5.0;
    double terminal_window_ms = 220.0;
    // Mora-level pitch contrast (semitones). Real Tokyo Japanese has
    // about 3-4 ST between adjacent H and L; the old 2.0 / 2.5 was too
    // small and the contour sounded monotone.
    double accent_high_st = 3.5;
    double accent_low_st  = 3.5;
    // Within a run of consecutive H moras, each successive H sits a
    // little lower than the previous one (downstep / catathesis). This
    // is what gives Japanese sentences their characteristic
    // staircase-down profile. Pierrehumbert & Beckman 1988 §4.
    double downstep_per_h_mora_st = 0.4;

    void render(int sample_rate,
                std::span<const uint8_t> voiced_mask,
                std::span<const double> sentence_starts_s,
                std::span<const ContourKind> sentence_contours,
                double total_s,
                std::span<const std::size_t> mora_nucleus_samples,
                std::span<const uint8_t> mora_nucleus_pitch,
                std::span<double> f0_out) const;
};

}  // namespace klattalker
