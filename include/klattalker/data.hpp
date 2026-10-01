#pragma once
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace klattalker {

// A Klatt phoneme target carries the complete static configuration the
// engine needs for that phoneme:
//   * Cascade formant frequencies + bandwidths (F1..F5).
//   * Parallel-branch amplitudes A1..A6 in dB (negative -> off). These
//     drive the parallel resonators in the klsyn80/88 fricative path.
//   * Voicing/frication/aspiration/nasal coupling amplitudes.
//   * Duration + closure flag for stops.
//
// Vowel targets come from Hillenbrand et al. 1995 (steady-state averages
// for adult M / F speakers). Consonant targets follow the MITalk
// conventions in Allen, Hunnicutt & Klatt 1987, refined for our reduced
// 5-formant + 6-parallel topology with values cross-checked against
// Stevens 1998 (Acoustic Phonetics).
struct Target {
    std::array<double, 5> formants{};      // Hz (steady-state target)
    std::array<double, 5> bandwidths{};    // Hz
    std::array<double, 6> parallel_db{};   // A1..A6 dB; -100 = off
    double bypass_db = -100.0;             // AB bypass amplitude (parallel)
    double duration_ms = 120.0;
    double voicing = 1.0;
    double frication = 0.0;
    double aspiration = 0.0;
    double nasal = 0.0;
    bool closure = false;
    std::string label;

    // Locus values used by the coarticulation model. For a consonant,
    // these are the F2/F3 "loci" toward which an adjacent vowel's
    // formants transition (Delattre, Liberman & Cooper 1955). For a
    // vowel these are simply the steady-state targets, so the vowel
    // doesn't bend toward anything on its own. A "lock factor" of 1.0
    // means the formant fully reaches the locus at the consonant
    // boundary; values <1 model partial coarticulation.
    std::array<double, 5> locus{};
    double locus_lock = 0.0;               // 0..1; 0 = use target verbatim

    // Duration of the noisy release after a closure (ms). Default 20 ms
    // suits a plain stop burst (/p/, /t/, /k/, /b/, /d/, /g/). For
    // affricates (/tʃ/, /dʒ/, /tɕ/, /dʑ/) we use ~60 ms so the burst
    // carries the full affricate noise -- otherwise /ch/ collapses
    // toward /sh/ acoustically.
    double burst_ms = 20.0;

    // Voice-onset time after the release burst (ms). The first vot_ms of
    // the FOLLOWING phoneme are forced to aspirated (voicing=0,
    // aspiration=1) so the listener hears the breathy [h]-like delay
    // before voicing resumes -- the primary cue distinguishing English
    // /p t k/ (long VOT, ~60-80 ms) from /b d g/ (short VOT, ~10-20 ms).
    // Lisker & Abramson 1964 measured 58 / 70 / 80 ms for /p t k/ and
    // 1 / 5 / 21 ms for /b d g/ in word-initial position. Default 0.
    double vot_ms = 0.0;

    // Nasal pole + zero (antiformant) parameters. Klatt's parwav.c has
    // these per-frame; we keep them per-phoneme. For oral phonemes the
    // defaults (FNP=270, FNZ=280, both BW=100) cancel each other so
    // the nasal branch is acoustically transparent. For nasal
    // consonants /m/ /n/ /ŋ/ we vary them so each has its distinctive
    // resonance. For Polish nasal vowels ę/ą the antiformant is what
    // actually cancels the mid-frequency energy real speakers produce.
    // Defaults match /m/'s antiformant rather than 280 Hz -- the
    // AntiResonator's normalisation factor 1/(1-B-C) is large for
    // low-frequency zeros (~143 at 280 Hz vs ~22 at 750 Hz), and the
    // resulting per-sample coefficient swings produced audible clicks
    // at every nasal boundary. Non-nasal phonemes don't care what the
    // nasal_zero value is (nasal_amount = 0 hides the nasal branch),
    // so we use a default that keeps the filter math comfortable.
    double nasal_pole_freq = 270.0;
    double nasal_pole_bw   = 100.0;
    double nasal_zero_freq = 750.0;
    double nasal_zero_bw   = 150.0;

    // Trill model for alveolar trills (Polish/Spanish/Russian /r/).
    // The tongue tip oscillates at trill_rate_hz, producing periodic
    // closure + open phases (Solé 2002; Recasens 2014). When
    // trill_rate_hz > 0 the sequencer applies a square-wave amplitude
    // modulation to the voicing track and a brief F1 dip during the
    // closure portion of each tap cycle. `trill_open_fraction` controls
    // the duty cycle (0.6 = 40% closed / 60% open, typical for trills).
    double trill_rate_hz = 0.0;            // 0 = no trill
    double trill_depth = 0.7;              // amplitude modulation depth 0..1
    double trill_open_fraction = 0.60;     // fraction of period that is "open"
};

enum class Sex { Male, Female };

// Language variant. Drives vowel formant table selection (Hillenbrand
// for US, Deterding for UK, Jassem for Polish, Pätzold/Simpson for
// German) and G2P routing (CMU dict for US, espeak-ng for the rest).
enum class Variant { USEnglish, UKEnglish, Polish, German, Mandarin, Japanese, Italian, French };

// Vowel lookup. For USEnglish uses Hillenbrand 1995, for UKEnglish uses
// Deterding 1997, for Polish uses Jassem 2003. Returns nullopt for
// unknown labels in that variant.
std::optional<Target> vowel_target(std::string_view key, Sex sex,
                                   Variant variant = Variant::USEnglish);

// Consonant lookup by ARPABET-style key.
//   nasals:    m, n, ng
//   liquids:   l, r
//   glides:    w, y
//   v.fric:    v, dh, z, zh
//   uv.fric:   f, th, s, sh, h
//   stops:     p, t, k, b, d, g
//   affricat:  ch, jh
//   trill:     rr  (alveolar trill, used for Polish /r/)
//   alv.pal.:  sj, zj, cj, dj  (Polish /ɕ/ /ʑ/ /tɕ/ /dʑ/)
//   silence:   sil
std::optional<Target> consonant_target(std::string_view key, Sex sex,
                                       Variant variant = Variant::USEnglish);

// Unified lookup.
std::optional<Target> get_target(std::string_view key, Sex sex,
                                 Variant variant = Variant::USEnglish);

// Nasal pole/zero defaults (Klatt 1980 table I).
constexpr double kNasalPoleFreq = 270.0;
constexpr double kNasalPoleBw   = 100.0;
constexpr double kNasalZeroFreq = 280.0;
constexpr double kNasalZeroBw   = 100.0;

}  // namespace klattalker
