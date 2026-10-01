#pragma once

// Klatt 1979 / MITalk segmental-duration model.
//
// References:
//   Klatt (1979) "Synthesis by rule of segmental durations in English
//                sentences" JASA 66(3) pp.1055-1066.
//   Allen, Hunnicutt & Klatt (1987) "From Text to Speech: The MITalk
//                System" Cambridge UP, Chapter 6.
//   van Santen (1994) "Assignment of segmental duration in text-to-speech
//                synthesis" Computer Speech & Language 8.
//
// The model:
//
//     dur(p) = inherent(p) * Π_r multiplier_r(context)
//
// applied phoneme-by-phoneme. Inherent durations come from Klatt 1979
// Table II (averaged steady-state durations measured from natural
// speech). The multipliers cover the highest-impact context effects:
//
//     * Phrase-final lengthening (the last segment before a sentence
//       pause is ~40% longer than non-final occurrences).
//     * Stress lengthening (vowels with primary lexical stress are ~40%
//       longer; secondary stress ~20%).
//     * Unstressed reduction (unstressed vowels other than schwa are
//       shortened by ~25%; schwa is shortened further to ~50%).
//     * Word-final lengthening (~15% on the last segment of each word).
//     * Cluster shortening (~15% on consonants inside a 2-or-more
//       consonant cluster — accounts for coarticulation pressure).
//
// We deliberately omit the full Klatt rule set (~20 rules); these five
// give most of the audible rhythm with one table lookup per phoneme.

#include "klattalker/sequencer.hpp"
#include <string>

namespace klattalker {

// Look up the inherent duration for a phoneme key (ms).
double inherent_duration_ms(const std::string& key);

// Per-phoneme position context inside an utterance — computed from the
// sequence so the duration rules can fire.
struct DurationContext {
    bool word_final = false;       // last phoneme of a word
    bool phrase_final = false;     // last voiced phoneme before a pause
    bool in_cluster = false;       // surrounded by consonants on both sides
    int  stress = 0;               // 0/1/2 from CMU dict
    bool is_long = false;          // vowel was followed by IPA ː
    bool next_voiceless = false;   // vowel followed by voiceless consonant
};

// Apply Klatt 1979 multipliers to inherent_duration_ms for one phoneme.
double apply_duration_rules(const std::string& key,
                            double inherent_ms,
                            const DurationContext& ctx);

// Walk a phoneme sequence and rewrite each item's duration_ms based on
// inherent durations + Klatt rules. Phonemes whose duration_ms is
// already positive (caller-supplied) are left alone.
void assign_durations(std::vector<PhonemeItem>& items);

}  // namespace klattalker
