#include "klattalker/duration.hpp"
#include <unordered_map>
#include <unordered_set>

namespace klattalker {

namespace {

// Inherent steady-state durations (ms).
// Klatt 1979 Table II is the source, but those values are citation-form
// averages (slow isolated-word reading); plugging them into a TTS for
// running speech gives a noticeably slow delivery -- "I am" alone ate
// ~460 ms of voicing under the old values when it should be ~280 ms.
// We scale by ~0.70 to reach a normal conversational rate, which lines
// up with Crystal & House 1988 / Picheny et al. 1986 measurements of
// connected speech (vowels are 65-75% of citation length on average).
// Stops/fricatives are left alone -- they're already conversational.
const std::unordered_map<std::string, double>& inherent_table() {
    static const std::unordered_map<std::string, double> m = {
        // vowels -- conversational rate (Klatt 1979 * 0.70 ish)
        {"iy", 110}, {"ih",  70}, {"ey", 120}, {"eh",  80},
        {"ae", 135}, {"aa", 145}, {"ao", 160}, {"ow", 135},
        {"uh",  75}, {"uw", 125}, {"ah",  75}, {"er", 150},

        // stops (closure + release combined)
        {"p",  90}, {"t",  80}, {"k",  95},
        {"b",  75}, {"d",  65}, {"g",  80},

        // nasals
        {"m",  70}, {"n",  60}, {"ng", 85}, {"ny",  75},

        // fricatives
        {"f", 105}, {"v",  65},
        {"th",100}, {"dh", 60},
        {"s", 130}, {"z",  80},
        {"sh",130}, {"zh", 75},
        {"h",  65},

        // affricates
        {"ch",100}, {"jh", 65},

        // liquids / glides
        {"l",  60}, {"l_dark", 75}, {"r",  65}, {"w",  70}, {"y",  50},

        // diphthong offglides (the closing half of /eɪ/ /oʊ/ /aɪ/
        // /aʊ/ /ɔɪ/). Short so the nucleus carries most of the
        // diphthong duration (~70/30 nucleus:offglide is the typical
        // English ratio per Lehiste & Peterson 1961).
        {"iglide", 70}, {"uglide", 70},

        // silence (overridden by punctuation rules in text.cpp)
        {"sil", 80},

        // glottal stop -- brief closure
        {"q", 40},

        // alveolar trill + Polish alveolo-palatals
        {"rr", 110}, {"sj", 130}, {"zj", 100}, {"cj", 120}, {"dj", 120},
        // German uvular trill + ich-laut + ach-laut
        {"ru", 90}, {"xi", 100}, {"xa", 110},
        // Mandarin /ɤ/ "e" + apical "buzzed" vowel (zi/zhi etc.)
        {"ee_zh", 180}, {"i_apic", 100},
        // French nasal vowels /ɑ̃ ɛ̃ ɔ̃ œ̃/
        {"an_fr", 130}, {"en_fr", 130}, {"on_fr", 130}, {"un_fr", 130},
        // Mandarin retroflex sibilant + affricates
        {"sh_r", 130}, {"ch_r", 100}, {"jh_r", 65},
        // Japanese alveolar tap (very short -- it's a flap, not a hold)
        {"r_ja", 35},
        // German umlaut vowels (conversational)
        {"yi", 115}, {"yu",  75}, {"oe", 115}, {"ou",  75},
    };
    return m;
}

const std::unordered_set<std::string>& vowels_set() {
    static const std::unordered_set<std::string> v = {
        "iy","ih","ey","eh","ae","aa","ao","ow","uh","uw","ah","er",
        // French nasal vowels behave as vowels for duration/stress
        "an_fr","en_fr","on_fr","un_fr",
        // German umlauts
        "yi","yu","oe","ou",
        // Mandarin
        "ee_zh","i_apic",
    };
    return v;
}

bool is_vowel(const std::string& k) {
    return vowels_set().count(k) > 0;
}

bool is_consonant_segment(const std::string& k) {
    return !is_vowel(k) && k != "sil";
}

}  // namespace

double inherent_duration_ms(const std::string& key) {
    auto it = inherent_table().find(key);
    if (it == inherent_table().end()) return 100.0;   // safe default
    return it->second;
}

double apply_duration_rules(const std::string& key,
                            double inherent_ms,
                            const DurationContext& ctx) {
    double d = inherent_ms;
    const bool vowel = is_vowel(key);

    // Long-vowel lengthening. German /aː eː iː oː uː yː øː/ are ~1.6x
    // the duration of their short counterparts (Pätzold & Simpson
    // 1997 §3.2). The IPA parser sets ctx.is_long when the source
    // stream had a ː after the vowel. Without this, short/long pairs
    // sound identical -- the most-cited issue with our German.
    if (vowel && ctx.is_long) d *= 1.6;

    // Pre-fortis clipping (Klatt 1973): vowels are shorter before
    // voiceless consonants ("bit" /bɪt/ vs "bid" /bɪd"). Universal
    // across English and present in German/Italian/Polish too -- the
    // 0.78 ratio is taken from the conversational-speech end of Klatt's
    // measurements (citation form is closer to 0.65, too aggressive).
    if (vowel && ctx.next_voiceless) d *= 0.78;

    // 1. Phrase-final lengthening. Klatt 1979 rule 1 reports ~40% on
    //    the rhyme of the phrase-final syllable, but that's measured
    //    in formal read speech; in conversational tone the effect is
    //    closer to ~10-15%.
    if (ctx.phrase_final) d *= 1.12;

    // 2. Lexical stress lengthening on vowels. Klatt 1979 gives ~50%
    //    for primary stress; conversational speech is ~25% (Crystal &
    //    House 1988).
    if (vowel) {
        if (ctx.stress == 1)        d *= 1.25;   // primary
        else if (ctx.stress == 2)   d *= 1.12;   // secondary
        else if (key == "ah") {
            // schwa-reduce unstressed /ah/ -- in our table that key
            // doubles for both /ʌ/ and /ə/, which Klatt distinguishes.
            d *= 0.55;
        } else {
            d *= 0.80;   // generic unstressed reduction
        }
    }

    // 3. Word-final lengthening (Klatt 1979 rule 4: ~15%).
    if (ctx.word_final && !ctx.phrase_final) d *= 1.15;

    // 4. Consonant in a cluster — shorten by 15% (Klatt 1979 rule 7).
    if (!vowel && ctx.in_cluster) d *= 0.85;

    return d;
}

void assign_durations(std::vector<PhonemeItem>& items) {
    const std::size_t n = items.size();
    if (n == 0) return;

    // Tokenizer flags every word-/phrase-final phoneme. We promote
    // phrase-final lengthening to the nucleus of the final syllable
    // when the boundary actually lands on a coda consonant — that's
    // where Klatt 1979 puts the largest effect.
    std::size_t pf_nucleus = n;
    for (std::size_t i = n; i-- > 0; ) {
        if (items[i].phrase_final) {
            // walk left to find the nucleus of the same syllable
            std::size_t j = i;
            while (j > 0 && !is_vowel(items[j].key)) --j;
            pf_nucleus = j;
            break;
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        PhonemeItem& it = items[i];
        if (it.duration_ms > 0.0) continue;
        if (it.key == "sil") continue;

        DurationContext ctx;
        ctx.stress = it.stressed ? 1 : 0;
        ctx.phrase_final = (i == pf_nucleus) || it.phrase_final;
        ctx.word_final = it.word_final;
        ctx.is_long = it.is_long;

        // Pre-fortis: find next non-vowel and check if it's voiceless.
        // Voiceless set: stops /p t k/, fricatives /f th s sh h xi xa/,
        // affricate /ch/.
        if (is_vowel(it.key)) {
            static const std::unordered_set<std::string> vless = {
                "p","t","k","f","th","s","sh","h","ch","xi","xa",
            };
            for (std::size_t k = i + 1; k < n; ++k) {
                if (is_vowel(items[k].key)) break;
                if (items[k].key == "sil") break;
                if (vless.count(items[k].key)) {
                    ctx.next_voiceless = true;
                    break;
                }
                if (is_consonant_segment(items[k].key)) break;
            }
        }

        const bool prev_is_cons = i > 0 && is_consonant_segment(items[i - 1].key);
        const bool next_is_cons = (i + 1 < n) && is_consonant_segment(items[i + 1].key);
        ctx.in_cluster = is_consonant_segment(it.key)
                         && (prev_is_cons || next_is_cons);

        const double inherent = inherent_duration_ms(it.key);
        it.duration_ms = apply_duration_rules(it.key, inherent, ctx);
    }
}

}  // namespace klattalker
