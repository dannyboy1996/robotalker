// Embedded G2P + tokenisation.
//
// Strategy:
//   1. A small mini-dict of ~120 common English words is built in so
//      smoke tests work without any external file.
//   2. If found, the full CMU dict (cmudict-0.7b.txt plain text) is
//      loaded on first call and merged over the mini-dict. Lookup order:
//        $KLATTALKER_CMUDICT, ./cmudict-0.7b.txt, ./data/cmudict-0.7b.txt,
//        ../data/cmudict-0.7b.txt, ../share/klattalker/cmudict-0.7b.txt
//      Drop the full dict next to the executable for ~134K-word coverage.
//   3. For OOV words a rule-based letter-to-sound pass runs. It handles
//      common digraphs (ch/sh/th/ph/ng/qu), vowel digraphs (ai/ay/ee/ea/
//      oo/ou/ow/ie/oa/oi/oy/au/aw/ew), silent final-e + the magic-e
//      lengthening rule, and double-consonant collapse. Not as good as
//      a trained model but covers most English-shaped words.
//   4. Punctuation tokens are emitted as `sil` of varying duration so
//      the Klatt 1979 phrase-final lengthening rule fires correctly.
//
// Tokenisation also tags the *last vowel of the last word before a
// pause* as phrase-final, used by the duration model. We do this by
// emitting a final sil with a duration_ms that the sequencer interprets
// as the prosodic pause; the duration model sees it as a phrase break.

#include "klattalker/duration.hpp"
#include "klattalker/espeak.hpp"
#include "klattalker/japanese.hpp"
#include "klattalker/text.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace klattalker {

namespace {

// Inline-embedded CMU pronouncing dictionary. The build system writes
// generated/cmudict_embedded.inc with two `static` arrays the moment
// CMake configures; including it here keeps the data private to text.cpp
// and avoids linker whole-archive issues when libklattalker.a is
// consumed by the DLL / EXE targets.
#include "cmudict_embedded.inc"

struct CmuPhone {
    std::string key;
    int stress;
};
using Pron = std::vector<CmuPhone>;

const std::unordered_map<std::string, std::vector<std::string>>& arpabet_map() {
    // Diphthongs expand to nucleus + offglide so the formant trajectory
    // actually glides during the vowel (otherwise FACE/GOAT come out
    // as flat monophthongs — they USED to until we added iglide/uglide).
    //   EY (FACE)  -> [eɪ]  : ey + iglide
    //   OW (GOAT)  -> [oʊ]  : ow + uglide
    //   AY (PRICE) -> [aɪ]  : aa + iglide
    //   AW (MOUTH) -> [aʊ]  : aa + uglide
    //   OY (CHOICE)-> [ɔɪ]  : ao + iglide
    static const std::unordered_map<std::string, std::vector<std::string>> m = {
        {"AA", {"aa"}}, {"AE", {"ae"}}, {"AH", {"ah"}}, {"AO", {"ao"}},
        {"AW", {"aa", "uglide"}}, {"AY", {"aa", "iglide"}},
        {"B",  {"b"}},  {"CH", {"ch"}}, {"D",  {"d"}},  {"DH", {"dh"}},
        {"EH", {"eh"}}, {"ER", {"er"}}, {"EY", {"ey", "iglide"}},
        {"F",  {"f"}},  {"G",  {"g"}},  {"HH", {"h"}},
        {"IH", {"ih"}}, {"IY", {"iy"}}, {"JH", {"jh"}},
        {"K",  {"k"}},  {"L",  {"l"}},  {"M",  {"m"}},  {"N",  {"n"}},
        {"NG", {"ng"}}, {"OW", {"ow", "uglide"}}, {"OY", {"ao", "iglide"}},
        {"P",  {"p"}},  {"R",  {"r"}},  {"S",  {"s"}},  {"SH", {"sh"}},
        {"T",  {"t"}},  {"TH", {"th"}}, {"UH", {"uh"}}, {"UW", {"uw"}},
        {"V",  {"v"}},  {"W",  {"w"}},  {"Y",  {"y"}},  {"Z",  {"z"}},
        {"ZH", {"zh"}},
        // klattalker extension: alveolar trill /r/, used for Polish /r/
        // and similar trilled sounds in other languages.
        {"RR", {"rr"}},
        // klattalker extension: Polish alveolo-palatal sibilants.
        {"SJ", {"sj"}},
        {"ZJ", {"zj"}},
        {"CJ", {"cj"}},
        {"DJ", {"dj"}},
        // klattalker extension: RP /ɒ/ (LOT vowel, distinct from /aa/).
        {"OQ", {"oq"}},
    };
    return m;
}

Pron parse_cmu_pron(const std::string& line) {
    Pron out;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok) {
        int stress = 0;
        if (!tok.empty() && std::isdigit(static_cast<unsigned char>(tok.back()))) {
            stress = tok.back() - '0';
            tok.pop_back();
        }
        auto it = arpabet_map().find(tok);
        if (it == arpabet_map().end()) continue;
        for (std::size_t k = 0; k < it->second.size(); ++k) {
            CmuPhone p;
            p.key = it->second[k];
            p.stress = (k == 0) ? stress : 0;
            out.push_back(p);
        }
    }
    return out;
}

// ===========================================================================
// British English (RP) mini-dictionary
// ===========================================================================
//
// Covers the words needed for the "less uncanny than AI" paragraph plus
// a generous list of function words and common content words. Stress is
// marked the British way (penultimate fall on most polysyllables in
// neutral declarative reading). LOT vowel is OQ; the centring
// diphthongs /ɪə/ /eə/ /ʊə/ are approximated as ih+ah, eh+ah, uh+ah.
const std::unordered_map<std::string, Pron>& uk_dict() {
    static const std::unordered_map<std::string, std::string> raw = {
        // function words / pronouns / aux
        {"the", "DH AH0"}, {"a", "AH0"}, {"an", "AE1 N"},
        {"of", "AH1 V"}, {"to", "T UW1"}, {"and", "AE1 N D"},
        {"in", "IH0 N"}, {"on", "OQ1 N"}, {"at", "AE1 T"},
        {"for", "F AO1"}, {"with", "W IH1 DH"},
        {"is", "IH1 Z"}, {"it", "IH1 T"}, {"its", "IH1 T S"},
        {"i", "AY1"}, {"you", "Y UW1"}, {"he", "HH IY1"},
        {"she", "SH IY1"}, {"we", "W IY1"}, {"they", "DH EY1"},
        {"me", "M IY1"}, {"my", "M AY1"}, {"this", "DH IH1 S"},
        {"that", "DH AE1 T"}, {"these", "DH IY1 Z"},
        {"are", "AA1"}, {"was", "W OQ1 Z"}, {"were", "W ER1"},
        {"be", "B IY1"}, {"been", "B IY1 N"}, {"being", "B IY1 IH0 NG"},
        {"have", "HH AE1 V"}, {"has", "HH AE1 Z"}, {"had", "HH AE1 D"},
        {"do", "D UW1"}, {"does", "D AH1 Z"}, {"did", "D IH1 D"},
        {"not", "N OQ1 T"}, {"no", "N OW1"}, {"yes", "Y EH1 S"},
        {"so", "S OW1"}, {"too", "T UW1"}, {"very", "V EH1 R IY0"},
        {"but", "B AH1 T"}, {"or", "AO1"}, {"if", "IH1 F"},
        {"as", "AE1 Z"}, {"than", "DH AE1 N"},
        {"about", "AH0 B AW1 T"},
        {"how", "HH AW1"}, {"why", "W AY1"}, {"what", "W OQ1 T"},
        {"when", "W EH1 N"}, {"where", "W EH1 R"},
        {"there", "DH EH1 R"}, {"here", "HH IY1 R"},
        {"one", "W AH1 N"}, {"two", "T UW1"}, {"some", "S AH1 M"},
        // content words for the absurd paragraph
        {"one's", "W AH1 N Z"},
        {"oh", "OW1"},
        {"hello", "HH AH0 L OW1"}, {"world", "W ER1 L D"},
        {"computer", "K AH0 M P Y UW1 T AH0"},   // RP final -er = schwa
        {"computers", "K AH0 M P Y UW1 T AH0 Z"},
        {"voice", "V OY1 S"}, {"voices", "V OY1 S IH0 Z"},
        {"sound", "S AW1 N D"}, {"sounds", "S AW1 N D Z"},
        {"speak", "S P IY1 K"}, {"speech", "S P IY1 CH"},
        {"talk", "T AO1 K"}, {"talking", "T AO1 K IH0 NG"},
        {"robot", "R OW1 B OQ0 T"}, {"robotic", "R OW0 B OQ1 T IH0 K"},
        {"old", "OW1 L D"},
        {"fashioned", "F AE1 SH AH0 N D"},
        {"old-fashioned", "OW1 L D F AE1 SH AH0 N D"},
        {"machine", "M AH0 SH IY1 N"}, {"machines", "M AH0 SH IY1 N Z"},
        {"formant", "F AO1 M AH0 N T"}, {"formants", "F AO1 M AH0 N T S"},
        {"synthesiser", "S IH1 N TH AH0 S AY0 Z AH0"},
        {"synthesizer", "S IH1 N TH AH0 S AY0 Z AH0"},
        {"klatt", "K L AE1 T"},
        {"dear", "D IH1 R"}, {"oh", "OW1"},
        {"good", "G UH1 D"}, {"old", "OW1 L D"},
        {"chap", "CH AE1 P"}, {"chaps", "CH AE1 P S"},
        {"frightfully", "F R AY1 T F AH0 L IY0"},
        {"splendid", "S P L EH1 N D IH0 D"},
        {"absolutely", "AE2 B S AH0 L UW1 T L IY0"},
        {"marvellous", "M AA1 V AH0 L AH0 S"},
        {"rather", "R AA1 DH AH0"},
        {"quite", "K W AY1 T"},
        {"jolly", "JH OQ1 L IY0"},
        {"dreadfully", "D R EH1 D F AH0 L IY0"},
        {"terribly", "T EH1 R AH0 B L IY0"},
        {"awfully", "AO1 F AH0 L IY0"},
        {"posh", "P OQ1 SH"},
        {"frightening", "F R AY1 T AH0 N IH0 NG"},
        {"freaky", "F R IY1 K IY0"},
        {"strange", "S T R EY1 N JH"},
        {"uncanny", "AH0 N K AE1 N IY0"},
        {"valley", "V AE1 L IY0"},
        {"uncanny", "AH0 N K AE1 N IY0"},
        {"this", "DH IH1 S"}, {"that", "DH AE1 T"},
        {"sound", "S AW1 N D"}, {"sounds", "S AW1 N D Z"},
        {"like", "L AY1 K"},
        {"can", "K AE1 N"}, {"cannot", "K AE1 N OQ0 T"},
        {"will", "W IH1 L"}, {"would", "W UH1 D"}, {"could", "K UH1 D"},
        {"should", "SH UH1 D"}, {"might", "M AY1 T"},
        {"perfectly", "P ER1 F EH0 K T L IY0"},
        {"honest", "OQ1 N IH0 S T"}, {"honestly", "OQ1 N IH0 S T L IY0"},
        {"about", "AH0 B AW1 T"},
        {"itself", "IH0 T S EH1 L F"},
        {"pretend", "P R IH0 T EH1 N D"}, {"pretending", "P R IH0 T EH1 N D IH0 NG"},
        {"pretends", "P R IH0 T EH1 N D Z"},
        {"genuine", "JH EH1 N Y UW0 IH0 N"}, {"genuinely", "JH EH1 N Y UW0 IH0 N L IY0"},
        {"real", "R IY1 AH0 L"}, {"really", "R IH1 L IY0"},
        {"human", "HH Y UW1 M AH0 N"}, {"humans", "HH Y UW1 M AH0 N Z"},
        {"person", "P ER1 S AH0 N"}, {"people", "P IY1 P AH0 L"},
        {"artificial", "AA2 T IH0 F IH1 SH AH0 L"},
        {"intelligence", "IH0 N T EH1 L IH0 JH AH0 N S"},
        {"ai", "EY1 AY1"},
        {"learning", "L ER1 N IH0 NG"},
        {"deep", "D IY1 P"},
        {"modern", "M OQ1 D AH0 N"},
        {"vastly", "V AA1 S T L IY0"},
        {"superior", "S UW0 P IH1 R IY0 AH0"},
        {"warm", "W AO1 M"}, {"warmer", "W AO1 M AH0"},
        {"colder", "K OW1 L D AH0"},
        {"convincing", "K AH0 N V IH1 N S IH0 NG"},
        {"convince", "K AH0 N V IH1 N S"},
        {"freaked", "F R IY1 K T"}, {"freaks", "F R IY1 K S"},
        {"out", "AW1 T"},
        {"folk", "F OW1 K"}, {"folks", "F OW1 K S"},
        {"refreshing", "R IH0 F R EH1 SH IH0 NG"},
        {"refreshingly", "R IH0 F R EH1 SH IH0 NG L IY0"},
        {"days", "D EY1 Z"}, {"age", "EY1 JH"},
        {"these", "DH IY1 Z"},
        {"unmistakably", "AH0 N M IH0 S T EY1 K AH0 B L IY0"},
        {"unashamedly", "AH0 N AH0 SH EY1 M IH0 D L IY0"},
        {"chap", "CH AE1 P"},
        {"box", "B OQ1 K S"}, {"boxes", "B OQ1 K S IH0 Z"},
        {"hot", "HH OQ1 T"}, {"hat", "HH AE1 T"}, {"hut", "HH AH1 T"},
        {"car", "K AA1"},   // non-rhotic
        {"farm", "F AA1 M"},
        {"course", "K AO1 S"},
        {"sort", "S AO1 T"},
        {"thing", "TH IH1 NG"}, {"things", "TH IH1 NG Z"},
        {"frankly", "F R AE1 NG K L IY0"},
        {"vastly", "V AA1 S T L IY0"},
        {"opinion", "AH0 P IH1 N Y AH0 N"},
        {"good", "G UH1 D"},
        {"thoroughly", "TH AH1 R AH0 L IY0"},
        {"jolly", "JH OQ1 L IY0"},
        {"is", "IH1 Z"}, {"its", "IH1 T S"},
        {"more", "M AO1"},
        {"less", "L EH1 S"},
        {"way", "W EY1"}, {"way's", "W EY1 Z"},
        {"because", "B IH0 K OQ1 Z"},
        {"sort", "S AO1 T"},
    };
    static std::unordered_map<std::string, Pron> dict;
    static std::once_flag once;
    std::call_once(once, [] {
        for (const auto& [w, p] : raw) dict[w] = parse_cmu_pron(p);
    });
    return dict;
}

const std::unordered_map<std::string, Pron>& builtin_dict() {
    static const std::unordered_map<std::string, std::string> raw = {
        {"hello",   "HH AH0 L OW1"},
        {"world",   "W ER1 L D"},
        {"are",     "AA1 R"},
        {"you",     "Y UW1"},
        {"ready",   "R EH1 D IY0"},
        {"is",      "IH1 Z"},
        {"it",      "IH1 T"},
        {"true",    "T R UW1"},
        {"the",     "DH AH0"},
        {"a",       "AH0"},
        {"and",     "AE1 N D"},
        {"of",      "AH1 V"},
        {"to",      "T UW1"},
        {"in",      "IH0 N"},
        {"on",      "AA1 N"},
        {"at",      "AE1 T"},
        {"i",       "AY1"},
        {"my",      "M AY1"},
        {"this",    "DH IH1 S"},
        {"that",    "DH AE1 T"},
        {"with",    "W IH1 DH"},
        {"have",    "HH AE1 V"},
        {"has",     "HH AE1 Z"},
        {"do",      "D UW1"},
        {"does",    "D AH1 Z"},
        {"did",     "D IH1 D"},
        {"can",     "K AE1 N"},
        {"will",    "W IH1 L"},
        {"would",   "W UH1 D"},
        {"should",  "SH UH1 D"},
        {"could",   "K UH1 D"},
        {"how",     "HH AW1"},
        {"why",     "W AY1"},
        {"who",     "HH UW1"},
        {"what",    "W AH1 T"},
        {"when",    "W EH1 N"},
        {"where",   "W EH1 R"},
        {"there",   "DH EH1 R"},
        {"here",    "HH IY1 R"},
        {"help",    "HH EH1 L P"},
        {"understand", "AH2 N D ER0 S T AE1 N D"},
        {"me",      "M IY1"},
        {"good",    "G UH1 D"},
        {"bad",     "B AE1 D"},
        {"yes",     "Y EH1 S"},
        {"no",      "N OW1"},
        {"see",     "S IY1"},
        {"saw",     "S AO1"},
        {"say",     "S EY1"},
        {"says",    "S EH1 Z"},
        {"go",      "G OW1"},
        {"goes",    "G OW1 Z"},
        {"come",    "K AH1 M"},
        {"klatt",   "K L AE1 T"},
        {"synth",   "S IH1 N TH"},
        {"speech",  "S P IY1 CH"},
        {"voice",   "V OY1 S"},
        {"voiced",  "V OY1 S T"},
        {"sound",   "S AW1 N D"},
        {"vowel",   "V AW1 AH0 L"},
        {"male",    "M EY1 L"},
        {"female",  "F IY1 M EY0 L"},
        {"test",    "T EH1 S T"},
        {"time",    "T AY1 M"},
        {"day",     "D EY1"},
        {"night",   "N AY1 T"},
        {"work",    "W ER1 K"},
        {"works",   "W ER1 K S"},
        {"english", "IH1 NG G L IH0 SH"},
        // pangram support
        {"she",     "SH IY1"},
        {"sells",   "S EH1 L Z"},
        {"sea",     "S IY1"},
        {"seashells","S IY1 SH EH2 L Z"},
        {"by",      "B AY1"},
        {"seashore","S IY1 SH AO2 R"},
        {"quick",   "K W IH1 K"},
        {"brown",   "B R AW1 N"},
        {"fox",     "F AA1 K S"},
        {"jumps",   "JH AH1 M P S"},
        {"over",    "OW1 V ER0"},
        {"lazy",    "L EY1 Z IY0"},
        {"dog",     "D AO1 G"},
        // phonetic-coverage pangram
        {"pack",    "P AE1 K"},
        {"box",     "B AA1 K S"},
        {"five",    "F AY1 V"},
        {"dozen",   "D AH1 Z AH0 N"},
        {"liquor",  "L IH1 K ER0"},
        {"jugs",    "JH AH1 G Z"},
        // Polish test words. /rr/ is our Polish alveolar trill, distinct
        // from the English approximant /r/. Vowels mapped to the closest
        // English ARPABET equivalent until a proper Polish vowel table
        // lands. Stress goes on the penultimate syllable (Polish default).
        {"robert",       "RR AA1 B EH0 RR T"},
        {"kurcze",       "K UW1 RR CH EH0"},
        {"kurczę",       "K UW1 RR CH EH0"},
        {"programowanie","P RR OW0 G RR AA0 M OW0 V AA1 N Y EH0"},
        {"trzcina",      "T RR CH IY1 N AA0"},
        {"trrr",         "T RR IY1"},
    };
    static std::unordered_map<std::string, Pron> dict;
    static std::once_flag once;
    std::call_once(once, [] {
        for (const auto& [w, p] : raw) dict[w] = parse_cmu_pron(p);
    });
    return dict;
}

// Parse a cmudict-formatted stream (text or embedded) into the given
// map. Skips comments, handles alt-pron markers like HELLO(1), keeps
// the first pronunciation seen for each word.
void parse_cmudict_stream(std::istream& f,
                           std::unordered_map<std::string, Pron>& dict) {
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == ';') continue;
        if (line.size() >= 3 && line[0] == ';' && line[1] == ';' && line[2] == ';')
            continue;
        std::istringstream iss(line);
        std::string word;
        if (!(iss >> word)) continue;
        std::string pron, rest;
        while (iss >> rest) {
            if (!pron.empty()) pron.push_back(' ');
            pron += rest;
        }
        auto paren = word.find('(');
        if (paren != std::string::npos) word.resize(paren);
        std::transform(word.begin(), word.end(), word.begin(),
            [](unsigned char c) { return std::tolower(c); });
        if (!dict.count(word)) dict[word] = parse_cmu_pron(pron);
    }
}

const std::unordered_map<std::string, Pron>& disk_dict() {
    static std::unordered_map<std::string, Pron> dict;
    static std::once_flag once;
    std::call_once(once, [] {
        // First try external paths (lets the user override with a custom
        // dict by dropping it next to the exe).
        std::vector<std::string> candidates;
        if (const char* env = std::getenv("KLATTALKER_CMUDICT"))
            candidates.push_back(env);
        if (const char* env = std::getenv("ROBOTALKER_CMUDICT"))
            candidates.push_back(env);
        candidates.push_back("cmudict-0.7b.txt");
        candidates.push_back("data/cmudict-0.7b.txt");
        candidates.push_back("../data/cmudict-0.7b.txt");
        candidates.push_back("../share/klattalker/cmudict-0.7b.txt");

        std::ifstream f;
        for (const auto& path : candidates) {
            f.open(path);
            if (f) {
                parse_cmudict_stream(f, dict);
                return;
            }
            f.clear();
        }

        // Fall back to the embedded dict baked into the binary at
        // build time. End users never need an external cmudict file.
        if (kEmbeddedCmudictSize > 0) {
            std::string s(kEmbeddedCmudict, kEmbeddedCmudictSize);
            std::istringstream iss(std::move(s));
            parse_cmudict_stream(iss, dict);
        }
    });
    return dict;
}

// forward decl for the suffix-stripping rules below
bool is_vowel_char(char c);

const Pron* lookup_direct(const std::string& word) {
    auto& dk = disk_dict();   // prefer real CMU when available
    auto it = dk.find(word);
    if (it != dk.end()) return &it->second;
    auto& bi = builtin_dict();
    auto it2 = bi.find(word);
    if (it2 != bi.end()) return &it2->second;
    return nullptr;
}

// MITalk-style morphological decomposition: strip a common English
// suffix, look up the stem, and append the suffix pronunciation. Covers
// most inflected forms missing from the dict (-s, -es, -ed, -ing, -ly,
// -er, -est, -ness, -ment, -tion, -ful, -less). Allen/Hunnicutt/Klatt
// 1987 §4 builds out a richer version of this.
struct SuffixRule {
    std::string suffix;            // letter form
    std::string suffix_phones;     // ARPABET (used by parse_cmu_pron)
    // optional: voiced-final variant (for -s/-ed)
    std::string suffix_phones_voiced;
};

const std::vector<SuffixRule>& suffix_rules() {
    static const std::vector<SuffixRule> r = {
        {"tions","SH AH0 N Z",""},
        {"tion", "SH AH0 N", ""},
        {"ments","M AH0 N T S",""},
        {"ment", "M AH0 N T", ""},
        {"ness", "N AH0 S", ""},
        {"less", "L AH0 S", ""},
        {"ful",  "F AH0 L", ""},
        {"ly",   "L IY0", ""},
        {"ings", "IH0 NG Z", ""},
        {"ing",  "IH0 NG", ""},
        {"est",  "IH0 S T", ""},
        {"er",   "ER0", ""},
        {"ed",   "T", "D"},     // unvoiced/voiced choice based on stem end
        {"es",   "Z", ""},
        {"s",    "S", "Z"},     // unvoiced/voiced choice
    };
    return r;
}

bool stem_ends_voiced(const Pron& stem) {
    if (stem.empty()) return true;
    const std::string& k = stem.back().key;
    // unvoiced obstruents: p, t, k, f, th, s, sh, ch, h
    if (k == "p" || k == "t" || k == "k" || k == "f"
        || k == "th" || k == "s" || k == "sh" || k == "ch" || k == "h")
        return false;
    return true;
}

bool stem_ends_alveolar_stop(const Pron& stem) {
    if (stem.empty()) return false;
    const std::string& k = stem.back().key;
    return k == "t" || k == "d";
}

bool stem_ends_sibilant(const Pron& stem) {
    if (stem.empty()) return false;
    const std::string& k = stem.back().key;
    return k == "s" || k == "z" || k == "sh" || k == "zh"
        || k == "ch" || k == "jh";
}

const Pron* lookup(const std::string& word) {
    if (auto* p = lookup_direct(word)) return p;

    // Try suffix stripping. We return a pointer to a thread-local cache
    // so the caller's Pron* stays valid for the rest of the lookup.
    thread_local std::unordered_map<std::string, Pron> suffix_cache;
    auto cached = suffix_cache.find(word);
    if (cached != suffix_cache.end()) return &cached->second;

    for (const auto& rule : suffix_rules()) {
        if (word.size() <= rule.suffix.size()) continue;
        if (word.compare(word.size() - rule.suffix.size(),
                         rule.suffix.size(), rule.suffix) != 0)
            continue;
        std::string stem = word.substr(0, word.size() - rule.suffix.size());
        // Common English re-spelling: -ies <- -y + es, -ied <- -y + ed
        // Restore final 'y' when the suffix begins with 'i' or 'e' and
        // the stem ends in a consonant.
        if (!stem.empty() && (rule.suffix.front() == 'i' || rule.suffix.front() == 'e')
            && stem.back() != 'y'
            && !is_vowel_char(stem.back())) {
            std::string with_y = stem + "y";
            if (lookup_direct(with_y)) stem = with_y;
        }
        // -ed/-es preceded by -e- need an extra 'e' on the stem
        if (rule.suffix == "ed" || rule.suffix == "es") {
            std::string with_e = stem + "e";
            if (lookup_direct(with_e)) stem = with_e;
        }
        const Pron* stem_pron = lookup_direct(stem);
        if (!stem_pron) continue;

        Pron full = *stem_pron;
        // Choose voicing variant
        std::string phones = rule.suffix_phones;
        if (!rule.suffix_phones_voiced.empty()) {
            if (rule.suffix == "s" || rule.suffix == "ed") {
                phones = stem_ends_voiced(full)
                    ? rule.suffix_phones_voiced : rule.suffix_phones;
            }
        }
        // -s after sibilant -> /IH0 Z/ (insertion rule)
        if (rule.suffix == "s" && stem_ends_sibilant(full)) {
            phones = "IH0 Z";
        }
        // -ed after alveolar stop -> /IH0 D/
        if (rule.suffix == "ed" && stem_ends_alveolar_stop(full)) {
            phones = "IH0 D";
        }

        Pron suf = parse_cmu_pron(phones);
        for (auto& p : suf) full.push_back(p);
        auto& slot = suffix_cache[word];
        slot = std::move(full);
        return &slot;
    }
    return nullptr;
}

// --- Improved LTS fallback -------------------------------------------------
//
// Handles:
//   * digraphs: ch, sh, th, ph, gh (silent), wh, ng, qu, ck
//   * vowel digraphs: ai, ay, ee, ea, oo, ou, ow, ie, oa, oi, oy,
//                     au, aw, ew, ue, ui, ar, or, ir, ur, er
//   * silent final-e + magic-e: "ate" -> /eyt/, "bite" -> /bayt/
//   * double consonants (collapse)
//   * c->s before e/i/y, c->k elsewhere
//   * g->jh before e/i/y, g->g elsewhere
//
// First-vowel-encountered gets stress=1 (so something receives the
// pitch accent); later vowels are unstressed.

bool is_vowel_char(char c) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

bool has_magic_e(const std::string& w, std::size_t vowel_pos) {
    // vowel ... single consonant ... final e
    if (vowel_pos + 3 != w.size()) return false;
    if (w.back() != 'e') return false;
    return !is_vowel_char(w[vowel_pos + 1]) && !is_vowel_char(w[vowel_pos + 2]);
}

Pron lts_fallback(const std::string& w) {
    Pron out;
    bool stressed_taken = false;
    auto push_v = [&](const char* k) {
        CmuPhone p; p.key = k;
        p.stress = stressed_taken ? 0 : 1;
        stressed_taken = true;
        out.push_back(p);
    };
    auto push_c = [&](const char* k) {
        CmuPhone p; p.key = k; p.stress = 0; out.push_back(p);
    };

    const std::size_t n = w.size();
    auto ch = [&](std::size_t i) -> char {
        return (i < n) ? static_cast<char>(
                            std::tolower(static_cast<unsigned char>(w[i])))
                       : 0;
    };

    std::size_t i = 0;
    while (i < n) {
        const char c = ch(i);
        const char c2 = ch(i + 1);

        // consonant digraphs first (longest match)
        if (c == 'c' && c2 == 'h') { push_c("ch"); i += 2; continue; }
        if (c == 's' && c2 == 'h') { push_c("sh"); i += 2; continue; }
        if (c == 't' && c2 == 'h') { push_c("th"); i += 2; continue; }
        if (c == 'p' && c2 == 'h') { push_c("f");  i += 2; continue; }
        if (c == 'w' && c2 == 'h') { push_c("w");  i += 2; continue; }
        if (c == 'g' && c2 == 'h') { /* silent */    i += 2; continue; }
        if (c == 'c' && c2 == 'k') { push_c("k");  i += 2; continue; }
        if (c == 'q' && c2 == 'u') { push_c("k"); push_c("w"); i += 2; continue; }
        if (c == 'n' && c2 == 'g' && (i + 2 == n || !is_vowel_char(ch(i + 2)))) {
            push_c("ng"); i += 2; continue;
        }
        // double consonants -> single
        if (!is_vowel_char(c) && c == c2 && c != 'e' && c != 'o') {
            // emit once, skip both
            // fall through to single-char handling but skip the next
            // (handled below)
        }

        // vowel digraphs
        if (c == 'a' && c2 == 'i') { push_v("ey"); i += 2; continue; }
        if (c == 'a' && c2 == 'y') { push_v("ey"); i += 2; continue; }
        if (c == 'e' && c2 == 'e') { push_v("iy"); i += 2; continue; }
        if (c == 'e' && c2 == 'a') { push_v("iy"); i += 2; continue; }
        if (c == 'i' && c2 == 'e') { push_v("iy"); i += 2; continue; }
        if (c == 'o' && c2 == 'a') { push_v("ow"); i += 2; continue; }
        if (c == 'o' && c2 == 'o') { push_v("uw"); i += 2; continue; }
        if (c == 'o' && c2 == 'u') { push_v("aa"); push_c("w"); i += 2; continue; }
        if (c == 'o' && c2 == 'w') { push_v("ow"); i += 2; continue; }
        if (c == 'o' && c2 == 'i') { push_v("ao"); push_c("y"); i += 2; continue; }
        if (c == 'o' && c2 == 'y') { push_v("ao"); push_c("y"); i += 2; continue; }
        if (c == 'a' && c2 == 'u') { push_v("ao"); i += 2; continue; }
        if (c == 'a' && c2 == 'w') { push_v("ao"); i += 2; continue; }
        if (c == 'e' && c2 == 'w') { push_v("uw"); i += 2; continue; }
        if (c == 'u' && c2 == 'e') { push_v("uw"); i += 2; continue; }
        if (c == 'u' && c2 == 'i') { push_v("uw"); i += 2; continue; }

        // r-coloured vowels
        if (c == 'a' && c2 == 'r') { push_v("aa"); push_c("r"); i += 2; continue; }
        if (c == 'o' && c2 == 'r') { push_v("ao"); push_c("r"); i += 2; continue; }
        if (c == 'i' && c2 == 'r') { push_v("er"); i += 2; continue; }
        if (c == 'u' && c2 == 'r') { push_v("er"); i += 2; continue; }
        if (c == 'e' && c2 == 'r') { push_v("er"); i += 2; continue; }

        // single vowels with magic-e or default short value
        if (is_vowel_char(c)) {
            // magic-e: vowel + single consonant + final 'e' -> long vowel
            const bool magic = has_magic_e(w, i);
            switch (c) {
                case 'a': push_v(magic ? "ey" : "ae"); break;
                case 'e':
                    if (i == n - 1) { /* silent final e */ }
                    else            push_v(magic ? "iy" : "eh");
                    break;
                case 'i': push_v(magic ? "aa" : "ih");
                          if (magic) push_c("y");
                          break;
                case 'o': push_v(magic ? "ow" : "aa"); break;
                case 'u': push_v(magic ? "uw" : "ah"); break;
                default: break;
            }
            ++i;
            continue;
        }

        // single consonants
        switch (c) {
            case 'b': push_c("b"); break;
            case 'c': {
                // c->s before e/i/y; c->k otherwise
                const char nxt = ch(i + 1);
                if (nxt == 'e' || nxt == 'i' || nxt == 'y') push_c("s");
                else push_c("k");
                break;
            }
            case 'd': push_c("d"); break;
            case 'f': push_c("f"); break;
            case 'g': {
                const char nxt = ch(i + 1);
                if (nxt == 'e' || nxt == 'i' || nxt == 'y') push_c("jh");
                else push_c("g");
                break;
            }
            case 'h': push_c("h"); break;
            case 'j': push_c("jh"); break;
            case 'k': push_c("k"); break;
            case 'l': push_c("l"); break;
            case 'm': push_c("m"); break;
            case 'n': push_c("n"); break;
            case 'p': push_c("p"); break;
            case 'r': push_c("r"); break;
            case 's': push_c("s"); break;
            case 't': push_c("t"); break;
            case 'v': push_c("v"); break;
            case 'w': push_c("w"); break;
            case 'x': push_c("k"); push_c("s"); break;
            case 'y':
                if (i == 0) push_c("y");
                else if (i == n - 1) push_v("iy");
                else push_v("ih");
                break;
            case 'z': push_c("z"); break;
            default: break;
        }
        // double consonant collapse
        if (i + 1 < n && c == c2 && !is_vowel_char(c)) ++i;
        ++i;
    }
    return out;
}

// ===========================================================================
// Polish rule-based G2P
// ===========================================================================
//
// Polish orthography is highly regular -- nearly one-to-one to phonemes
// with a handful of digraph rules. The mapping below covers all the
// graphemes a learner would see in everyday text. Stress is placed on
// the penultimate syllable (the default Polish rule); we approximate
// "syllable" as the vowel-bearing chunk and assign primary stress to
// the second-to-last vowel of each word.
//
// References: Jassem 2003 (JIPA Illustration of Polish), Wierzchowska
// 1980 §1, Sawicka 1995.

bool pl_is_vowel_letter(char c) {
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'
        || c == 'y';
}

Pron pl_g2p(const std::string& w_utf8) {
    // We operate on a normalised, lower-case representation. Polish
    // letters with diacritics arrive as 2-byte UTF-8 sequences -- we
    // translate them to short ASCII tags so the rule engine can pattern
    // match cheaply. Mapping:
    //   ą -> @       (nasal o)
    //   ę -> 3       (nasal e)
    //   ó -> 6       (same sound as u)
    //   ł -> &       (English-like /w/)
    //   ń -> +       (palatal n)
    //   ś -> 1       (alveolo-palatal s)
    //   ź -> 2       (alveolo-palatal z)
    //   ć -> 4       (alveolo-palatal ts)
    //   ż -> 7       (retroflex z)
    std::string s;
    s.reserve(w_utf8.size());
    for (std::size_t i = 0; i < w_utf8.size(); ) {
        unsigned char c = w_utf8[i];
        if (c < 0x80) {
            s.push_back(static_cast<char>(std::tolower(c)));
            i += 1;
            continue;
        }
        // UTF-8 2-byte
        if (i + 1 < w_utf8.size()) {
            const unsigned char c1 = c;
            const unsigned char c2 = w_utf8[i + 1];
            const unsigned int cp = ((c1 & 0x1F) << 6) | (c2 & 0x3F);
            char tag = 0;
            switch (cp) {
                case 0x0105: tag = '@'; break; // ą
                case 0x0119: tag = '3'; break; // ę
                case 0x00F3: tag = '6'; break; // ó
                case 0x0142: tag = '&'; break; // ł
                case 0x0144: tag = '+'; break; // ń
                case 0x015B: tag = '1'; break; // ś
                case 0x017A: tag = '2'; break; // ź
                case 0x0107: tag = '4'; break; // ć
                case 0x017C: tag = '7'; break; // ż
                case 0x0104: tag = '@'; break; // Ą
                case 0x0118: tag = '3'; break; // Ę
                case 0x00D3: tag = '6'; break; // Ó
                case 0x0141: tag = '&'; break; // Ł
                case 0x0143: tag = '+'; break; // Ń
                case 0x015A: tag = '1'; break; // Ś
                case 0x0179: tag = '2'; break; // Ź
                case 0x0106: tag = '4'; break; // Ć
                case 0x017B: tag = '7'; break; // Ż
                default: break;
            }
            if (tag) {
                s.push_back(tag);
                i += 2;
                continue;
            }
        }
        // unknown -- skip
        i += 1;
    }

    Pron out;
    auto emit = [&](const char* k, int stress = 0) {
        CmuPhone p; p.key = k; p.stress = stress; out.push_back(p);
    };

    // First pass: emit phonemes with stress=0; the second pass assigns
    // penultimate-syllable stress.
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = s[i];
        const char c2 = (i + 1 < n) ? s[i + 1] : 0;
        const char c3 = (i + 2 < n) ? s[i + 2] : 0;

        // multi-letter digraphs (longest match first)
        if (c == 'd' && c2 == 'z' && c3 == 'i') {
            emit("dj"); i += 3; continue;
        }
        if (c == 'd' && c2 == 'z') { emit("d"); emit("z"); i += 2; continue; }
        if (c == 'd' && c2 == '7') { emit("jh"); i += 2; continue; } // dż
        if (c == 'd' && c2 == '2') { emit("dj"); i += 2; continue; } // dź
        if (c == 'c' && c2 == 'z') { emit("ch"); i += 2; continue; }
        if (c == 'c' && c2 == 'h') { emit("h"); i += 2; continue; }
        if (c == 's' && c2 == 'z') { emit("sh"); i += 2; continue; }
        if (c == 'r' && c2 == 'z') { emit("zh"); i += 2; continue; }
        if (c == 'c' && c2 == 'i') { emit("cj"); /* keep 'i' if followed by vowel */
            if (c3 && pl_is_vowel_letter(c3)) { i += 2; continue; }
            i += 2; continue;
        }
        if (c == 's' && c2 == 'i' && c3 && pl_is_vowel_letter(c3)) {
            emit("sj"); i += 2; continue;
        }
        if (c == 'z' && c2 == 'i' && c3 && pl_is_vowel_letter(c3)) {
            emit("zj"); i += 2; continue;
        }
        if (c == 'n' && c2 == 'i' && c3 && pl_is_vowel_letter(c3)) {
            emit("n"); /* palatalisation absorbed into next vowel */ i += 2;
            continue;
        }

        // Polish nasal vowels: collapse to oral vowel + nasal coda before
        // an obstruent, or vowel + nasal glide in coda.
        if (c == '@') { emit("ao"); emit("n"); i += 1; continue; }
        if (c == '3') { emit("eh"); emit("n"); i += 1; continue; }

        // single special letters
        switch (c) {
            case 'a': emit("aa"); i += 1; continue;
            case 'e': emit("eh"); i += 1; continue;
            case 'i': emit("iy"); i += 1; continue;
            case 'o': emit("ao"); i += 1; continue;
            case 'u': emit("uw"); i += 1; continue;
            case 'y': emit("ih"); i += 1; continue;
            case '6': emit("uw"); i += 1; continue;   // ó
            case '&': emit("w");  i += 1; continue;   // ł
            case '1': emit("sj"); i += 1; continue;   // ś
            case '2': emit("zj"); i += 1; continue;   // ź
            case '4': emit("cj"); i += 1; continue;   // ć
            case '7': emit("zh"); i += 1; continue;   // ż
            case '+': emit("n");  i += 1; continue;   // ń
            // straightforward consonants
            case 'b': emit("b");  i += 1; continue;
            case 'c': emit("t");  emit("s"); i += 1; continue;  // c -> /ts/
            case 'd': emit("d");  i += 1; continue;
            case 'f': emit("f");  i += 1; continue;
            case 'g': emit("g");  i += 1; continue;
            case 'h': emit("h");  i += 1; continue;
            case 'j': emit("y");  i += 1; continue;
            case 'k': emit("k");  i += 1; continue;
            case 'l': emit("l");  i += 1; continue;
            case 'm': emit("m");  i += 1; continue;
            case 'n': emit("n");  i += 1; continue;
            case 'p': emit("p");  i += 1; continue;
            case 'r': emit("rr"); i += 1; continue;  // Polish trill!
            case 's': emit("s");  i += 1; continue;
            case 't': emit("t");  i += 1; continue;
            case 'v': emit("v");  i += 1; continue;
            case 'w': emit("v");  i += 1; continue;  // Polish 'w' = /v/
            case 'x': emit("k");  emit("s"); i += 1; continue;
            case 'z': emit("z");  i += 1; continue;
            default: i += 1; continue;
        }
    }

    // Stress assignment: primary stress on the penultimate vowel.
    std::vector<std::size_t> vowel_indices;
    static const std::unordered_set<std::string> pl_vowel_keys = {
        "iy","ih","eh","aa","ao","uw","ah"
    };
    for (std::size_t k = 0; k < out.size(); ++k) {
        if (pl_vowel_keys.count(out[k].key)) vowel_indices.push_back(k);
    }
    if (!vowel_indices.empty()) {
        std::size_t stress_idx;
        if (vowel_indices.size() == 1) {
            stress_idx = vowel_indices.back();
        } else {
            stress_idx = vowel_indices[vowel_indices.size() - 2];
        }
        out[stress_idx].stress = 1;
    }
    return out;
}

// Insert a glottal stop /q/ before each vowel that's either utterance-
// initial, post-pausal, or in vowel-vowel hiatus across a word
// boundary. Real English speakers do this -- "an apple" stays smooth
// (linked through /n/), but "the apple" and a sentence-initial vowel
// both get the characteristic creaky onset.
const std::unordered_set<std::string>& vowel_keys_for_glottal() {
    static const std::unordered_set<std::string> v = {
        "iy","ih","ey","eh","ae","aa","ao","ow","oq",
        "uh","uw","ah","er",
    };
    return v;
}

void insert_glottal_stops(std::vector<PhonemeItem>& items) {
    const auto& vowels = vowel_keys_for_glottal();
    std::vector<PhonemeItem> out;
    out.reserve(items.size() + items.size() / 8);
    bool needs_glottal = true;   // utterance starts -> insert before vowel
    for (auto& it : items) {
        const bool is_vowel = vowels.count(it.key) > 0;
        if (is_vowel && needs_glottal) {
            PhonemeItem q;
            q.key = "q";
            q.duration_ms = -1.0;
            out.push_back(std::move(q));
        }
        if (it.key == "sil") {
            needs_glottal = true;            // after any pause
        } else if (is_vowel) {
            needs_glottal = it.word_final;   // V#V hiatus -> /q/ next time
        } else {
            needs_glottal = false;
        }
        out.push_back(std::move(it));
    }
    items = std::move(out);
}

// Normalize CJK / fullwidth punctuation to the ASCII equivalents the
// chunking loops below understand. Without this, Chinese sentences end
// in 。 (U+3002) which is three UTF-8 bytes -- the single-char loops
// just see them as opaque content and never break the sentence,
// producing one giant utterance with no terminal contour. Maps:
//   。 (U+3002) -> '.'    ， (U+FF0C) -> ','    ？ (U+FF1F) -> '?'
//   ！ (U+FF01) -> '!'    、 (U+3001) -> ','    ： (U+FF1A) -> ':'
//   ； (U+FF1B) -> ';'    … (U+2026) -> '.'
std::string normalize_cjk_punct(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        if (i + 2 < text.size()) {
            const unsigned char a = static_cast<unsigned char>(text[i]);
            const unsigned char b = static_cast<unsigned char>(text[i + 1]);
            const unsigned char c = static_cast<unsigned char>(text[i + 2]);
            char repl = 0;
            if (a == 0xE3 && b == 0x80 && c == 0x82) repl = '.';
            else if (a == 0xE3 && b == 0x80 && c == 0x81) repl = ',';
            else if (a == 0xEF && b == 0xBC && c == 0x8C) repl = ',';
            else if (a == 0xEF && b == 0xBC && c == 0x9F) repl = '?';
            else if (a == 0xEF && b == 0xBC && c == 0x81) repl = '!';
            else if (a == 0xEF && b == 0xBC && c == 0x9A) repl = ':';
            else if (a == 0xEF && b == 0xBC && c == 0x9B) repl = ';';
            else if (a == 0xE2 && b == 0x80 && c == 0xA6) repl = '.';
            if (repl) {
                out.push_back(repl);
                i += 3;
                continue;
            }
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

// Per-language letter pronunciation tables for spell-out expansion.
// Each entry maps an ASCII lowercase letter to its name in that
// language's orthography. The expander below substitutes acronyms
// like "TTS" or "USA" with their letter names BEFORE G2P, so the
// existing engine handles them as ordinary words.
const std::unordered_map<char, std::string>& letter_names_en() {
    static const std::unordered_map<char, std::string> m = {
        {'a',"ay"}, {'b',"bee"}, {'c',"see"}, {'d',"dee"}, {'e',"ee"},
        {'f',"eff"}, {'g',"jee"}, {'h',"aitch"}, {'i',"eye"},
        {'j',"jay"}, {'k',"kay"}, {'l',"el"}, {'m',"em"}, {'n',"en"},
        {'o',"oh"}, {'p',"pee"}, {'q',"cue"}, {'r',"are"}, {'s',"ess"},
        {'t',"tee"}, {'u',"you"}, {'v',"vee"}, {'w',"double you"},
        {'x',"ex"}, {'y',"why"}, {'z',"zee"},
    };
    return m;
}
const std::unordered_map<char, std::string>& letter_names_it() {
    static const std::unordered_map<char, std::string> m = {
        {'a',"a"}, {'b',"bi"}, {'c',"ci"}, {'d',"di"}, {'e',"e"},
        {'f',"effe"}, {'g',"gi"}, {'h',"acca"}, {'i',"i"}, {'j',"i lunga"},
        {'k',"cappa"}, {'l',"elle"}, {'m',"emme"}, {'n',"enne"},
        {'o',"o"}, {'p',"pi"}, {'q',"cu"}, {'r',"erre"}, {'s',"esse"},
        {'t',"ti"}, {'u',"u"}, {'v',"vu"}, {'w',"doppia vu"},
        {'x',"ics"}, {'y',"i greca"}, {'z',"zeta"},
    };
    return m;
}
const std::unordered_map<char, std::string>& letter_names_de() {
    static const std::unordered_map<char, std::string> m = {
        {'a',"ah"}, {'b',"beh"}, {'c',"tseh"}, {'d',"deh"}, {'e',"eh"},
        {'f',"eff"}, {'g',"geh"}, {'h',"hah"}, {'i',"ih"}, {'j',"yott"},
        {'k',"kah"}, {'l',"ell"}, {'m',"emm"}, {'n',"enn"},
        {'o',"oh"}, {'p',"peh"}, {'q',"ku"}, {'r',"err"}, {'s',"ess"},
        {'t',"teh"}, {'u',"uh"}, {'v',"fau"}, {'w',"veh"},
        {'x',"iks"}, {'y',"ypsilon"}, {'z',"tsett"},
    };
    return m;
}
const std::unordered_map<char, std::string>& letter_names_pl() {
    static const std::unordered_map<char, std::string> m = {
        {'a',"a"}, {'b',"be"}, {'c',"ce"}, {'d',"de"}, {'e',"e"},
        {'f',"ef"}, {'g',"gie"}, {'h',"ha"}, {'i',"i"}, {'j',"jot"},
        {'k',"ka"}, {'l',"el"}, {'m',"em"}, {'n',"en"},
        {'o',"o"}, {'p',"pe"}, {'q',"ku"}, {'r',"er"}, {'s',"es"},
        {'t',"te"}, {'u',"u"}, {'v',"fau"}, {'w',"wu"},
        {'x',"iks"}, {'y',"igrek"}, {'z',"zet"},
    };
    return m;
}

const std::unordered_map<char, std::string>& letter_names_for(Variant v) {
    switch (v) {
        case Variant::Italian: return letter_names_it();
        case Variant::German:  return letter_names_de();
        case Variant::Polish:  return letter_names_pl();
        default:               return letter_names_en();
    }
}

// Vowel set for spell-out detection. y counts so words like "by"/"my"
// don't trigger; per-language vowel sets cover diacritics.
bool is_vowel_for_spell(char c, Variant v) {
    const char lc = static_cast<char>(std::tolower(
        static_cast<unsigned char>(c)));
    if (lc == 'a' || lc == 'e' || lc == 'i' || lc == 'o' || lc == 'u')
        return true;
    if (lc == 'y' && (v == Variant::USEnglish || v == Variant::UKEnglish
                      || v == Variant::Polish))
        return true;
    return false;
}

// Spell-out acronyms / consonant-only words. A word is expanded into
// per-language letter names if it's all-uppercase (length >= 2) OR
// has no vowels (length >= 2). "Ashley" / "the" / "by" stay; "TTS" /
// "tts" / "USA" / "NASA" / "mrs" get expanded to "tee tee ess" etc.
std::string spell_out_acronyms(const std::string& text, Variant variant) {
    const auto& names = letter_names_for(variant);
    std::string out;
    out.reserve(text.size() * 2);
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        // Build a contiguous ASCII-letter run as a candidate "word".
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            std::size_t j = i;
            bool all_upper = true;
            bool has_vowel = false;
            while (j < n) {
                const unsigned char cj = static_cast<unsigned char>(text[j]);
                if (!((cj >= 'a' && cj <= 'z') || (cj >= 'A' && cj <= 'Z')))
                    break;
                if (cj >= 'a' && cj <= 'z') all_upper = false;
                if (is_vowel_for_spell(text[j], variant)) has_vowel = true;
                ++j;
            }
            const std::size_t word_len = j - i;
            const bool spell =
                word_len >= 2 && (all_upper || !has_vowel);
            if (spell) {
                bool first = true;
                for (std::size_t k = i; k < j; ++k) {
                    const char lc = static_cast<char>(std::tolower(
                        static_cast<unsigned char>(text[k])));
                    auto it = names.find(lc);
                    if (it != names.end()) {
                        if (!first) out.push_back(' ');
                        out += it->second;
                        first = false;
                    }
                }
            } else {
                out.append(text, i, word_len);
            }
            i = j;
            continue;
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

// ===========================================================================
// Number-to-words expansion
// ===========================================================================
//
// Per-language reading of digit sequences. The pre-pass walks the input
// after acronym spell-out, finds contiguous ASCII digit runs, and
// substitutes them with their word forms in the target language. Years
// like 2015 read out as "two thousand fifteen" / "duemila quindici" /
// "zweitausendfünfzehn" / "dwa tysiące piętnaście" depending on variant.

std::string num_to_words_en(unsigned long long n) {
    static const char* ones[]  = {"zero","one","two","three","four","five",
                                  "six","seven","eight","nine"};
    static const char* teens[] = {"ten","eleven","twelve","thirteen",
                                  "fourteen","fifteen","sixteen",
                                  "seventeen","eighteen","nineteen"};
    static const char* tens[]  = {"","","twenty","thirty","forty","fifty",
                                  "sixty","seventy","eighty","ninety"};
    if (n == 0) return "zero";
    auto under100 = [&](unsigned long long v) {
        std::string s;
        if (v >= 20) {
            s += tens[v / 10];
            if (v % 10) s += std::string(" ") + ones[v % 10];
        } else if (v >= 10) s += teens[v - 10];
        else if (v > 0) s += ones[v];
        return s;
    };
    auto under1000 = [&](unsigned long long v) {
        std::string s;
        if (v >= 100) {
            s += std::string(ones[v / 100]) + " hundred";
            if (v % 100) s += " " + under100(v % 100);
        } else s += under100(v);
        return s;
    };
    std::string r;
    if (n >= 1000000000ULL) {
        r += under1000(n / 1000000000ULL) + " billion";
        n %= 1000000000ULL;
        if (n) r += " ";
    }
    if (n >= 1000000ULL) {
        r += under1000(n / 1000000ULL) + " million";
        n %= 1000000ULL;
        if (n) r += " ";
    }
    if (n >= 1000ULL) {
        r += under1000(n / 1000ULL) + " thousand";
        n %= 1000ULL;
        if (n) r += " ";
    }
    if (n > 0) r += under1000(n);
    return r;
}

std::string num_to_words_it(unsigned long long n) {
    static const char* ones[]  = {"zero","uno","due","tre","quattro","cinque",
                                  "sei","sette","otto","nove"};
    static const char* teens[] = {"dieci","undici","dodici","tredici",
                                  "quattordici","quindici","sedici",
                                  "diciassette","diciotto","diciannove"};
    static const char* tens[]  = {"","","venti","trenta","quaranta",
                                  "cinquanta","sessanta","settanta",
                                  "ottanta","novanta"};
    if (n == 0) return "zero";
    auto under100 = [&](unsigned long long v) -> std::string {
        if (v < 10) return ones[v];
        if (v < 20) return teens[v - 10];
        unsigned long long t = v / 10, o = v % 10;
        if (o == 0) return tens[t];
        std::string base = tens[t];
        // Apocope before uno/otto: venti+uno -> ventuno, venti+otto -> ventotto
        if (o == 1 || o == 8) base.pop_back();
        return base + ones[o];
    };
    auto under1000 = [&](unsigned long long v) -> std::string {
        if (v < 100) return under100(v);
        unsigned long long h = v / 100, r = v % 100;
        std::string base = (h == 1) ? "cento" : (std::string(ones[h]) + "cento");
        if (r) base += " " + under100(r);
        return base;
    };
    std::string r;
    if (n >= 1000000ULL) {
        unsigned long long m = n / 1000000ULL;
        r += (m == 1) ? "un milione" : (under1000(m) + " milioni");
        n %= 1000000ULL;
        if (n) r += " ";
    }
    if (n >= 1000ULL) {
        unsigned long long t = n / 1000ULL;
        r += (t == 1) ? "mille" : (under1000(t) + "mila");
        n %= 1000ULL;
        if (n) r += " ";
    }
    if (n > 0) r += under1000(n);
    return r;
}

std::string num_to_words_de(unsigned long long n) {
    static const char* ones[]  = {"null","eins","zwei","drei","vier","fünf",
                                  "sechs","sieben","acht","neun"};
    static const char* teens[] = {"zehn","elf","zwölf","dreizehn","vierzehn",
                                  "fünfzehn","sechzehn","siebzehn",
                                  "achtzehn","neunzehn"};
    static const char* tens[]  = {"","","zwanzig","dreißig","vierzig",
                                  "fünfzig","sechzig","siebzig","achtzig",
                                  "neunzig"};
    if (n == 0) return "null";
    auto under100 = [&](unsigned long long v) -> std::string {
        if (v < 10) return ones[v];
        if (v < 20) return teens[v - 10];
        unsigned long long t = v / 10, o = v % 10;
        if (o == 0) return tens[t];
        // einundzwanzig (ones digit first, "und" between)
        std::string lead = (o == 1) ? "ein" : ones[o];
        return lead + "und" + tens[t];
    };
    auto under1000 = [&](unsigned long long v) -> std::string {
        if (v < 100) return under100(v);
        unsigned long long h = v / 100, r = v % 100;
        std::string base = (h == 1) ? "einhundert"
                                    : (std::string(ones[h]) + "hundert");
        if (r) base += under100(r);   // no separator
        return base;
    };
    std::string r;
    if (n >= 1000000ULL) {
        unsigned long long m = n / 1000000ULL;
        r += (m == 1) ? "eine Million" : (under1000(m) + " Millionen");
        n %= 1000000ULL;
        if (n) r += " ";
    }
    if (n >= 1000ULL) {
        unsigned long long t = n / 1000ULL;
        r += (t == 1) ? "eintausend" : (under1000(t) + "tausend");
        n %= 1000ULL;
    }
    if (n > 0) r += under1000(n);
    return r;
}

std::string num_to_words_pl(unsigned long long n) {
    static const char* ones[]  = {"zero","jeden","dwa","trzy","cztery","pięć",
                                  "sześć","siedem","osiem","dziewięć"};
    static const char* teens[] = {"dziesięć","jedenaście","dwanaście",
                                  "trzynaście","czternaście","piętnaście",
                                  "szesnaście","siedemnaście","osiemnaście",
                                  "dziewiętnaście"};
    static const char* tens[]  = {"","","dwadzieścia","trzydzieści",
                                  "czterdzieści","pięćdziesiąt",
                                  "sześćdziesiąt","siedemdziesiąt",
                                  "osiemdziesiąt","dziewięćdziesiąt"};
    static const char* hundreds[] = {"","sto","dwieście","trzysta","czterysta",
                                     "pięćset","sześćset","siedemset",
                                     "osiemset","dziewięćset"};
    if (n == 0) return "zero";
    auto under100 = [&](unsigned long long v) -> std::string {
        if (v < 10) return ones[v];
        if (v < 20) return teens[v - 10];
        unsigned long long t = v / 10, o = v % 10;
        std::string s = tens[t];
        if (o) s += std::string(" ") + ones[o];
        return s;
    };
    auto under1000 = [&](unsigned long long v) -> std::string {
        if (v < 100) return under100(v);
        unsigned long long h = v / 100, r = v % 100;
        std::string s = hundreds[h];
        if (r) s += " " + under100(r);
        return s;
    };
    // Polish numeric agreement: 1 -> sing., 2/3/4 -> "few" (paucal),
    // 5+ -> "many" (genitive plural). Roughly:
    //   tysiąc / tysiące / tysięcy
    //   milion / miliony  / milionów
    auto count_form = [&](unsigned long long v, const char* s1,
                          const char* sf, const char* sm) -> std::string {
        unsigned long long last2 = v % 100;
        unsigned long long last  = v % 10;
        if (v == 1) return s1;
        if (last2 >= 12 && last2 <= 14) return sm;
        if (last >= 2 && last <= 4) return sf;
        return sm;
    };
    std::string r;
    if (n >= 1000000ULL) {
        unsigned long long m = n / 1000000ULL;
        const char* sfx = count_form(m, "milion", "miliony", "milionów").c_str();
        if (m == 1) r += "milion";
        else r += under1000(m) + " " + sfx;
        n %= 1000000ULL;
        if (n) r += " ";
    }
    if (n >= 1000ULL) {
        unsigned long long t = n / 1000ULL;
        std::string sfx = count_form(t, "tysiąc", "tysiące", "tysięcy");
        if (t == 1) r += "tysiąc";
        else r += under1000(t) + " " + sfx;
        n %= 1000ULL;
        if (n) r += " ";
    }
    if (n > 0) r += under1000(n);
    return r;
}

std::string num_to_words_for(unsigned long long n, Variant v) {
    switch (v) {
        case Variant::Italian: return num_to_words_it(n);
        case Variant::German:  return num_to_words_de(n);
        case Variant::Polish:  return num_to_words_pl(n);
        default:               return num_to_words_en(n);
    }
}

std::string expand_numbers(const std::string& text, Variant variant) {
    std::string out;
    out.reserve(text.size() * 2);
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        const char c = text[i];
        if (c >= '0' && c <= '9') {
            std::size_t j = i;
            while (j < n && text[j] >= '0' && text[j] <= '9') ++j;
            // Parse the digit run. Up to 18 digits fits in unsigned long long.
            // For longer numbers (rare), spell digit by digit -- safer than
            // overflowing the parser.
            if (j - i <= 18) {
                unsigned long long val = 0;
                for (std::size_t k = i; k < j; ++k) val = val * 10 + (text[k] - '0');
                out += num_to_words_for(val, variant);
            } else {
                // digit-by-digit fallback for very long sequences
                bool first = true;
                for (std::size_t k = i; k < j; ++k) {
                    if (!first) out.push_back(' ');
                    out += num_to_words_for(text[k] - '0', variant);
                    first = false;
                }
            }
            i = j;
            continue;
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

// American English /t/ /d/ flap. Between vowels (or after /r n/) and
// before an unstressed vowel, /t/ and /d/ surface as the alveolar tap
// [ɾ]. "water" /wɔːtɚ/ → [wɔːɾɚ], "butter" → [bʌɾɚ], "ladder" →
// [læɾɚ]. We reuse our Japanese tap target r_ja which has the exact
// brief alveolar contact + quick release we want.
//
// Conditions (Vaux & Wolfe 2009 §2; Picard 1997):
//   * /t/ or /d/
//   * previous item is a vowel (or syllabic r/l/n)
//   * next item is an UNSTRESSED vowel (the stress condition is the
//     classic American distribution: "atom" flaps, "attack" doesn't).
//   * the /t/ /d/ is not at a word boundary IN where the next vowel
//     starts a new content word -- we allow flap across word
//     boundaries for "got it" / "lot of" / "hit a"
void apply_american_flap(std::vector<PhonemeItem>& items) {
    static const std::unordered_set<std::string> vowels = {
        "iy","ih","ey","eh","ae","aa","ao","ow","uh","uw","ah","er",
        "iglide","uglide",   // diphthong offglides also count
    };
    auto is_vowel_or_son = [&](const std::string& k) {
        return vowels.count(k) > 0 || k == "r" || k == "n";
    };
    for (std::size_t i = 1; i + 1 < items.size(); ++i) {
        if (items[i].key != "t" && items[i].key != "d") continue;
        if (!is_vowel_or_son(items[i - 1].key)) continue;
        if (!vowels.count(items[i + 1].key)) continue;
        if (items[i + 1].stressed) continue;
        items[i].key = "r_ja";
        items[i].duration_ms = -1.0;     // let duration model reassign
        items[i].is_long = false;
    }
}

// Dark /l/ ([ɫ], velarised) for English word-final or pre-consonantal
// /l/. "Cool" /kuːɫ/ is dark, "lock" /lɑk/ is light. We mark the
// allophone by swapping the key to "l_dark" (defined in data.cpp with
// a lower F2 around 900 Hz). Wells 1982; Sproat & Fujimura 1993.
void apply_dark_l(std::vector<PhonemeItem>& items) {
    static const std::unordered_set<std::string> vowels = {
        "iy","ih","ey","eh","ae","aa","ao","ow","uh","uw","ah","er",
        "oq","iglide","uglide",
    };
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].key != "l") continue;
        const bool at_end = (i + 1 == items.size())
                         || items[i + 1].key == "sil"
                         || items[i].word_final;
        const bool before_cons = (i + 1 < items.size())
                              && !vowels.count(items[i + 1].key)
                              && items[i + 1].key != "y";
        if (at_end || before_cons) items[i].key = "l_dark";
    }
}

// Vowel nasalisation before a nasal coda. In English / German / Italian
// etc., a vowel before /m n ŋ/ in coda position is anticipatorily
// nasalised. "can" /kæn/ is really [kæ̃n], "sing" is [sɪ̃ŋ]. We model
// this by ramping the nasal_amount up during the LAST ~30 ms of the
// vowel before the nasal. The sequencer's nasal track is already in
// the right place; we just bump up the source vowel item's `nasal`
// target so the per-frame interpolation produces the ramp.
//
// This is the simplest implementation; a more accurate model would
// override the nasal track for those specific frames in the sequencer.
// The naive approach: set the vowel's item nasal to ~0.35 if the
// following item is a nasal in the same syllable.
void apply_vowel_nasalization(std::vector<PhonemeItem>& items) {
    static const std::unordered_set<std::string> vowels = {
        "iy","ih","ey","eh","ae","aa","ao","ow","uh","uw","ah","er",
        "oq","iglide","uglide",
    };
    static const std::unordered_set<std::string> nasals = { "m", "n", "ng" };
    for (std::size_t i = 0; i + 1 < items.size(); ++i) {
        if (!vowels.count(items[i].key)) continue;
        if (!nasals.count(items[i + 1].key)) continue;
        // Anticipatory nasalisation -- the sequencer's per-phoneme
        // interpolation will smooth this into a ramp toward the nasal.
        // 0.35 is enough to be perceptible without sounding fully nasal.
        // (The actual final nasal carries nasal=1.0 so the boundary
        // crossfade naturally pulls the vowel's tail nasal upward.)
    }
    // The eighth-blend crossfade in the sequencer already produces a
    // mild nasal ramp at the vowel/nasal boundary. This function is
    // currently a placeholder for an explicit ramp that's outside the
    // PhonemeItem level -- the right place to do it is in the
    // sequencer's ns[] track after the main loop fills it. We do that
    // in the sequencer itself rather than here.
    (void)items;
}

// Punctuation -> pause length (ms). Returns -1 if not a pause character.
double punctuation_pause_ms(char c) {
    switch (c) {
        case '.': return 400.0;
        case '?': case '!': return 380.0;
        case ',': case ';': case ':': return 200.0;
        default: return -1.0;
    }
}

// True for sentence-ending punctuation (./!/?) but NOT mid-sentence
// punctuation (,/;/:). The F0 model uses this to reset declination /
// re-trigger a Fujisaki phrase command at the start of the next
// sentence.
bool is_sentence_ending(char c) {
    return c == '.' || c == '!' || c == '?';
}

// English function words. In connected speech these get reduced /
// destressed regardless of CMU's word-level stress marker. Klatt 1979
// §3 calls this "function word reduction" and applies a global stress
// removal before the duration rules.
const std::unordered_set<std::string>& function_words() {
    static const std::unordered_set<std::string> s = {
        // articles + determiners
        "a","an","the","this","that","these","those","some","any",
        "no","all","each","every","my","your","his","her","its",
        "our","their",
        // pronouns
        "i","you","he","she","it","we","they","me","him","us","them",
        // auxiliaries + modals
        "am","is","are","was","were","be","been","being",
        "do","does","did","done","doing",
        "have","has","had","having",
        "will","would","shall","should","may","might","must",
        "can","could",
        // prepositions
        "of","in","on","at","to","for","with","by","from","as","into",
        "onto","through","over","under","up","down","off","out",
        "about","than","like","near",
        // conjunctions
        "and","but","or","if","when","while","because","although",
        "since","so","yet","nor",
        // common short adverbs / fillers
        "not","just","very","too","also","there","here","then","now",
    };
    return s;
}

}  // namespace

std::vector<PhonemeItem> text_to_phonemes(const std::string& input_text,
                                          Variant variant) {
    // Pre-normalize CJK / fullwidth punctuation so the single-char
    // chunking loops below see ASCII period/comma/question/exclamation
    // markers. Without this, Chinese text never sentence-breaks (since
    // 。 is three UTF-8 bytes that look opaque to the char-by-char
    // walk) and the whole utterance collapses into one giant chunk
    // with no terminal contour.
    std::string text = normalize_cjk_punct(input_text);
    // Then expand acronyms / consonant-only words to their per-language
    // letter names: TTS -> "tee tee ess", USA -> "you ess ay". Skipped
    // for Mandarin / Japanese where ASCII letter-spelling isn't a thing.
    if (variant != Variant::Mandarin && variant != Variant::Japanese) {
        text = spell_out_acronyms(text, variant);
        // Then digit sequences: 2015 -> "two thousand fifteen" /
        // "duemila quindici" / "zweitausendfünfzehn" / "dwa tysiące
        // piętnaście" depending on variant.
        text = expand_numbers(text, variant);
    }

    // Japanese uses our own kana/romaji G2P -- espeak's Japanese voice
    // doesn't expose phonemes we can map cleanly.
    if (variant == Variant::Japanese) {
        auto items = japanese_text_to_phonemes(text);
        insert_glottal_stops(items);
        assign_durations(items);
        return items;
    }

    // Espeak-ng path is used for UK English, Polish, German, Mandarin
    // Chinese, and Italian. US English keeps the CMU dict path below
    // -- it sounds better for American voices than espeak's en-us.
    if ((variant == Variant::UKEnglish || variant == Variant::Polish
         || variant == Variant::German || variant == Variant::Mandarin
         || variant == Variant::Italian || variant == Variant::French)
        && espeak_available()) {
        std::vector<PhonemeItem> out;
        std::string chunk;
        auto flush_chunk = [&](double trailing_sil_ms, bool sentence_break,
                                ContourKind end_kind) {
            if (!chunk.empty()) {
                auto items = espeak_text_to_phonemes(chunk, variant);
                if (items) {
                    for (auto& it : *items) out.push_back(std::move(it));
                }
                chunk.clear();
            }
            if (trailing_sil_ms > 0.0 && !out.empty()
                && out.back().key != "sil") {
                out.back().phrase_final = true;
                PhonemeItem sil;
                sil.key = "sil";
                sil.duration_ms = trailing_sil_ms;
                sil.is_sentence_break = sentence_break;
                sil.sentence_end_kind = end_kind;
                out.push_back(std::move(sil));
            }
        };
        for (char c : text) {
            if (c == '.') {
                flush_chunk(280.0, true, ContourKind::Statement);
            } else if (c == '?') {
                flush_chunk(260.0, true, ContourKind::Question);
            } else if (c == '!') {
                flush_chunk(260.0, true, ContourKind::Exclamation);
            } else if (c == ',' || c == ';' || c == ':') {
                flush_chunk(180.0, false, ContourKind::Statement);
            } else {
                chunk.push_back(c);
            }
        }
        flush_chunk(200.0, true, ContourKind::Statement);

        // collapse adjacent sils
        std::vector<PhonemeItem> collapsed;
        for (auto& it : out) {
            if (it.key == "sil" && !collapsed.empty()
                && collapsed.back().key == "sil") {
                collapsed.back().duration_ms = std::max(
                    collapsed.back().duration_ms, it.duration_ms);
            } else {
                collapsed.push_back(std::move(it));
            }
        }
        insert_glottal_stops(collapsed);
        assign_durations(collapsed);
        return collapsed;
    }

    std::vector<PhonemeItem> result;
    std::string word;

    // Emit phonemes for `word`. `trailing_sil_ms > 0` adds a phrase-
    // boundary silence (punctuation only); inter-word boundaries get
    // *no* silence -- just a word_final flag on the last phoneme so
    // voicing stays continuous through the boundary.
    auto flush = [&](double trailing_sil_ms, bool sentence_break,
                     ContourKind end_kind = ContourKind::Statement) {
        if (!word.empty()) {
            // Polish keeps UTF-8 bytes intact for its diacritics; English
            // strips to ASCII letters.
            std::string clean_for_lookup;
            std::string clean_for_polish;
            for (char c : word) {
                const unsigned char uc = static_cast<unsigned char>(c);
                if (std::isalpha(uc))
                    clean_for_lookup.push_back(
                        static_cast<char>(std::tolower(uc)));
                if (uc >= 0x80 || std::isalpha(uc) || uc == '-')
                    clean_for_polish.push_back(c);
            }

            // Choose G2P path by variant.
            Pron pron;
            const Pron* p = nullptr;
            bool is_fn = false;
            if (variant == Variant::Polish) {
                if (!clean_for_polish.empty()) {
                    pron = pl_g2p(clean_for_polish);
                    p = &pron;
                }
                is_fn = false;   // no English function-word reduction
            } else {
                if (!clean_for_lookup.empty()) {
                    is_fn = function_words().count(clean_for_lookup) > 0;
                    if (variant == Variant::UKEnglish) {
                        auto& d = uk_dict();
                        auto it = d.find(clean_for_lookup);
                        if (it != d.end()) p = &it->second;
                    }
                    if (!p) p = lookup(clean_for_lookup);   // US path
                    if (!p) { pron = lts_fallback(clean_for_lookup); p = &pron; }
                }
            }

            if (p) {
                const std::size_t emitted_before = result.size();
                for (const auto& ph : *p) {
                    PhonemeItem item;
                    item.key = ph.key;
                    item.duration_ms = -1.0;
                    item.stressed = !is_fn && (ph.stress == 1);
                    result.push_back(std::move(item));
                }
                if (result.size() > emitted_before) {
                    result.back().word_final = true;
                }
            }
            word.clear();
        }
        if (trailing_sil_ms > 0.0 && !result.empty()) {
            // mark phrase-final on the last emitted phoneme so the
            // duration model can apply pre-pausal lengthening, then
            // emit the actual silence.
            result.back().phrase_final = true;
            PhonemeItem sil; sil.key = "sil";
            sil.duration_ms = trailing_sil_ms;
            sil.is_sentence_break = sentence_break;
            sil.sentence_end_kind = end_kind;
            result.push_back(std::move(sil));
        }
    };

    auto kind_for = [](char c) {
        if (c == '?') return ContourKind::Question;
        if (c == '!') return ContourKind::Exclamation;
        return ContourKind::Statement;
    };

    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            flush(0.0, false);   // word boundary -- NO silence inserted
        } else if (double pp = punctuation_pause_ms(c); pp > 0.0) {
            flush(pp, is_sentence_ending(c), kind_for(c));
        } else {
            word.push_back(c);
        }
    }
    // final word with sentence-final pause (always a sentence break).
    // The kind is set by synthesize_text based on the input's last char.
    flush(300.0, true);

    // collapse runs of silence (e.g. ". " -> sil + nothing-from-flush)
    std::vector<PhonemeItem> collapsed;
    for (auto& it : result) {
        if (it.key == "sil" && !collapsed.empty()
            && collapsed.back().key == "sil") {
            collapsed.back().duration_ms = std::max(
                collapsed.back().duration_ms, it.duration_ms);
        } else {
            collapsed.push_back(std::move(it));
        }
    }

    insert_glottal_stops(collapsed);
    // English allophone passes (CMU/lookup path). Run BEFORE duration
    // assignment so the swapped phonemes pick up their own inherent
    // durations.
    if (variant == Variant::USEnglish || variant == Variant::UKEnglish) {
        if (variant == Variant::USEnglish) {
            apply_american_flap(collapsed);
        }
        apply_dark_l(collapsed);
    }
    assign_durations(collapsed);
    return collapsed;
}

}  // namespace klattalker
