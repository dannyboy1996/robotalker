// espeak-ng G2P bridge.
//
// We never use espeak-ng's *synthesis* -- that would defeat the purpose
// of a Klatt synth. We only use its text-to-IPA conversion, which is
// best-in-class for a wide range of languages and free.
//
// Implementation: write the input text to a temp file (avoids shell
// quoting hazards), invoke `espeak-ng -v <voice> -q --ipa -f <file>`,
// read stdout, parse the IPA into our internal phoneme keys.

#include "klattalker/espeak.hpp"
#include "klattalker/text.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace klattalker {

namespace {

// ----- subprocess helpers -------------------------------------------------

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << content;
    return f.good();
}

// Generate a unique-ish temp file name in the current dir.
std::string make_tmp(const char* tag) {
    static std::atomic<int> counter{0};
    const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::string(".klattalker_") + tag + "_"
         + std::to_string(t & 0xFFFFFFFFULL) + "_"
         + std::to_string(counter.fetch_add(1)) + ".txt";
}

// Run "espeak-ng -v <voice> -q --ipa -f <in>" and return its stdout.
// Returns nullopt if the process couldn't be launched or returned
// non-zero.
std::optional<std::string> run_espeak(const std::string& text,
                                       const std::string& voice,
                                       const std::string& mode = "--ipa") {
    const std::string tmp_in = make_tmp("in");
    const std::string tmp_out = make_tmp("out");
    if (!write_file(tmp_in, text)) return std::nullopt;

    // Quote argument strings minimally. Voice + mode are restricted to a
    // safe alphabet by the caller; file names are our own.
    std::string cmd = "espeak-ng -v " + voice + " -q " + mode + " -f \""
                    + tmp_in + "\" > \"" + tmp_out + "\" 2> nul";
    int rc = std::system(cmd.c_str());
    std::string out = read_file(tmp_out);
    std::remove(tmp_in.c_str());
    std::remove(tmp_out.c_str());
    if (rc != 0) return std::nullopt;
    return out;
}

bool detect_espeak() {
    int rc = std::system("espeak-ng --version > nul 2> nul");
    return rc == 0;
}

// ----- IPA token table ----------------------------------------------------
//
// Each token maps to one or more internal phoneme keys. Variant-specific
// overrides handle:
//   * /r/ -> "r" for English, "rr" (trill) for Polish
//   * /ɒ/ -> "oq" for UK, falls back to "aa" elsewhere
//   * /ɲ/ -> "n" + palatalisation hint absorbed
// The lookup is longest-match-first, so multi-character entries (eg.
// "tʃ", "əʊ") are tried before single chars.

struct IpaMap {
    std::unordered_map<std::string, std::vector<std::string>> base;
};

const IpaMap& base_ipa() {
    static IpaMap m;
    static std::once_flag once;
    std::call_once(once, [] {
        m.base = {
            // ----- vowels (UK / US) -----
            {"iː", {"iy"}}, {"i",  {"iy"}},
            {"ɪ",  {"ih"}},
            {"eː", {"ey"}},                    // German long /eː/ (Beet)
            {"e",  {"eh"}}, {"ɛ",  {"eh"}},
            {"ɛː", {"eh"}},                    // German /ɛː/ (Bär)
            {"æ",  {"ae"}}, {"a",  {"ae"}},
            {"ɑː", {"aa"}}, {"ɑ",  {"aa"}},
            {"ɒ",  {"oq"}},
            {"oː", {"ow"}}, {"o",  {"ow"}},    // German long /oː/ (Boot)
            {"ɔː", {"ao"}}, {"ɔ",  {"ao"}},
            {"ʊ",  {"uh"}},
            {"uː", {"uw"}}, {"u",  {"uw"}},
            {"ʌ",  {"ah"}}, {"ɐ",  {"ah"}},
            {"ɜː", {"er"}}, {"ɜ",  {"er"}},
            {"ə",  {"ah"}},
            {"ɨ",  {"ih"}},   // Polish y
            // Closing diphthongs: nucleus + lax offglide (iglide/uglide).
            // /eɪ/ and /oʊ/ MUST expand to two segments otherwise FACE
            // and GOAT come out as flat monophthongs.
            {"eɪ", {"ey", "iglide"}},
            {"aɪ", {"aa", "iglide"}},
            {"ɔɪ", {"ao", "iglide"}},
            {"aʊ", {"aa", "uglide"}},
            {"əʊ", {"ow", "uglide"}},  {"oʊ", {"ow", "uglide"}},
            // centring diphthongs (RP)
            {"ɪə", {"ih", "ah"}},
            {"eə", {"eh", "ah"}},  {"ɛə", {"eh", "ah"}},
            {"ʊə", {"uh", "ah"}},
            // nasal Polish vowels (mostly oral + n in espeak's output);
            // overridden per-variant below for French.
            {"ɛ̃", {"eh", "n"}}, {"ɔ̃", {"ao", "n"}},
            // French nasal vowels -- per-variant override directs these
            // to dedicated nasal-coupled targets. Base maps stay as
            // Polish-style approximations.
            {"ɑ̃", {"aa", "n"}}, {"œ̃", {"ou", "n"}},

            // ----- consonants -----
            {"p", {"p"}},  {"b", {"b"}},
            {"t", {"t"}},  {"d", {"d"}},
            {"k", {"k"}},
            {"ɡ", {"g"}},  {"g", {"g"}},
            {"m", {"m"}},  {"n", {"n"}},
            {"ŋ", {"ng"}},
            {"ɲ", {"ny"}},  // palatal nasal /ɲ/ (Italian gn, Spanish/French ñ)
            // German-only front rounded vowels (Pätzold/Simpson 1997).
            // Single-quality entries; the long/short distinction is
            // discarded since our cascade treats them with the same
            // formant target (length is rendered via the duration model).
            {"yː", {"yi"}}, {"y", {"yi"}},
            {"ʏ",  {"yu"}},
            {"øː", {"oe"}}, {"ø", {"oe"}},
            {"œ",  {"ou"}},
            // German EU / ÄU diphthong. espeak's German voice writes
            // it as ɔø (a non-IPA convention); the canonical form is
            // ɔʏ (Deutsch, neun, Häuser, läuft). Both decompose to
            // /ɔ/ + /ʏ/ in our model.
            {"ɔø", {"ao", "yu"}}, {"ɔʏ", {"ao", "yu"}},
            // German /ç/ Ich-Laut: dedicated palatal fricative target
            // (xi) -- acoustically distinct from /ʃ/ (post-alveolar)
            // and /ɕ/ (alveolo-palatal /sj/) with energy peak around
            // 3200 Hz.
            {"ç",  {"xi"}},
            // German vocalised r and open-mid central -> schwa
            {"ɐ",  {"ah"}}, {"ɜ", {"ah"}},
            // German uvular flap/trill/fricative -- all rendered as /ʀ/
            // (the ru target). The /r/ -> r mapping in the base table is
            // overridden in ipa_lookup() based on variant.
            {"f", {"f"}},  {"v", {"v"}},
            {"θ", {"th"}}, {"ð", {"dh"}},
            {"s", {"s"}},  {"z", {"z"}},
            {"ʃ", {"sh"}}, {"ʒ", {"zh"}},
            {"ɕ", {"sj"}}, {"ʑ", {"zj"}},      // alveolo-palatal (Polish)
            {"x", {"xa"}},                      // /x/ ach-laut (Bach,
                                                //   Sprache, doch) and
                                                //   Polish ch/h. Velar
                                                //   fricative, NOT /h/.
            {"h", {"h"}}, {"ɦ", {"h"}},
            {"tʃ", {"ch"}}, {"dʒ", {"jh"}},
            {"tɕ", {"cj"}}, {"dʑ", {"dj"}},    // Polish ć / dź
            {"ts", {"t", "s"}}, {"dz", {"d", "z"}},
            {"l", {"l"}}, {"ɫ", {"l"}},
            // Italian palatal lateral /ʎ/ (gli) -- approximate as
            // /lj/. The palatal F2 on the following vowel carries the
            // perceptual cue.
            {"ʎ", {"l", "y"}},
            {"r", {"r"}},  {"ɾ", {"r"}}, {"ɹ", {"r"}},
            // Uvular fricatives/trills must be in the base map so the
            // IPA tokenizer recognises them; otherwise the parser
            // skips past these bytes as "unknown" and the per-variant
            // override (which routes them to "ru") never fires. This
            // was THE bug behind the French /r/ being silently
            // dropped from "rara" / "Pierre" / etc.
            {"ʁ", {"ru"}}, {"ʀ", {"ru"}}, {"χ", {"ru"}},
            // r-variants — overridden per-variant below
            {"j", {"y"}},  {"w", {"w"}},
        };
    });
    return m;
}

// Variant-specific overrides take precedence over the base map.
std::vector<std::string> ipa_lookup(const std::string& tok, Variant variant) {
    if (variant == Variant::Polish) {
        // Polish trill realisation of /r/
        if (tok == "r" || tok == "ɾ" || tok == "ɹ") return {"rr"};
        if (tok == "a"  || tok == "ɑ" || tok == "ɑː") return {"aa"};
        if (tok == "æ") return {"aa"};
        if (tok == "ɒ") return {"ao"};
        if (tok == "ɜː" || tok == "ɜ") return {"eh"};
        if (tok == "ʊ") return {"uw"};
        if (tok == "ɨ") return {"ih"};
    } else if (variant == Variant::UKEnglish) {
        if (tok == "ɒ") return {"oq"};
    } else if (variant == Variant::German) {
        // German uvular trill: /ʁ/, /ʀ/, espeak's /ɾ/, plain /r/ all
        // map to our uvular trill phoneme `ru`. /ɐ̯/ (non-syllabic ɐ,
        // the vocalised r in coda) also lands here -- followed by a
        // schwa it becomes "ah" + nothing; alone it's the trill.
        if (tok == "r" || tok == "ɾ" || tok == "ɹ" || tok == "ʁ" || tok == "ʀ")
            return {"ru"};
        // German has no TRAP /æ/; map to /a/ (aa)
        if (tok == "æ") return {"aa"};
        // German /a/ short and /aː/ long both -> aa in our model
        if (tok == "a" || tok == "ɑ" || tok == "ɑː") return {"aa"};
        // /ɒ/ doesn't exist in German -> /ɔ/
        if (tok == "ɒ") return {"ao"};
        // German /ɜ/ /ɜː/ uncommon -> schwa
        if (tok == "ɜ" || tok == "ɜː") return {"ah"};
    } else if (variant == Variant::Italian) {
        // Italian /r/ is canonically a tap intervocalically and trill
        // word-initially / when doubled (Bertinetto & Loporcaro 2005
        // §3). We always map to rr here; a post-pass converts to r_ja
        // (tap) when both neighbours are vowels and it's not part of
        // a geminate.
        if (tok == "r" || tok == "ɾ" || tok == "ɹ") return {"rr"};
    } else if (variant == Variant::French) {
        // French nasal vowels -- four dedicated phonemes (an_fr/en_fr/
        // on_fr/un_fr) with nasal=0.5 coupling baked in. /œ̃/ has
        // largely merged with /ɛ̃/ in modern Parisian French (Hansen
        // 1998) but espeak still distinguishes them.
        if (tok == "ɑ̃") return {"an_fr"};
        if (tok == "ɛ̃") return {"en_fr"};
        if (tok == "ɔ̃") return {"on_fr"};
        if (tok == "œ̃") return {"un_fr"};
        // /ʁ/ uvular fricative -- reuse ru from German.
        if (tok == "ʁ" || tok == "ʀ" || tok == "ʁ̥") return {"ru"};
        if (tok == "r" || tok == "ɾ" || tok == "ɹ") return {"ru"};
        // French has no /æ/.
        if (tok == "æ") return {"aa"};
        // /a/ short and /ɑ/ are both /a/ in modern French.
        if (tok == "a" || tok == "ɑ" || tok == "ɑː") return {"aa"};
        // /ɒ/ doesn't exist in French.
        if (tok == "ɒ") return {"ao"};
    }
    auto& base = base_ipa().base;
    auto it = base.find(tok);
    if (it == base.end()) return {};
    return it->second;
}

// ----- IPA parser ---------------------------------------------------------
//
// Walk the IPA string. We strip combining diacritics that don't carry
// phonemic info in our model (palatalisation ʲ, primary length ː we
// already match in the multi-char tokens, syllabification dots, etc.),
// pass stress markers ˈ ˌ through to the next vowel, and emit phoneme
// keys for everything else.

// Minimal UTF-8 -> codepoint iterator. We only need to count bytes per
// codepoint to advance through the IPA string.
int utf8_char_bytes(unsigned char b) {
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

// Sorted list of multi-byte tokens, longest first.
const std::vector<std::string>& sorted_tokens() {
    static std::vector<std::string> toks;
    static std::once_flag once;
    std::call_once(once, [] {
        for (const auto& [k, _] : base_ipa().base) toks.push_back(k);
        std::sort(toks.begin(), toks.end(),
                  [](const std::string& a, const std::string& b) {
                      return a.size() > b.size();
                  });
    });
    return toks;
}

static const std::unordered_set<std::string>& vowel_keys() {
    static const std::unordered_set<std::string> v = {
        "iy","ih","ey","eh","ae","aa","ao","ow","oq",
        "uh","uw","ah","er",
        // German front-rounded umlauts
        "yi","yu","oe","ou",
    };
    return v;
}

std::vector<PhonemeItem> parse_ipa(const std::string& ipa, Variant variant) {
    std::vector<PhonemeItem> out;
    int pending_stress = 0;       // 0 = none, 1 = secondary, 2 = primary
    bool word_just_ended = false;

    const std::size_t n = ipa.size();
    std::size_t i = 0;
    while (i < n) {
        // ASCII whitespace / newline -> word boundary
        const unsigned char c = static_cast<unsigned char>(ipa[i]);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!out.empty()) out.back().word_final = true;
            word_just_ended = true;
            pending_stress = 0;
            ++i;
            continue;
        }

        // Multi-byte UTF-8 handling for stress markers + diacritics
        if (starts_with(ipa, i, "ˈ")) { pending_stress = 2; i += 2; continue; }
        if (starts_with(ipa, i, "ˌ")) {
            pending_stress = std::max(pending_stress, 1);
            i += 2; continue;
        }
        if (starts_with(ipa, i, "ʲ")) { i += 2; continue; }   // palatalisation
        if (starts_with(ipa, i, "ː")) {
            // Length / gemination marker. Behaviour depends on what
            // just landed in `out`:
            //   * vowel  -> set is_long on the last vowel item. The
            //              duration model picks this up and stretches
            //              the inherent length. German /aː/ "Staat"
            //              and /a/ "Stadt" now actually contrast.
            //   * consonant -> Italian geminate: prepend a 50 ms
            //              closure copy. Same mechanism as Japanese
            //              sokuon.
            static const std::unordered_set<std::string> stop_keys = {
                "p","t","k","b","d","g",
                "f","v","s","z","sh","zh","th","dh",
                "m","n","ng","l","r","rr",
                "ch","jh",
            };
            if (!out.empty()) {
                const bool is_vowel = vowel_keys().count(out.back().key) > 0;
                if (is_vowel) {
                    out.back().is_long = true;
                } else if (stop_keys.count(out.back().key)) {
                    PhonemeItem geminate = out.back();
                    geminate.duration_ms = 50.0;
                    geminate.word_final = false;
                    geminate.phrase_final = false;
                    geminate.stressed = false;
                    geminate.is_long = false;
                    out.insert(out.end() - 1, std::move(geminate));
                }
            }
            i += 2;
            continue;
        }
        if (starts_with(ipa, i, ".")) { ++i; continue; }      // syllable break
        if (starts_with(ipa, i, "ˑ")) { i += 2; continue; }   // half-length
        if (starts_with(ipa, i, "‿")) { i += 3; continue; }   // tie bar

        // Try longest IPA token match
        bool matched = false;
        for (const auto& tok : sorted_tokens()) {
            if (!starts_with(ipa, i, tok)) continue;
            auto mapped = ipa_lookup(tok, variant);
            if (mapped.empty()) {
                // recognised but no internal key -> skip
                i += tok.size();
                matched = true;
                break;
            }
            const bool is_vowel = vowel_keys().count(mapped.front()) > 0;
            for (std::size_t k = 0; k < mapped.size(); ++k) {
                PhonemeItem item;
                item.key = mapped[k];
                item.duration_ms = -1.0;
                if (k == 0 && is_vowel && pending_stress == 2) {
                    item.stressed = true;
                }
                out.push_back(item);
            }
            if (is_vowel) pending_stress = 0;
            i += tok.size();
            matched = true;
            word_just_ended = false;
            break;
        }
        if (!matched) {
            // unknown char -- skip a single codepoint
            i += utf8_char_bytes(c);
        }
    }

    if (!out.empty()) out.back().word_final = true;
    (void)word_just_ended;
    return out;
}

// ----- Mandarin kirshenbaum parser ---------------------------------------
//
// espeak's `--ipa` output for Mandarin conflates T1 and T4 (both render
// as a trailing "5"), so we use `-x` (kirshenbaum) instead. That output
// gives the unambiguous two- or three-digit Chao tone marker after each
// syllable's vowel (55/35/21/214/51/44/11).
//
// Sample input:
//   ni35X'Au214_| w'o21_| m@44n_| 'ai51_|
//
// Mapping covers the Mandarin phoneme inventory minus apical vowels
// /ɿ/ /ʅ/ (post-sibilant "i" in zi/ci/si/zhi/chi/shi/ri) -- those are
// approximated with our "iy" target, which is wrong but tolerable.
//
// Mandarin "b/d/g" pinyin are voiceless unaspirated; we route them to
// our short-VOT /b/ /d/ /g/ since the perceptual cue is the VOT split.
// Pinyin "p/t/k" are aspirated -> our long-VOT /p/ /t/ /k/.
const std::vector<std::pair<std::string, std::vector<std::string>>>&
mandarin_kirshenbaum_tokens() {
    // Longest-first order matters: "ts_h" must be tried before "ts".
    static const std::vector<std::pair<std::string, std::vector<std::string>>> m = {
        // Affricates -- longest match first. espeak uses several
        // notations for the same Mandarin affricate (pinyin
        // j/q/zh/ch); we route all variants to a single internal key.
        //   tS;h / tS_h / tC_h / ts.h / ts._h  -> ch (aspirated)
        //   tS;  / tS   / tC                   -> cj or jh
        //   ts.  / ts._h                       -> jh / ch (retroflex)
        {"tS;h",  {"ch"}},   // pinyin q (alveolo-palatal aspirated)
        {"tS;",   {"cj"}},   // pinyin j (alveolo-palatal unaspirated)
        // Pinyin ch (retroflex /tʂʰ/) and zh (retroflex /tʂ/) -- route
        // to the retroflex variants with their low-F3 compact spectrum.
        {"ts._h", {"ch_r"}}, // pinyin ch (alt form with underscore)
        {"ts.h",  {"ch_r"}}, // pinyin ch (espeak form, no underscore)
        {"ts.",   {"jh_r"}}, // pinyin zh (retroflex unaspirated)
        {"tS_h",  {"ch_r"}}, // alt encoding for ch
        {"tS",    {"jh_r"}}, // alt encoding for zh
        {"tC_h",  {"ch"}},   // alt notation
        {"tC",    {"cj"}},   // alt notation
        {"ts_h",  {"t","s"}},// pinyin c (alveolar aspirated)
        {"ts",    {"t","s"}},// pinyin z (alveolar unaspirated)
        // aspirated stops -> our long-VOT stops
        {"p_h", {"p"}},
        {"t_h", {"t"}},
        {"k_h", {"k"}},
        // sibilants
        {"S;",  {"sj"}},     // pinyin x (alveolo-palatal /ɕ/)
        {"S",   {"sh_r"}},   // pinyin sh (retroflex /ʂ/)
        {"s.",  {"sh_r"}},   // alt retroflex notation
        {"z.",  {"zh"}},     // pinyin r (retroflex voiced fricative /ʐ/)
        {"r.",  {"r"}},      // pinyin r approximant variant
        // simple consonants
        {"p", {"b"}}, {"t", {"d"}}, {"k", {"g"}},  // unaspirated -> short-VOT
        {"f", {"f"}}, {"s", {"s"}},
        {"m", {"m"}}, {"n", {"n"}}, {"N", {"ng"}},
        {"l", {"l"}}, {"w", {"w"}}, {"j", {"y"}},
        {"x", {"xi"}},   // /ç/ ich-laut (rare, but espeak emits for some
                         // pinyin in dialectal positions)
        {"X", {"xa"}},   // /x/ ach-laut (Mandarin pinyin h)
        // Apical "buzzed" vowels -- syllabic /ɿ/ or /ʅ/ that espeak
        // marks with a trailing `[` (post-alveolar) or `.` (retroflex).
        // Must beat the bare `i` mapping; longest-first does that.
        {"i[", {"i_apic"}},
        {"i.", {"i_apic"}},
        // vowels (kirshenbaum letters)
        {"A", {"aa"}}, {"a", {"aa"}},
        {"E", {"eh"}}, {"e", {"eh"}},
        {"i", {"iy"}}, {"I", {"ih"}},
        {"u", {"uw"}}, {"U", {"uh"}},
        {"O", {"ow"}}, {"o", {"ow"}},
        {"Y", {"yi"}},                  // ü
        {"@", {"ah"}},
        {"7", {"ee_zh"}}, {"8", {"ee_zh"}},
    };
    return m;
}

// Returns (tone, is_citation_T3). is_citation_T3 is only meaningful when
// tone == 3; espeak signals citation form by emitting a 3-digit run
// (typically "214") while connected-speech half-3 comes out as "21".
std::pair<int, bool> chao_to_tone(const std::string& digits) {
    if (digits.empty()) return {0, false};
    const char a = digits.front();
    const char b = digits.back();
    const bool three_digits = digits.size() >= 3;
    if (a == '5' && b == '5') return {1, false};
    if (a == '3' && b == '5') return {2, false};
    if (a == '2' && (b == '1' || b == '4'))
        return {3, three_digits || b == '4'};
    if (a == '5' && b == '1') return {4, false};
    if ((a == '4' && b == '4') || (a == '1' && b == '1'))
        return {5, false};
    if (a == '5') return {1, false};
    if (a == '3') return {2, false};
    if (a == '2') return {3, three_digits};
    if (a == '4') return {5, false};
    return {0, false};
}

// T3 sandhi post-pass: T3+T3 -> T2+T3 (espeak usually applies this in its
// lexicon, but this pass is defensive in case of edge cases or raw-pinyin
// input). Also re-marks the surviving T3 as citation if it's utterance-
// final (no following toned syllable in this utterance).
void apply_t3_sandhi(std::vector<PhonemeItem>& items) {
    // Collect indices of every toned nucleus, with knowledge of whether
    // the following nucleus sits within the same utterance.
    std::vector<std::size_t> tone_idx;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].tone > 0) tone_idx.push_back(i);
    }

    // Walk right-to-left: if a T3 is followed (in tone_idx) by another
    // T3, the earlier one becomes T2. The last T3 in any group stays
    // T3. We also need to respect sentence-break sils -- a T3 right
    // before a sentence boundary shouldn't sandhi with a T3 that's
    // already in the NEXT sentence.
    auto crosses_break = [&](std::size_t a, std::size_t b) {
        for (std::size_t k = a + 1; k < b; ++k)
            if (items[k].is_sentence_break) return true;
        return false;
    };
    for (std::size_t k = tone_idx.size(); k-- > 1; ) {
        const std::size_t prev = tone_idx[k - 1];
        const std::size_t cur  = tone_idx[k];
        if (items[prev].tone == 3 && items[cur].tone == 3
            && !crosses_break(prev, cur)) {
            items[prev].tone = 2;
            items[prev].tone_citation = false;
        }
    }

    // Citation T3: a surviving T3 with no following toned syllable in
    // the same utterance (or whose only following tones are across a
    // sentence break) should ring out as the full 214 dipping-rising
    // contour. Half-3 stays for the T3+(T1|T2|T4|T5) case.
    for (std::size_t k = 0; k < tone_idx.size(); ++k) {
        const std::size_t cur = tone_idx[k];
        if (items[cur].tone != 3) continue;
        bool has_next_in_utt = false;
        for (std::size_t m = k + 1; m < tone_idx.size(); ++m) {
            if (crosses_break(cur, tone_idx[m])) break;
            has_next_in_utt = true;
            break;
        }
        if (!has_next_in_utt) items[cur].tone_citation = true;
    }
}

std::vector<PhonemeItem> parse_kirshenbaum_zh(const std::string& kx) {
    std::vector<PhonemeItem> out;
    int pending_stress = 0;
    static const std::unordered_set<std::string> vowels_zh = {
        "iy","ih","aa","ow","uw","yi","eh","ah","ee_zh","i_apic",
        "iglide","uglide",
    };

    const std::size_t n = kx.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = kx[i];
        // Word boundary marker
        if (c == '_' && i + 1 < n && kx[i + 1] == '|') {
            if (!out.empty()) out.back().word_final = true;
            i += 2;
            pending_stress = 0;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!out.empty()) out.back().word_final = true;
            ++i;
            pending_stress = 0;
            continue;
        }
        if (c == '\'') { pending_stress = 2; ++i; continue; }
        if (c == ',')  { pending_stress = std::max(pending_stress, 1); ++i; continue; }
        if (c == '.') { ++i; continue; }   // syllable break or noise

        // Tone digits: run of 1-3 digits attaches to the most recent vowel
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::string digits;
            while (i < n && std::isdigit(static_cast<unsigned char>(kx[i]))) {
                digits.push_back(kx[i]);
                ++i;
            }
            const auto [tone, is_citation] = chao_to_tone(digits);
            // Find the most recent vowel item in this syllable -- walk
            // backward until we hit a vowel.
            for (std::size_t k = out.size(); k-- > 0; ) {
                if (vowels_zh.count(out[k].key)) {
                    out[k].tone = tone;
                    out[k].tone_citation = is_citation;
                    break;
                }
            }
            continue;
        }

        // Try longest token match
        bool matched = false;
        for (const auto& [tok, keys] : mandarin_kirshenbaum_tokens()) {
            if (i + tok.size() > n) continue;
            if (std::memcmp(kx.data() + i, tok.data(), tok.size()) != 0) continue;
            const bool is_vowel = vowels_zh.count(keys.front()) > 0;
            for (std::size_t k = 0; k < keys.size(); ++k) {
                PhonemeItem item;
                item.key = keys[k];
                item.duration_ms = -1.0;
                if (k == 0 && is_vowel && pending_stress == 2) {
                    item.stressed = true;
                }
                out.push_back(item);
            }
            if (is_vowel) pending_stress = 0;
            i += tok.size();
            matched = true;
            break;
        }
        if (!matched) ++i;   // skip unknown byte
    }
    if (!out.empty()) out.back().word_final = true;
    return out;
}

}  // namespace

bool espeak_available() {
    static const bool ok = detect_espeak();
    return ok;
}

std::optional<std::vector<PhonemeItem>>
espeak_text_to_phonemes(const std::string& text, Variant variant) {
    if (!espeak_available()) return std::nullopt;

    const char* voice = nullptr;
    switch (variant) {
        case Variant::USEnglish:  voice = "en-us"; break;
        case Variant::UKEnglish:  voice = "en-gb"; break;
        case Variant::Polish:     voice = "pl";    break;
        case Variant::German:     voice = "de";    break;
        case Variant::Mandarin:   voice = "cmn";   break;
        case Variant::Italian:    voice = "it";    break;
        case Variant::French:     voice = "fr";    break;
        case Variant::Japanese:   return std::nullopt;   // own G2P
    }
    if (!voice) return std::nullopt;

    // Mandarin uses kirshenbaum (-x) because --ipa collapses T1 and T4
    // (both render as a trailing "5"); -x emits unambiguous two-digit
    // Chao tone markers (55/35/21/214/51/44/11).
    if (variant == Variant::Mandarin) {
        auto kx = run_espeak(text, voice, "-x");
        if (!kx) return std::nullopt;
        auto items = parse_kirshenbaum_zh(*kx);
        apply_t3_sandhi(items);
        return items;
    }

    auto ipa = run_espeak(text, voice);
    if (!ipa) return std::nullopt;
    auto items = parse_ipa(*ipa, variant);

    // Italian intervocalic-tap conversion: change single /rr/ between
    // two vowels to /r_ja/ (alveolar tap). Geminated trills (carro,
    // birra, terra) are preserved -- in those cases the rr will be
    // adjacent to another rr (the closure pad) rather than a vowel.
    if (variant == Variant::Italian) {
        static const std::unordered_set<std::string> vowels = {
            "iy","ih","ey","eh","aa","ao","ow","uw","ah","er","ae",
        };
        for (std::size_t k = 1; k + 1 < items.size(); ++k) {
            if (items[k].key != "rr") continue;
            const bool prev_v = vowels.count(items[k - 1].key) > 0;
            const bool next_v = vowels.count(items[k + 1].key) > 0;
            if (prev_v && next_v) {
                items[k].key = "r_ja";
                items[k].duration_ms = -1.0;
            }
        }
    }
    return items;
}

}  // namespace klattalker
