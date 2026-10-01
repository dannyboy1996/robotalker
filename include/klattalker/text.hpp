#pragma once
#include "klattalker/data.hpp"
#include "klattalker/sequencer.hpp"
#include <string>
#include <vector>

namespace klattalker {

// Convert text to a phoneme sequence. The variant selects the G2P path:
//   USEnglish  -> embedded CMU dict + LTS fallback (default).
//   UKEnglish  -> built-in RP mini-dict + the same LTS as US.
//   Polish     -> rule-based G2P operating directly on Polish orthography.
std::vector<PhonemeItem> text_to_phonemes(
    const std::string& text,
    Variant variant = Variant::USEnglish);

}  // namespace klattalker
