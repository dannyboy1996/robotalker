// Japanese G2P.
//
// Inputs accepted: hiragana, katakana, Hepburn romaji (the three forms
// learners actually paste in). Kanji is silently skipped; supporting it
// would require a tokenizer + accent dictionary (MeCab+IPADic), and the
// user explicitly told us NOT to go through espeak.
//
// Output: a phoneme stream sized in moras. Each mora consumes 1-2
// kana characters and emits one or more PhonemeItems (typically C+V or
// C+y+V for yō-on). Punctuation 。、？！ is handled at the text.cpp
// level via the CJK punctuation normalizer.
//
// Notes on consonant choices:
//   し シ shi -> sh (Japanese is actually /ɕ/ alveolo-palatal; sh is the
//                    closest English target)
//   ち チ chi -> ch (palatalised /tɕ/)
//   つ ツ tsu -> t+s (affricate)
//   ふ フ fu  -> f   (Japanese /ɸ/ bilabial; f is acoustically close)
//   ひ ヒ hi  -> xi  (Japanese /çi/ palatal -- our German ich-laut target
//                    is exactly the right shape)
//   じ ジ ji  -> jh  (alveolo-palatal /dʑ/; jh matches well)
//   ら 行     -> r   (Japanese is a tap /ɾ/; r approximant is closest)
//   ん        -> n / m / ng depending on the following onset
//
// References:
//   Vance 2008 "The Sounds of Japanese" §3 (segmental inventory)
//   Akamatsu 1997 "Japanese Phonetics: Theory and Practice"

#include "klattalker/japanese.hpp"
#include "klattalker/duration.hpp"
#include <array>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace klattalker {

namespace {

// A mora -> phonemes mapping. Keys are kana / romaji strings (UTF-8).
// Values are vectors of internal phoneme keys.
const std::unordered_map<std::string, std::vector<std::string>>& mora_table() {
    static const std::unordered_map<std::string, std::vector<std::string>> m = {
        // ---- hiragana basic ----
        {"あ", {"aa"}}, {"い", {"iy"}}, {"う", {"uw"}},
        {"え", {"eh"}}, {"お", {"ow"}},
        {"か", {"k","aa"}}, {"き", {"k","iy"}}, {"く", {"k","uw"}},
        {"け", {"k","eh"}}, {"こ", {"k","ow"}},
        {"が", {"g","aa"}}, {"ぎ", {"g","iy"}}, {"ぐ", {"g","uw"}},
        {"げ", {"g","eh"}}, {"ご", {"g","ow"}},
        {"さ", {"s","aa"}}, {"し", {"sh","iy"}}, {"す", {"s","uw"}},
        {"せ", {"s","eh"}}, {"そ", {"s","ow"}},
        {"ざ", {"z","aa"}}, {"じ", {"jh","iy"}}, {"ず", {"z","uw"}},
        {"ぜ", {"z","eh"}}, {"ぞ", {"z","ow"}},
        {"た", {"t","aa"}}, {"ち", {"ch","iy"}}, {"つ", {"t","s","uw"}},
        {"て", {"t","eh"}}, {"と", {"t","ow"}},
        {"だ", {"d","aa"}}, {"ぢ", {"jh","iy"}}, {"づ", {"z","uw"}},
        {"で", {"d","eh"}}, {"ど", {"d","ow"}},
        {"な", {"n","aa"}}, {"に", {"n","iy"}}, {"ぬ", {"n","uw"}},
        {"ね", {"n","eh"}}, {"の", {"n","ow"}},
        {"は", {"h","aa"}}, {"ひ", {"xi","iy"}}, {"ふ", {"f","uw"}},
        {"へ", {"h","eh"}}, {"ほ", {"h","ow"}},
        {"ば", {"b","aa"}}, {"び", {"b","iy"}}, {"ぶ", {"b","uw"}},
        {"べ", {"b","eh"}}, {"ぼ", {"b","ow"}},
        {"ぱ", {"p","aa"}}, {"ぴ", {"p","iy"}}, {"ぷ", {"p","uw"}},
        {"ぺ", {"p","eh"}}, {"ぽ", {"p","ow"}},
        {"ま", {"m","aa"}}, {"み", {"m","iy"}}, {"む", {"m","uw"}},
        {"め", {"m","eh"}}, {"も", {"m","ow"}},
        {"や", {"y","aa"}}, {"ゆ", {"y","uw"}}, {"よ", {"y","ow"}},
        {"ら", {"r_ja","aa"}}, {"り", {"r_ja","iy"}}, {"る", {"r_ja","uw"}},
        {"れ", {"r_ja","eh"}}, {"ろ", {"r_ja","ow"}},
        {"わ", {"w","aa"}}, {"を", {"ow"}},

        // ---- hiragana yō-on (C+y+V) ----
        {"きゃ", {"k","y","aa"}}, {"きゅ", {"k","y","uw"}}, {"きょ", {"k","y","ow"}},
        {"ぎゃ", {"g","y","aa"}}, {"ぎゅ", {"g","y","uw"}}, {"ぎょ", {"g","y","ow"}},
        {"しゃ", {"sh","aa"}},    {"しゅ", {"sh","uw"}},    {"しょ", {"sh","ow"}},
        {"じゃ", {"jh","aa"}},    {"じゅ", {"jh","uw"}},    {"じょ", {"jh","ow"}},
        {"ちゃ", {"ch","aa"}},    {"ちゅ", {"ch","uw"}},    {"ちょ", {"ch","ow"}},
        {"にゃ", {"n","y","aa"}}, {"にゅ", {"n","y","uw"}}, {"にょ", {"n","y","ow"}},
        {"ひゃ", {"xi","aa"}},    {"ひゅ", {"xi","uw"}},    {"ひょ", {"xi","ow"}},
        {"びゃ", {"b","y","aa"}}, {"びゅ", {"b","y","uw"}}, {"びょ", {"b","y","ow"}},
        {"ぴゃ", {"p","y","aa"}}, {"ぴゅ", {"p","y","uw"}}, {"ぴょ", {"p","y","ow"}},
        {"みゃ", {"m","y","aa"}}, {"みゅ", {"m","y","uw"}}, {"みょ", {"m","y","ow"}},
        {"りゃ", {"r_ja","y","aa"}}, {"りゅ", {"r_ja","y","uw"}}, {"りょ", {"r_ja","y","ow"}},

        // ---- katakana mirror (identical phoneme mapping) ----
        {"ア", {"aa"}}, {"イ", {"iy"}}, {"ウ", {"uw"}},
        {"エ", {"eh"}}, {"オ", {"ow"}},
        {"カ", {"k","aa"}}, {"キ", {"k","iy"}}, {"ク", {"k","uw"}},
        {"ケ", {"k","eh"}}, {"コ", {"k","ow"}},
        {"ガ", {"g","aa"}}, {"ギ", {"g","iy"}}, {"グ", {"g","uw"}},
        {"ゲ", {"g","eh"}}, {"ゴ", {"g","ow"}},
        {"サ", {"s","aa"}}, {"シ", {"sh","iy"}}, {"ス", {"s","uw"}},
        {"セ", {"s","eh"}}, {"ソ", {"s","ow"}},
        {"ザ", {"z","aa"}}, {"ジ", {"jh","iy"}}, {"ズ", {"z","uw"}},
        {"ゼ", {"z","eh"}}, {"ゾ", {"z","ow"}},
        {"タ", {"t","aa"}}, {"チ", {"ch","iy"}}, {"ツ", {"t","s","uw"}},
        {"テ", {"t","eh"}}, {"ト", {"t","ow"}},
        {"ダ", {"d","aa"}}, {"ヂ", {"jh","iy"}}, {"ヅ", {"z","uw"}},
        {"デ", {"d","eh"}}, {"ド", {"d","ow"}},
        {"ナ", {"n","aa"}}, {"ニ", {"n","iy"}}, {"ヌ", {"n","uw"}},
        {"ネ", {"n","eh"}}, {"ノ", {"n","ow"}},
        {"ハ", {"h","aa"}}, {"ヒ", {"xi","iy"}}, {"フ", {"f","uw"}},
        {"ヘ", {"h","eh"}}, {"ホ", {"h","ow"}},
        {"バ", {"b","aa"}}, {"ビ", {"b","iy"}}, {"ブ", {"b","uw"}},
        {"ベ", {"b","eh"}}, {"ボ", {"b","ow"}},
        {"パ", {"p","aa"}}, {"ピ", {"p","iy"}}, {"プ", {"p","uw"}},
        {"ペ", {"p","eh"}}, {"ポ", {"p","ow"}},
        {"マ", {"m","aa"}}, {"ミ", {"m","iy"}}, {"ム", {"m","uw"}},
        {"メ", {"m","eh"}}, {"モ", {"m","ow"}},
        {"ヤ", {"y","aa"}}, {"ユ", {"y","uw"}}, {"ヨ", {"y","ow"}},
        {"ラ", {"r_ja","aa"}}, {"リ", {"r_ja","iy"}}, {"ル", {"r_ja","uw"}},
        {"レ", {"r_ja","eh"}}, {"ロ", {"r_ja","ow"}},
        {"ワ", {"w","aa"}}, {"ヲ", {"ow"}},
        {"キャ", {"k","y","aa"}}, {"キュ", {"k","y","uw"}}, {"キョ", {"k","y","ow"}},
        {"ギャ", {"g","y","aa"}}, {"ギュ", {"g","y","uw"}}, {"ギョ", {"g","y","ow"}},
        {"シャ", {"sh","aa"}},    {"シュ", {"sh","uw"}},    {"ショ", {"sh","ow"}},
        {"ジャ", {"jh","aa"}},    {"ジュ", {"jh","uw"}},    {"ジョ", {"jh","ow"}},
        {"チャ", {"ch","aa"}},    {"チュ", {"ch","uw"}},    {"チョ", {"ch","ow"}},
        {"ニャ", {"n","y","aa"}}, {"ニュ", {"n","y","uw"}}, {"ニョ", {"n","y","ow"}},
        {"ヒャ", {"xi","aa"}},    {"ヒュ", {"xi","uw"}},    {"ヒョ", {"xi","ow"}},
        {"ビャ", {"b","y","aa"}}, {"ビュ", {"b","y","uw"}}, {"ビョ", {"b","y","ow"}},
        {"ピャ", {"p","y","aa"}}, {"ピュ", {"p","y","uw"}}, {"ピョ", {"p","y","ow"}},
        {"ミャ", {"m","y","aa"}}, {"ミュ", {"m","y","uw"}}, {"ミョ", {"m","y","ow"}},
        {"リャ", {"r_ja","y","aa"}}, {"リュ", {"r_ja","y","uw"}}, {"リョ", {"r_ja","y","ow"}},

        // ---- romaji (Hepburn) ----
        {"a", {"aa"}}, {"i", {"iy"}}, {"u", {"uw"}}, {"e", {"eh"}}, {"o", {"ow"}},
        {"ka", {"k","aa"}}, {"ki", {"k","iy"}}, {"ku", {"k","uw"}},
        {"ke", {"k","eh"}}, {"ko", {"k","ow"}},
        {"ga", {"g","aa"}}, {"gi", {"g","iy"}}, {"gu", {"g","uw"}},
        {"ge", {"g","eh"}}, {"go", {"g","ow"}},
        {"sa", {"s","aa"}}, {"shi",{"sh","iy"}}, {"su", {"s","uw"}},
        {"se", {"s","eh"}}, {"so", {"s","ow"}},
        {"za", {"z","aa"}}, {"ji", {"jh","iy"}}, {"zu", {"z","uw"}},
        {"ze", {"z","eh"}}, {"zo", {"z","ow"}},
        {"ta", {"t","aa"}}, {"chi",{"ch","iy"}}, {"tsu",{"t","s","uw"}},
        {"te", {"t","eh"}}, {"to", {"t","ow"}},
        {"da", {"d","aa"}}, {"de", {"d","eh"}}, {"do", {"d","ow"}},
        {"na", {"n","aa"}}, {"ni", {"n","iy"}}, {"nu", {"n","uw"}},
        {"ne", {"n","eh"}}, {"no", {"n","ow"}},
        {"ha", {"h","aa"}}, {"hi", {"xi","iy"}}, {"fu", {"f","uw"}},
        {"he", {"h","eh"}}, {"ho", {"h","ow"}},
        {"ba", {"b","aa"}}, {"bi", {"b","iy"}}, {"bu", {"b","uw"}},
        {"be", {"b","eh"}}, {"bo", {"b","ow"}},
        {"pa", {"p","aa"}}, {"pi", {"p","iy"}}, {"pu", {"p","uw"}},
        {"pe", {"p","eh"}}, {"po", {"p","ow"}},
        {"ma", {"m","aa"}}, {"mi", {"m","iy"}}, {"mu", {"m","uw"}},
        {"me", {"m","eh"}}, {"mo", {"m","ow"}},
        {"ya", {"y","aa"}}, {"yu", {"y","uw"}}, {"yo", {"y","ow"}},
        {"ra", {"r_ja","aa"}}, {"ri", {"r_ja","iy"}}, {"ru", {"r_ja","uw"}},
        {"re", {"r_ja","eh"}}, {"ro", {"r_ja","ow"}},
        {"wa", {"w","aa"}}, {"wo", {"ow"}},
        // Bare "n" -> moraic-n. Required for romaji like "sensei" /
        // "kanji" / "konnichiwa": when no CV pair matches, fall back to
        // moraic-n. The greedy 3-char>2-char>1-char order means "na"
        // still matches CV (na -> n+aa), only standalone "n" hits this.
        {"n", {"n"}},
        {"kya", {"k","y","aa"}}, {"kyu", {"k","y","uw"}}, {"kyo", {"k","y","ow"}},
        {"gya", {"g","y","aa"}}, {"gyu", {"g","y","uw"}}, {"gyo", {"g","y","ow"}},
        {"sha", {"sh","aa"}},    {"shu", {"sh","uw"}},    {"sho", {"sh","ow"}},
        {"ja",  {"jh","aa"}},    {"ju",  {"jh","uw"}},    {"jo",  {"jh","ow"}},
        {"cha", {"ch","aa"}},    {"chu", {"ch","uw"}},    {"cho", {"ch","ow"}},
        {"nya", {"n","y","aa"}}, {"nyu", {"n","y","uw"}}, {"nyo", {"n","y","ow"}},
        {"hya", {"xi","aa"}},    {"hyu", {"xi","uw"}},    {"hyo", {"xi","ow"}},
        {"bya", {"b","y","aa"}}, {"byu", {"b","y","uw"}}, {"byo", {"b","y","ow"}},
        {"pya", {"p","y","aa"}}, {"pyu", {"p","y","uw"}}, {"pyo", {"p","y","ow"}},
        {"mya", {"m","y","aa"}}, {"myu", {"m","y","uw"}}, {"myo", {"m","y","ow"}},
        {"rya", {"r_ja","y","aa"}}, {"ryu", {"r_ja","y","uw"}}, {"ryo", {"r_ja","y","ow"}},
    };
    return m;
}

// Romaji digraphs that *start* a mora -- used to skip past the geminate
// consonant when sokuon (っ) is followed by romaji. e.g. matta -> m-aa-t-aa
// (the doubled 't' is the geminate closure).
bool is_romaji_letter(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return (uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z');
}

int utf8_len(unsigned char b) {
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

bool starts_with(const std::string& s, std::size_t i, const std::string& tok) {
    if (i + tok.size() > s.size()) return false;
    return std::memcmp(s.data() + i, tok.data(), tok.size()) == 0;
}

// True if the next codepoint at `i` is a small ゃゅょ (hiragana or
// katakana) -- those combine with the previous mora into yō-on.
bool is_yoon_extender(const std::string& s, std::size_t i) {
    return starts_with(s, i, "ゃ") || starts_with(s, i, "ゅ")
        || starts_with(s, i, "ょ")
        || starts_with(s, i, "ャ") || starts_with(s, i, "ュ")
        || starts_with(s, i, "ョ");
}

bool is_sokuon(const std::string& s, std::size_t i) {
    return starts_with(s, i, "っ") || starts_with(s, i, "ッ");
}

bool is_chouonpu(const std::string& s, std::size_t i) {
    return starts_with(s, i, "ー");
}

bool is_moraic_n(const std::string& s, std::size_t i) {
    return starts_with(s, i, "ん") || starts_with(s, i, "ン");
}

// Vowel-only mora endings -- used to detect when a previous mora can
// take a chōonpu (long-vowel mark) extension.
const std::unordered_set<std::string>& vowel_phonemes() {
    static const std::unordered_set<std::string> v = {
        "iy","ih","eh","aa","ah","ao","ow","uh","uw","er",
    };
    return v;
}

// Decide the assimilated form of ん based on the NEXT phoneme key.
//   /m p b/  -> m
//   /n t d s z r/ -> n
//   /k g/    -> ng
//   default  -> n
std::string assimilate_moraic_n(const std::string& next_key) {
    if (next_key == "m" || next_key == "p" || next_key == "b") return "m";
    if (next_key == "k" || next_key == "g" || next_key == "ng") return "ng";
    return "n";
}

// High-vowel devoicing rule (Vance 2008 §3.5; Maekawa 1989). /i/ and
// /u/ between voiceless consonants -- and /u/ after a voiceless C and
// before silence or another voiceless C -- lose their voicing and
// effectively drop out perceptually. This is what makes -desu sound
// like "des", -masu like "mas", shita like "shta", suki like "ski".
//
// We approximate by deleting the vowel: the preceding fricative or
// affricate continues for its own duration and the listener doesn't
// hear the missing nucleus. Less accurate than rendering a truly
// voiceless vowel but a clear perceptual improvement.
const std::unordered_set<std::string>& voiceless_consonants_ja() {
    static const std::unordered_set<std::string> v = {
        "k", "s", "sh", "t", "ts", "ch", "h", "xi", "f", "p",
    };
    return v;
}

// Long-vowel normalisation. In Tokyo Japanese these grapheme pairs are
// realised as a single sustained /Vː/ rather than a V1-V2 diphthong
// (Vance 2008 §3.3, Saito 2014 on Tokyo long-vowel mergers):
//   ow + uw  ->  ow + ow    (王 ō, 東京 Tōkyō, 学校 gakkou)
//   eh + iy  ->  eh + eh    (先生 sensei, 時計 tokei)
// uw+uw, iy+iy are already long (chōonpu collapses to the same vowel).
// The aa+iy (kai 会, 階) and aa+uw (au 会う) sequences stay as
// diphthongs -- those are NOT merged in Tokyo speech.
//
// Both mora positions stay marked as nuclei (each "half" of a long
// vowel IS its own mora in Japanese phonology), so accent assignment
// over a long-vowel mora still works correctly: 王 ō (atamadaka, 2
// moras) produces ow+ow with H L pitches, giving the characteristic
// "ó↘ → low" fall.
void apply_long_vowel_normalization(std::vector<PhonemeItem>& items) {
    static const std::unordered_set<std::string> vowels = {
        "iy","ih","eh","aa","ah","ow","uw",
    };
    for (std::size_t k = 0; k + 1 < items.size(); ++k) {
        if (!vowels.count(items[k].key)) continue;
        if (!vowels.count(items[k + 1].key)) continue;
        const std::string& v1 = items[k].key;
        const std::string& v2 = items[k + 1].key;
        if (v1 == "ow" && v2 == "uw") items[k + 1].key = "ow";
        else if (v1 == "eh" && v2 == "iy") items[k + 1].key = "eh";
    }
}

void apply_high_vowel_devoicing(std::vector<PhonemeItem>& items) {
    if (items.empty()) return;
    static const std::unordered_set<std::string> vowels = {
        "iy","ih","eh","aa","ah","ow","uw",
    };
    std::vector<PhonemeItem> out;
    out.reserve(items.size());
    const auto& vless = voiceless_consonants_ja();
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        const bool is_high_vowel = (it.key == "iy" || it.key == "uw");
        if (is_high_vowel) {
            // Previous segment in `out` is the controlling consonant.
            const std::string prev = out.empty() ? "" : out.back().key;
            // Look at the IMMEDIATELY next item. If it's another vowel,
            // a sonorant, a voiced obstruent, or a moraic nasal, the
            // environment is voiced -- don't devoice. The old version
            // skipped past vowels in lookahead, which made the long
            // /uː/ in suu and the /u/ in sue devoice incorrectly.
            const std::string next = (i + 1 < items.size())
                ? items[i + 1].key : "sil";
            const bool next_is_voiced =
                vowels.count(next) > 0
                || next == "n" || next == "m" || next == "ng"
                || next == "r_ja" || next == "r"
                || next == "l" || next == "w" || next == "y"
                || next == "b" || next == "d" || next == "g"
                || next == "z" || next == "v" || next == "jh"
                || next == "dh" || next == "zh";
            const bool prev_voiceless = vless.count(prev) > 0;
            const bool at_phrase_end = it.phrase_final
                || next == "sil";
            // Devoice if BOTH preceding and following contexts are
            // voiceless (the canonical /i//u/-between-voiceless rule),
            // OR if the vowel sits at end of phrase after a voiceless
            // consonant (the canonical desu/masu word-final rule).
            // The word_final check is intentionally omitted: kana input
            // has no explicit word boundaries, and using it as a
            // trigger over-devoiced words mid-phrase.
            const bool next_voiceless_strict =
                !next_is_voiced && vless.count(next) > 0;
            const bool devoice =
                (prev_voiceless && next_voiceless_strict)
                || (prev_voiceless && at_phrase_end && it.key == "uw");
            if (devoice) {
                if (!out.empty()) {
                    if (it.word_final)   out.back().word_final = true;
                    if (it.phrase_final) out.back().phrase_final = true;
                }
                continue;
            }
        }
        out.push_back(it);
    }
    items = std::move(out);
}

// Count moras in a kana / romaji substring without actually emitting
// phonemes. Used by the dictionary tokenizer so each word knows its
// length for odaka (accent_pos == n_moras) classification.
int count_moras_kana(const std::string& s) {
    int count = 0;
    const auto& tbl = mora_table();
    std::size_t i = 0;
    while (i < s.size()) {
        if (is_sokuon(s, i) || is_chouonpu(s, i) || is_moraic_n(s, i)) {
            ++count; i += 3; continue;
        }
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 0x80 && i + 6 <= s.size() && is_yoon_extender(s, i + 3)
            && tbl.count(s.substr(i, 6))) {
            ++count; i += 6; continue;
        }
        if (c >= 0x80) {
            const int blen = utf8_len(c);
            if (tbl.count(s.substr(i, blen))) ++count;
            i += blen; continue;
        }
        if (is_romaji_letter(static_cast<char>(c))) {
            bool matched = false;
            for (int len = 3; len >= 1 && !matched; --len) {
                if (i + static_cast<std::size_t>(len) > s.size()) continue;
                bool all_letters = true;
                for (int k = 0; k < len; ++k) {
                    if (!is_romaji_letter(s[i + k])) {
                        all_letters = false; break;
                    }
                }
                if (!all_letters) continue;
                std::string tok = s.substr(i, len);
                for (char& cc : tok) cc = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(cc)));
                if (tbl.count(tok)) {
                    ++count; i += len; matched = true;
                }
            }
            if (!matched) ++i;
            continue;
        }
        ++i;
    }
    return count;
}

bool is_ascii_punct_or_space(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return uc == ' ' || uc == '\t' || uc == '\n' || uc == '\r'
        || c == '.' || c == ',' || c == '?' || c == '!'
        || c == ':' || c == ';';
}

struct WordSpan {
    std::size_t byte_start;
    std::size_t byte_end;
    int accent_pos;     // 0 = heiban, N>0 = kernel position
    int n_moras;
};

// Tokenize input into word spans using the accent dictionary. Skips
// ASCII punctuation/whitespace (the main emission loop handles those).
// Algorithm: at each position, try longest dict match first; if no
// match, accumulate codepoints into one "fallback heiban" span until
// the next dict match or punctuation.
std::vector<WordSpan> tokenize_japanese(const std::string& text) {
    const auto& dict = japanese_accent_dict();
    static std::size_t max_dict_bytes = 0;
    if (max_dict_bytes == 0) {
        for (const auto& [k, v] : dict) {
            (void)v;
            max_dict_bytes = std::max(max_dict_bytes, k.size());
        }
    }
    auto try_dict_at = [&](std::size_t i, std::size_t& out_len,
                           int& out_accent) {
        for (std::size_t len =
                std::min(max_dict_bytes, text.size() - i);
             len > 0; --len) {
            // Don't slice in the middle of a multi-byte codepoint.
            if (i + len < text.size()) {
                const unsigned char nc =
                    static_cast<unsigned char>(text[i + len]);
                if ((nc & 0xC0) == 0x80) continue;
            }
            auto it = dict.find(text.substr(i, len));
            if (it != dict.end()) {
                out_len = len;
                out_accent = it->second;
                return true;
            }
        }
        return false;
    };

    std::vector<WordSpan> spans;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (is_ascii_punct_or_space(c)) { ++i; continue; }
        std::size_t mlen = 0;
        int macc = 0;
        if (try_dict_at(i, mlen, macc)) {
            const std::string w = text.substr(i, mlen);
            spans.push_back({i, i + mlen, macc, count_moras_kana(w)});
            i += mlen;
            continue;
        }
        // Aggregate one fallback span until we hit a dict-matchable
        // position or punctuation. This keeps unknown content as ONE
        // heiban word rather than fragmenting it into single-mora L's.
        const std::size_t start = i;
        while (i < text.size()) {
            if (is_ascii_punct_or_space(text[i])) break;
            std::size_t mlen2 = 0; int macc2 = 0;
            if (try_dict_at(i, mlen2, macc2)) break;
            const unsigned char c2 = static_cast<unsigned char>(text[i]);
            i += (c2 >= 0x80) ? utf8_len(c2) : 1;
        }
        if (i > start) {
            const std::string w = text.substr(start, i - start);
            spans.push_back({start, i, 0, count_moras_kana(w)});
        }
    }
    return spans;
}

int compute_pitch(int mora_pos_1based, int accent_pos) {
    if (accent_pos == 0) return mora_pos_1based == 1 ? 0 : 1;   // heiban
    if (accent_pos == 1) return mora_pos_1based == 1 ? 1 : 0;   // atamadaka
    if (mora_pos_1based == 1) return 0;
    return mora_pos_1based <= accent_pos ? 1 : 0;
}

// Walk romaji greedily: try 3-char then 2-char then 1-char matches.
// `i` advances; returns true if a mora was emitted. Pushes each
// emitted phoneme through the caller-supplied callback (which records
// byte-position alongside).
template <typename Push>
bool try_romaji(const std::string& s, std::size_t& i, Push push) {
    const auto& tbl = mora_table();
    for (int len = 3; len >= 1; --len) {
        if (i + static_cast<std::size_t>(len) > s.size()) continue;
        bool all_letters = true;
        for (int k = 0; k < len; ++k) {
            if (!is_romaji_letter(s[i + k])) { all_letters = false; break; }
        }
        if (!all_letters) continue;
        std::string tok = s.substr(i, len);
        for (char& c : tok) c = static_cast<char>(std::tolower(
            static_cast<unsigned char>(c)));
        auto it = tbl.find(tok);
        if (it == tbl.end()) continue;
        for (const auto& key : it->second) {
            PhonemeItem item;
            item.key = key;
            item.duration_ms = -1.0;
            push(std::move(item));
        }
        i += static_cast<std::size_t>(len);
        return true;
    }
    return false;
}

}  // namespace

std::vector<PhonemeItem> japanese_text_to_phonemes(const std::string& text) {
    std::vector<PhonemeItem> out;
    std::vector<std::size_t> out_byte_start;   // parallel: source byte
    const auto& tbl = mora_table();

    bool pending_sokuon = false;          // gemination flag
    std::size_t pending_word_break = std::string::npos;
    std::size_t cur_unit_start = 0;       // updated each loop iteration

    // Wrapper that pushes to `out` while keeping byte-position tracking
    // in sync. Use this instead of direct push_back inside the main loop.
    auto push_item = [&](PhonemeItem item) {
        out.push_back(std::move(item));
        out_byte_start.push_back(cur_unit_start);
    };

    auto flush_word_break = [&]() {
        if (!out.empty()) out.back().word_final = true;
    };

    auto handle_sentence_punct = [&](double pause_ms, bool sent_break,
                                     ContourKind kind) {
        if (!out.empty()) {
            out.back().phrase_final = true;
            out.back().word_final = true;
        }
        PhonemeItem sil;
        sil.key = "sil";
        sil.duration_ms = pause_ms;
        sil.is_sentence_break = sent_break;
        sil.sentence_end_kind = kind;
        push_item(std::move(sil));
    };

    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        cur_unit_start = i;
        const unsigned char c = static_cast<unsigned char>(text[i]);

        // ASCII whitespace -> word boundary
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            flush_word_break();
            ++i;
            continue;
        }
        // ASCII punctuation: . , ? ! : ; -- text.cpp's CJK normalizer has
        // already mapped 。、？！：； to these.
        if (c == '.') { handle_sentence_punct(300.0, true, ContourKind::Statement); ++i; continue; }
        if (c == '?') { handle_sentence_punct(280.0, true, ContourKind::Question); ++i; continue; }
        if (c == '!') { handle_sentence_punct(280.0, true, ContourKind::Exclamation); ++i; continue; }
        if (c == ',' || c == ';' || c == ':') {
            handle_sentence_punct(180.0, false, ContourKind::Statement);
            ++i; continue;
        }

        // Sokuon -- queue gemination, don't emit anything yet
        if (is_sokuon(text, i)) {
            pending_sokuon = true;
            i += 3;
            continue;
        }

        // Chōonpu -- duplicate the previous vowel (we model this by
        // appending another copy of the last vowel mora's vowel key)
        if (is_chouonpu(text, i)) {
            for (std::size_t k = out.size(); k-- > 0; ) {
                if (vowel_phonemes().count(out[k].key)) {
                    PhonemeItem item;
                    item.key = out[k].key;
                    item.duration_ms = -1.0;
                    push_item(std::move(item));
                    break;
                }
            }
            i += 3;
            continue;
        }

        // Moraic n -- emit assimilated form by peeking at the next mora's
        // initial. Peek is best-effort; we choose plain "n" as fallback.
        if (is_moraic_n(text, i)) {
            // Scan ahead for the next phoneme key.
            std::string next_key = "";
            std::size_t j = i + 3;
            // skip sokuon if any
            if (j < n && is_sokuon(text, j)) j += 3;
            if (j < n) {
                // try kana mora
                for (int len = 2; len >= 1; --len) {
                    if (j + 3 * len > n) continue;
                    std::string tok = text.substr(j, 3 * len);
                    auto it = tbl.find(tok);
                    if (it != tbl.end() && !it->second.empty()) {
                        next_key = it->second.front();
                        break;
                    }
                }
                if (next_key.empty() && is_romaji_letter(text[j])) {
                    // peek romaji 3/2/1
                    for (int len = 3; len >= 1; --len) {
                        if (j + len > n) continue;
                        std::string tok = text.substr(j, len);
                        for (char& cc : tok) cc = static_cast<char>(
                            std::tolower(static_cast<unsigned char>(cc)));
                        auto it = tbl.find(tok);
                        if (it != tbl.end() && !it->second.empty()) {
                            next_key = it->second.front();
                            break;
                        }
                    }
                }
            }
            PhonemeItem item;
            item.key = assimilate_moraic_n(next_key);
            item.duration_ms = -1.0;
            push_item(std::move(item));
            i += 3;
            continue;
        }

        // Try yō-on first (3-byte base + 3-byte small kana)
        if (c >= 0x80 && i + 6 <= n && is_yoon_extender(text, i + 3)) {
            std::string tok = text.substr(i, 6);
            auto it = tbl.find(tok);
            if (it != tbl.end()) {
                if (pending_sokuon && !it->second.empty()) {
                    PhonemeItem geminate;
                    geminate.key = it->second.front();
                    geminate.duration_ms = 40.0;
                    push_item(std::move(geminate));
                    pending_sokuon = false;
                }
                for (const auto& key : it->second) {
                    PhonemeItem item;
                    item.key = key;
                    item.duration_ms = -1.0;
                    push_item(std::move(item));
                }
                i += 6;
                continue;
            }
        }
        // Single kana mora (1 codepoint)
        if (c >= 0x80) {
            const int blen = utf8_len(c);
            std::string tok = text.substr(i, blen);

            auto it = tbl.find(tok);
            if (it != tbl.end()) {
                if (pending_sokuon && !it->second.empty()) {
                    PhonemeItem geminate;
                    geminate.key = it->second.front();
                    geminate.duration_ms = 40.0;
                    push_item(std::move(geminate));
                    pending_sokuon = false;
                }
                for (const auto& key : it->second) {
                    PhonemeItem item;
                    item.key = key;
                    item.duration_ms = -1.0;
                    push_item(std::move(item));
                }
                i += blen;
                continue;
            }
            // Unknown CJK byte (kanji or other) -- skip silently.
            i += blen;
            continue;
        }

        // Romaji letter sequence
        if (is_romaji_letter(c)) {
            if (pending_sokuon) {
                // Buffer the romaji emit into a local vector, then prepend
                // a geminate-closure copy and push the lot through
                // push_item so byte-tracking stays consistent.
                std::vector<PhonemeItem> buf;
                std::size_t i_before = i;
                auto buf_push = [&](PhonemeItem item) {
                    buf.push_back(std::move(item));
                };
                if (try_romaji(text, i, buf_push)) {
                    if (!buf.empty()) {
                        PhonemeItem geminate;
                        geminate.key = buf.front().key;
                        geminate.duration_ms = 40.0;
                        push_item(std::move(geminate));
                        for (auto& it_buf : buf) push_item(std::move(it_buf));
                        pending_sokuon = false;
                    }
                } else {
                    (void)i_before;
                    ++i;
                }
                continue;
            }
            auto direct_push = [&](PhonemeItem item) {
                push_item(std::move(item));
            };
            if (!try_romaji(text, i, direct_push)) ++i;
            continue;
        }

        // Anything else: skip.
        ++i;
    }

    (void)pending_word_break;
    if (!out.empty()) {
        out.back().word_final = true;
        if (!out.back().phrase_final && out.back().key != "sil") {
            // Ensure terminal phrase-final flag for the duration model.
            out.back().phrase_final = true;
        }
    }

    apply_long_vowel_normalization(out);

    // Mark mora nuclei. A mora is either a vowel, or a MORAIC nasal
    // (the standalone ん). The bare /n/ /m/ /ng/ phonemes appear in two
    // very different roles in our stream:
    //   * onset C of a CV mora (e.g. な = n + aa)  -- NOT a nucleus
    //   * moraic-n / assimilated final              -- IS a nucleus
    // We distinguish by lookahead: if the NEXT phoneme is a vowel, the
    // nasal is an onset; otherwise it's moraic. Without this check the
    // word さんな san+na (3 moras) wrongly produced 4 mora nuclei and
    // the pitch contour shifted by one mora.
    {
        static const std::unordered_set<std::string> vowels = {
            "iy","ih","eh","aa","ah","ow","uw",
        };
        for (std::size_t k = 0; k < out.size(); ++k) {
            auto& it = out[k];
            if (vowels.count(it.key)) {
                it.mora_nucleus = true;
            } else if (it.key == "n" || it.key == "m" || it.key == "ng") {
                const bool next_is_vowel = (k + 1 < out.size())
                    && vowels.count(out[k + 1].key);
                if (!next_is_vowel) it.mora_nucleus = true;
            }
        }
    }

    // Pitch assignment using the accent dictionary. Tokenize the input
    // into word spans (dict matches + fallback heiban runs), then for
    // each mora nucleus look up which span its source byte sits in and
    // assign H/L by the span's accent pattern.
    {
        const auto spans = tokenize_japanese(text);
        // Resync size in case devoicing shortened `out`. We need
        // out_byte_start to still cover all current `out` items. The
        // devoicing pass only removes vowel items, never appends, so
        // we walk in parallel.
        std::vector<std::size_t> by(out_byte_start);
        if (by.size() != out.size()) {
            // Safety net: pad/truncate so the index math below stays sane.
            by.resize(out.size(), 0);
        }
        // For each span, find its mora nuclei and assign pitches.
        for (const auto& span : spans) {
            int mora_pos = 1;
            for (std::size_t k = 0; k < out.size(); ++k) {
                if (!out[k].mora_nucleus) continue;
                if (by[k] < span.byte_start || by[k] >= span.byte_end) continue;
                out[k].mora_pitch = compute_pitch(mora_pos, span.accent_pos);
                ++mora_pos;
            }
        }
    }

    apply_high_vowel_devoicing(out);

    return out;
}

}  // namespace klattalker
