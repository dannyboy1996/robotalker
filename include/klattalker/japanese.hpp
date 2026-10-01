#pragma once
#include "klattalker/sequencer.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace klattalker {

// Japanese G2P. Handles hiragana, katakana, and Hepburn romaji input.
// Does NOT handle kanji -- that requires a morphological analyzer like
// MeCab + IPADic. Kanji bytes are skipped silently.
//
// Output is a sequence of PhonemeItem with word/phrase/sentence flags
// already populated from punctuation tokens (Japanese 。、？！ AND the
// ASCII equivalents, since text_to_phonemes pre-normalizes them).
//
// Special kana handled:
//   ゃゅょ (yō-on)   : palatalise the preceding mora's consonant
//   っ    (sokuon)   : geminate the following consonant
//   ー    (chōonpu)  : lengthen the previous vowel
//   ん    (moraic n) : context-assimilated nasal coda
std::vector<PhonemeItem> japanese_text_to_phonemes(const std::string& text);

// Accent dictionary: kana spelling -> kernel position (0 = heiban).
// Defined in src/japanese_accent_dict.cpp.
const std::unordered_map<std::string, int>& japanese_accent_dict();

}  // namespace klattalker
