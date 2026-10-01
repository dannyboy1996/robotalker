#pragma once
#include "klattalker/data.hpp"
#include "klattalker/sequencer.hpp"
#include <optional>
#include <string>
#include <vector>

namespace klattalker {

// Shell out to espeak-ng for high-quality text-to-IPA, then map IPA to
// our internal phoneme keys. Returns nullopt if espeak-ng isn't on PATH
// or its output couldn't be parsed.
//
//   USEnglish -> voice "en-us"   (only used if caller routes US here)
//   UKEnglish -> voice "en-gb"
//   Polish    -> voice "pl"
//
// Stress markers (ˈ primary, ˌ secondary) in the IPA stream are
// translated to PhonemeItem.stressed on the following vowel.
std::optional<std::vector<PhonemeItem>>
espeak_text_to_phonemes(const std::string& text, Variant variant);

// True if espeak-ng appears to be callable on this system. Cached.
bool espeak_available();

}  // namespace klattalker
