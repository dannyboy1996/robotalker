#pragma once

// Klatt segmental-intensity model.
//
// Reference:
//   Klatt (1987) "Review of text-to-speech conversion for English"
//                JASA 82(3) pp.737-793, Section IV.D (Amplitudes).
//   Allen, Hunnicutt & Klatt (1987) "From Text to Speech: The MITalk
//                System" Cambridge UP, Chapter 6, Tables of "AVT"
//                (target voicing amplitude) per segment.
//
// Each phoneme has an intrinsic loudness (in dB). Vowels are the
// reference (~0 dB). Sonorants (nasals, liquids, glides) sit a few dB
// below. Voiced fricatives are quieter still; voiceless fricatives
// lowest of all. Stops are gated entirely by closure + burst.
//
// We apply the rule to two paths:
//   * voicing  multiplier (cascade amplitude)
//   * frication multiplier (parallel amplitude)
// so the final signal energy matches the phonetic prediction.

#include <string>

namespace klattalker {

// Inherent voicing amplitude relative to a 1.0 vowel reference.
// Returns 1.0 for unknown / vowel phones.
double inherent_voicing_amp(const std::string& key);

// Inherent frication amplitude. Returns 0.0 for non-fricative phones.
double inherent_frication_amp(const std::string& key);

}  // namespace klattalker
