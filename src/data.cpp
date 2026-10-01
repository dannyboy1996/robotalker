#include "klattalker/data.hpp"
#include <unordered_map>

namespace klattalker {

namespace {

// --- Hillenbrand 1995 adult-male steady-state vowel formants (Hz) --------
const std::unordered_map<std::string, std::array<double, 3>> kHillenbrandM = {
    {"iy", {342, 2322, 3000}},
    {"ih", {427, 2034, 2684}},
    // FACE/GOAT now expand to nucleus + offglide -- the table value is
    // the NUCLEUS, not the Hillenbrand vowel-midpoint. Pulled from
    // Hillenbrand's start-timepoint averages so the glide trajectory
    // actually moves:
    //   /eɪ/ nucleus -> [e̞], F2 ~1840 (rises to 2050 in iglide)
    //   /oʊ/ nucleus -> [ɵ̞], F2 ~1100 (drops to 950 in uglide) --
    //     more central than Hillenbrand's midpoint to reflect the
    //     modern-GA fronted-GOAT shift (Labov 2006 ANAE).
    {"ey", {530, 1840, 2691}},
    {"eh", {580, 1799, 2605}},
    {"ae", {588, 1952, 2601}},
    {"aa", {768, 1333, 2522}},
    {"ao", {652,  997, 2538}},
    {"ow", {500, 1100, 2459}},
    {"uh", {469, 1122, 2434}},
    {"uw", {378,  997, 2343}},
    {"ah", {623, 1200, 2550}},
    {"er", {474, 1379, 1710}},
};

const std::unordered_map<std::string, std::array<double, 3>> kHillenbrandF = {
    {"iy", {437, 2761, 3372}},
    {"ih", {483, 2365, 3053}},
    {"ey", {607, 2095, 3047}},   // FACE nucleus, female scale
    {"eh", {731, 2058, 2979}},
    {"ae", {669, 2349, 2972}},
    {"aa", {936, 1551, 2815}},
    {"ao", {781, 1136, 2825}},
    {"ow", {573, 1252, 2828}},   // FACE nucleus modern-GA, female scale
    {"uh", {519, 1225, 2827}},
    {"uw", {459, 1105, 2735}},
    {"ah", {753, 1426, 2933}},
    {"er", {523, 1588, 1929}},
};

// -----------------------------------------------------------------------
// Deterding 1997 RP/SSBE monophthongs (5 male + 5 female BBC speakers,
// JIPA 27:47-55). Notable contrasts with GA: /uː/ fronted to ~1200 Hz
// F2; distinct /ɒ/ in "lot"; lower F2 for /æ/. We re-use the ARPABET
// keys for the matching English vowels and add "oq" for /ɒ/.
const std::unordered_map<std::string, std::array<double, 3>> kDeterdingM = {
    {"iy", {280, 2249, 2765}},   // FLEECE
    {"ih", {367, 1757, 2556}},   // KIT
    {"eh", {494, 1650, 2547}},   // DRESS
    {"ae", {690, 1550, 2463}},   // TRAP
    {"aa", {646, 1155, 2490}},   // PALM / START
    {"oq", {558, 1047, 2481}},   // LOT  — distinct RP vowel
    {"ao", {415,  828, 2619}},   // THOUGHT
    {"uh", {379, 1173, 2445}},   // FOOT
    {"uw", {316, 1191, 2408}},   // GOOSE (fronted!)
    {"ah", {644, 1259, 2551}},   // STRUT
    {"er", {478, 1436, 2488}},   // NURSE
    // Diphthong NUCLEI (the offglide formants live in iglide/uglide).
    //   FACE  /eɪ/ : raised-to-DRESS nucleus, F2 ~ 1900 (Cruttenden 2014)
    //   GOAT  /əʊ/ : CENTRAL nucleus [ɵ], not LOT! F1 ~ 480, F2 ~ 1500.
    //                (Wells 1982; Deterding 1997 doesn't list əʊ as a
    //                 monophthong so we set it from Cruttenden's [ɵ].)
    //   PRICE/MOUTH/CHOICE nuclei use aa / ao from this same table.
    {"ey", {460, 1900, 2547}},   // FACE start  [e̝]
    {"ow", {480, 1500, 2400}},   // GOAT start  [ɵ] central rounded
};

const std::unordered_map<std::string, std::array<double, 3>> kDeterdingF = {
    // Female ~1.18x scale on F1/F2/F3 of the male table
    {"iy", {303, 2654, 3263}},
    {"ih", {432, 2073, 3015}},
    {"eh", {583, 1947, 3005}},
    {"ae", {814, 1829, 2906}},
    {"aa", {762, 1363, 2938}},
    {"oq", {659, 1235, 2927}},
    {"ao", {490,  977, 3091}},
    {"uh", {447, 1384, 2885}},
    {"uw", {373, 1405, 2841}},
    {"ah", {760, 1485, 3010}},
    {"er", {564, 1695, 2936}},
    {"ey", {543, 2242, 3005}},   // FACE start [e̝], female scale
    {"ow", {566, 1770, 2832}},   // GOAT start [ɵ], female scale
};

// -----------------------------------------------------------------------
// Polish vowel formants, population-average. The original klattalker
// table used Jassem 2003, which is one male + one female speaker --
// PW's /ɛ/ at F2≈1450 and /a/ at F2≈1000 are noticeably more central
// than the Polish-wide average and made the synth sound alien.
//
// These values are the consensus from larger studies:
//   * Nowak 2006 "Vowel Reduction in Polish" (UC Berkeley PhD,
//     multi-speaker corpus)
//   * Czajka & Włodarczak 2003 "Acoustic Studies of Polish Vowels"
//   * Wierzchowska 1971 "Wymowa Polska" (the older standard reference)
//
// /ɛ/ and /a/ are properly front / central instead of centralized
// back, matching what the phonological description actually says.
// Polish nasals ą/ę still realise as oral vowel + nasal glide.
const std::unordered_map<std::string, std::array<double, 3>> kJassemM = {
    {"iy", {290, 2200, 2900}},   // /i/
    {"ih", {380, 1700, 2500}},   // /ɨ/ — close central
    {"eh", {570, 1700, 2550}},   // /ɛ/ -- front, not central
    {"aa", {720, 1280, 2550}},   // /a/ -- central, not back
    {"ao", {570, 1020, 2580}},   // /ɔ/
    {"uw", {330,  880, 2440}},   // /u/
    {"ah", {500, 1500, 2500}},   // schwa-like fallback
};

// -----------------------------------------------------------------------
// Pätzold & Simpson 1997 German vowel formants ("Acoustic analysis of
// German vowels" — the canonical German vowel acoustic study). German
// has 14 monophthongs split between short and long; we collapse the
// length distinction onto a single key per quality and keep the four
// front-rounded vowels (umlauts) as separate keys:
//   yi  = /yː/  (über, fühlen)        — close front rounded
//   yu  = /ʏ/   (Hütte, Brücke)       — near-close front rounded
//   oe  = /øː/  (schön, Bötcher)      — close-mid front rounded
//   ou  = /œ/   (können, Möbel)       — open-mid front rounded
const std::unordered_map<std::string, std::array<double, 3>> kPaetzoldM = {
    {"iy", {290, 2230, 2950}},   // /iː/
    {"ih", {380, 1990, 2500}},   // /ɪ/
    {"ey", {370, 2080, 2750}},   // /eː/
    {"eh", {540, 1760, 2500}},   // /ɛ/, /ɛː/
    {"aa", {720, 1230, 2500}},   // /aː/, /a/
    {"ao", {510,  920, 2700}},   // /ɔ/
    {"ow", {370,  750, 2400}},   // /oː/
    {"uh", {380,  950, 2300}},   // /ʊ/
    {"uw", {320,  730, 2300}},   // /uː/
    {"ah", {470, 1460, 2500}},   // /ə/
    {"er", {620, 1330, 2500}},   // /ɐ/ vocalised r
    // German-only umlauts (front + rounded). F3 sits ~2150 Hz for all
    // four — the rounding lowers F3 vs the unrounded equivalents.
    {"yi", {290, 1880, 2150}},   // /yː/
    {"yu", {400, 1650, 2150}},   // /ʏ/
    {"oe", {370, 1500, 2150}},   // /øː/
    {"ou", {550, 1430, 2150}},   // /œ/
};

const std::unordered_map<std::string, std::array<double, 3>> kPaetzoldF = {
    // Female ~1.18x scaling on F1-F3, matching the ratios reported in
    // Pätzold & Simpson 1997 for the female subset.
    {"iy", {340, 2640, 3490}},
    {"ih", {450, 2350, 2950}},
    {"ey", {437, 2455, 3245}},
    {"eh", {637, 2080, 2950}},
    {"aa", {850, 1450, 2950}},
    {"ao", {602, 1085, 3185}},
    {"ow", {437,  885, 2830}},
    {"uh", {450, 1120, 2715}},
    {"uw", {378,  860, 2715}},
    {"ah", {555, 1725, 2950}},
    {"er", {730, 1570, 2950}},
    {"yi", {342, 2220, 2540}},
    {"yu", {472, 1950, 2540}},
    {"oe", {437, 1770, 2540}},
    {"ou", {650, 1690, 2540}},
};

// -----------------------------------------------------------------------
// Mandarin Chinese vowel formants. Values are population averages from:
//   * Wu 2017 "Acoustic analysis of Mandarin vowels" (multi-speaker
//     corpus, 30 adult speakers, the canonical modern reference)
//   * Lin 2007 "The Sounds of Chinese" (CUP) §3 -- consolidated formant
//     tables for the standard 6-vowel monophthong system
//
// Mandarin's main vowels are:
//   /a/   open central -> reuse "aa"
//   /ɤ/   close-mid back unrounded ("e" in 哥 gē) -- dedicated "ee_zh"
//   /i/   close front -> reuse "iy"
//   /o/   close-mid back rounded -> reuse "ow"
//   /u/   close back rounded -> reuse "uw"
//   /y/   close front rounded ("ü") -> reuse German "yi"
//   /ə/   schwa, used in some unstressed syllables -> reuse "ah"
// Diphthongs (ai, ao, ei, ou) decompose to nucleus + iglide/uglide.
//
// We don't override the shared monophthongs from Hillenbrand for now --
// the values are close enough for Mandarin that swapping would be
// counterproductive (Mandarin /i/ is right at iy=342/2322, Mandarin
// /u/ is at uw=378/997). The one quality unique to Mandarin (/ɤ/) gets
// its own key.
const std::unordered_map<std::string, std::array<double, 3>> kMandarinM = {
    {"iy", {310, 2280, 2900}},   // /i/
    {"ih", {360, 1900, 2600}},   // unused fallback
    {"aa", {820, 1230, 2500}},   // /a/  open central
    {"ow", {430,  790, 2450}},   // /o/  close-mid back rounded
    {"uw", {350,  790, 2400}},   // /u/  close back rounded
    {"yi", {290, 1830, 2150}},   // /y/  close front rounded (ü)
    {"eh", {580, 1700, 2500}},   // /ɛ/  open-mid front (after y/ü)
    {"ah", {500, 1300, 2400}},   // /ə/  reduced schwa
    // Dedicated Mandarin /ɤ/ "e" -- close-mid back unrounded, the
    // famous "ge"/"ke"/"he" vowel. F2 sits between /o/ and /a/, F1
    // moderate. Wu 2017 reports F1≈490, F2≈1100 male average.
    {"ee_zh", {490, 1100, 2400}},
    // Apical "buzzed" vowels: /ɿ/ after alveolar sibilants (zi/ci/si)
    // and /ʅ/ after retroflex sibilants (zhi/chi/shi/ri). Acoustically
    // these are syllabic continuations of the preceding fricative --
    // very centralised, low F2 (1400 for alveolar, 1100 for retroflex),
    // not the /i/-like vowel my old mapping was emitting. Using a
    // single key for both works: the preceding consonant's locus
    // dominates the F2 anyway. Lee & Zee 2003 measurements.
    {"i_apic", {320, 1250, 2200}},
};

const std::unordered_map<std::string, std::array<double, 3>> kMandarinF = {
    // ~1.17x F1-F3 scaling for female speakers
    {"iy", {363, 2668, 3393}},
    {"ih", {421, 2223, 3042}},
    {"aa", {959, 1439, 2925}},
    {"ow", {503,  924, 2867}},
    {"uw", {410,  924, 2808}},
    {"yi", {339, 2141, 2516}},
    {"eh", {679, 1989, 2925}},
    {"ah", {585, 1521, 2808}},
    {"ee_zh", {573, 1287, 2808}},
    {"i_apic", {374, 1463, 2574}},
};

// -----------------------------------------------------------------------
// Japanese vowel formants. Five-vowel system /a i u e o/. Values from:
//   * Keating & Huffman 1984 measurements for adult male Tokyo Japanese
//   * Sugito 1999 review of Japanese vowel acoustics
//
// Notable differences from the English defaults already in the table:
//   /u/ is unrounded and slightly fronted ([ɯ] ~ [ɯ̟]) -- F2 around 1200
//   instead of English's ~1000. Without this, Japanese /u/ sounds like
//   English /uw/ which is too back.
//   /a/ is central (low central [ä]) not the back English /ɑ/.
const std::unordered_map<std::string, std::array<double, 3>> kJapaneseM = {
    {"iy", {310, 2300, 2900}},   // /i/
    {"eh", {490, 1850, 2500}},   // /e/
    {"aa", {740, 1280, 2500}},   // /a/ central
    {"ow", {500,  850, 2500}},   // /o/
    {"uw", {380, 1200, 2400}},   // /ɯ/ unrounded fronted-back
    // Unused but kept so single-vowel lookups don't fail on accidents.
    {"ih", {410, 2050, 2700}},
    {"ao", {600,  900, 2500}},
    {"ah", {550, 1300, 2500}},
};

const std::unordered_map<std::string, std::array<double, 3>> kJapaneseF = {
    // ~1.18x F1-F3 scaling
    {"iy", {365, 2715, 3422}},
    {"eh", {578, 2183, 2950}},
    {"aa", {873, 1510, 2950}},
    {"ow", {590, 1003, 2950}},
    {"uw", {448, 1416, 2832}},
    {"ih", {483, 2419, 3186}},
    {"ao", {708, 1062, 2950}},
    {"ah", {649, 1534, 2950}},
};

// -----------------------------------------------------------------------
// Italian vowel formants. Seven-vowel system /i e ɛ a ɔ o u/. The
// values below are tightened to match published Italian acoustic
// studies. The previous values (Calamai 2009-ish) overlapped uncomfortably
// with American English -- /a/ at 700/1300 is essentially Hillenbrand's
// /aa/ at 768/1333, /e/ at 400/2000 is close to English ey, etc.
//
// Sources (composite where they disagree):
//   * Albano Leoni & Maturi 2003 "Manuale di fonetica" Tab. 4.3
//     (modern standard Italian male average -- the most-cited reference
//     used by Italian-language phonetic textbooks)
//   * Ferrero et al. 1995 "Vocali italiane: misurazioni acustiche"
//   * Vayra, Avesani & Fowler 1999 (focused on tonic/atonic /a/)
//
// Distinguishing features vs. English:
//   * /a/ more open AND more back  (F1=750, F2=1250)
//   * /ɛ/ more open than English /ɛ/ (F1=580 vs Hillenbrand 580 -- same;
//     but F2=1750 vs English 1799)
//   * /o/ and /u/ have lower F2 (more back) -- Italian round vowels are
//     more clearly back, not the central /u̟/ of American English
//   * F3 generally ~100 Hz lower than English -- Italian doesn't have
//     the high F3 of American /r/-colored speech to pull the average up
const std::unordered_map<std::string, std::array<double, 3>> kItalianM = {
    {"iy", {290, 2310, 3000}},   // /i/
    {"ey", {380, 2050, 2750}},   // /e/ close-mid front
    {"eh", {580, 1750, 2500}},   // /ɛ/ open-mid front
    {"aa", {720, 1280, 2450}},   // /a/ open central. Same values as
                                 // Polish /a/ (Nowak 2006) which is
                                 // perceptually correct. Trying to push
                                 // F1/F2 further apart (800/1100) made
                                 // the resonators interact and produce
                                 // the WRONG envelope around 540/1780,
                                 // landing back in English /æ/.
    {"ao", {570,  930, 2400}},   // /ɔ/ open-mid back rounded
    {"ow", {390,  720, 2400}},   // /o/ close-mid back rounded
    {"uw", {290,  640, 2380}},   // /u/ -- properly back, not centralised
    // Fallbacks for foreign loanwords / reduced vowels espeak emits
    {"ih", {380, 1900, 2650}},
    {"ah", {500, 1350, 2450}},
    {"ae", {650, 1700, 2450}},
    {"uh", {380,  900, 2450}},
    {"er", {450, 1400, 1700}},
};

const std::unordered_map<std::string, std::array<double, 3>> kItalianF = {
    // Female ~1.17x F1/F2/F3 scaling
    {"iy", {339, 2703, 3510}},
    {"ey", {445, 2399, 3218}},
    {"eh", {679, 2048, 2925}},
    {"aa", {850, 1510, 3009}},   // matches Polish female /a/
    {"ao", {667, 1088, 2808}},
    {"ow", {456,  842, 2808}},
    {"uw", {339,  749, 2785}},
    {"ih", {445, 2223, 3101}},
    {"ah", {585, 1580, 2867}},
    {"ae", {761, 1989, 2867}},
    {"uh", {445, 1053, 2867}},
    {"er", {527, 1638, 1989}},
};

// -----------------------------------------------------------------------
// French vowel formants. Standard Parisian French has 10 oral + 4 nasal
// vowels. Values are from:
//   * Carton 1974 "Introduction à la phonétique du français"
//   * Calliope 1989 "La parole et son traitement automatique"
// The oral set reuses existing keys; the nasal vowels are dedicated
// keys (an_fr, en_fr, on_fr, un_fr) defined in the consonant map with
// nasal=0.5 so the nasal branch couples in throughout.
// Values tightened against Carton 1974 Table 3 (male, Standard
// Parisian French). The previous table compressed the /e/-/ɛ/ F2
// distinction (2050 vs 1800 = only 250 Hz gap) and put /œ/ too high
// in F2. The Carton canonical values give a wider mid-front spread
// and properly separate the open-mid /ɛ/ and /œ/ from the close-mid
// /e/ and /ø/.
const std::unordered_map<std::string, std::array<double, 3>> kFrenchM = {
    {"iy", {255, 2127, 2802}},   // /i/
    {"ey", {336, 1899, 2671}},   // /e/ close-mid front
    {"eh", {556, 1640, 2589}},   // /ɛ/ open-mid front
    {"aa", {722, 1273, 2553}},   // /a/ central
    {"ao", {519,  973, 2553}},   // /ɔ/ open-mid back rounded
    {"ow", {378,  731, 2553}},   // /o/ close-mid back rounded
    {"uw", {293,  672, 2553}},   // /u/
    {"yi", {276, 1822, 2244}},   // /y/ close front rounded
    {"oe", {378, 1418, 2349}},   // /ø/ close-mid front rounded
    {"ou", {556, 1322, 2349}},   // /œ/ open-mid front rounded
    {"ah", {420, 1322, 2476}},   // /ə/ schwa
    {"ih", {380, 2000, 2700}},
    {"ae", {660, 1700, 2400}},
    {"uh", {380,  900, 2400}},
    {"er", {450, 1400, 1700}},
};

const std::unordered_map<std::string, std::array<double, 3>> kFrenchF = {
    // Female ~1.17x F1/F2/F3 scaling
    {"iy", {298, 2489, 3278}},
    {"ey", {393, 2222, 3125}},
    {"eh", {650, 1919, 3029}},
    {"aa", {845, 1489, 2987}},
    {"ao", {607, 1138, 2987}},
    {"ow", {442,  855, 2987}},
    {"uw", {343,  786, 2987}},
    {"yi", {323, 2132, 2625}},
    {"oe", {442, 1659, 2748}},
    {"ou", {650, 1547, 2748}},
    {"ah", {491, 1547, 2897}},
    {"ih", {445, 2340, 3159}},
    {"ae", {772, 1989, 2808}},
    {"uh", {445, 1053, 2808}},
    {"er", {527, 1638, 1989}},
};

const std::unordered_map<std::string, std::array<double, 3>> kJassemF = {
    // Female population averages ~1.18x scaling on F1/F2/F3.
    {"iy", {342, 2596, 3422}},
    {"ih", {448, 2006, 2950}},
    {"eh", {673, 2006, 3009}},
    {"aa", {850, 1510, 3009}},
    {"ao", {673, 1204, 3044}},
    {"uw", {389, 1038, 2879}},
    {"ah", {590, 1770, 2950}},
};

// F4, F5 fillers; vocal-tract length difference puts them lower for males.
constexpr std::array<double, 2> kExtraM = {3500.0, 4500.0};
constexpr std::array<double, 2> kExtraF = {4000.0, 5000.0};

// Klatt default formant bandwidths used for all vowels.
constexpr std::array<double, 5> kVowelBw = {60.0, 90.0, 150.0, 200.0, 250.0};

Target build_vowel(const std::array<double, 3>& f123,
                   const std::array<double, 2>& extras,
                   std::string label) {
    Target t;
    t.formants = {f123[0], f123[1], f123[2], extras[0], extras[1]};
    t.bandwidths = kVowelBw;
    t.parallel_db.fill(-100.0);
    t.duration_ms = 200.0;
    t.voicing = 1.0;
    // vowels don't pull on neighbors — locus equals target, lock = 0
    t.locus = t.formants;
    t.locus_lock = 0.0;
    t.label = std::move(label);
    return t;
}

// --- MITalk/Klatt 1980 consonant targets (male) --------------------------
//
// Values follow Allen, Hunnicutt & Klatt 1987 conventions, with
// cross-checks against Klatt 1980 Table I and Stevens 1998. The parallel
// amplitudes A1..A6 are calibrated so the cascade is silent for pure
// fricatives (voicing=0) and the parallel resonators paint the spectrum
// directly.
//
// Indexing: A1 -> 320 Hz, A2 -> 1000 Hz, A3 -> 2200 Hz, A4 -> 3500 Hz,
//           A5 -> 4500 Hz, A6 -> 5500 Hz (klsyn80/88 standard centres).
//
// Frequency / bandwidth columns are used by the cascade during voiced
// portions (e.g. voiced fricatives, nasals, sonorants).
// Locus values for the F2/F3 transitions adjacent vowels make into and
// out of each consonant. Source: Delattre/Liberman/Cooper 1955 (locus
// theory), Klatt 1980 Table I, Stevens 1998 §6 (place-of-articulation
// loci). F1 locus is typically low for stops/nasals (~250 Hz). Higher
// formants ride the same locus as F2 for simplicity (only F2/F3 carry
// significant place information).
//
//   bilabial  /p,b,m/      F2 ~  800
//   alveolar  /t,d,n,s,z/  F2 ~ 1700
//   velar     /k,g,ng/     F2 ~ 2300 (front) / 1300 (back) — average ~1800
//   /sh,zh/                F2 ~ 2100
//   /f,v/                  F2 ~ 1000
//   /th,dh/                F2 ~ 1500
//   /h/                    no locus (uses host vowel's formants)
//   /l/                    F2 ~ 1300, F3 ~ 2700
//   /r/                    F3 distinctive ~1600
//   /w/                    F2 ~  600 (like /u/)
//   /y/                    F2 ~ 2200 (like /i/)
const std::unordered_map<std::string, Target> kConsonantsMale = []{
    std::unordered_map<std::string, Target> m;
    auto add = [&](std::string key, Target t) {
        t.label = key;
        // copy formants->locus if no locus was set (so single-letter
        // additions with no explicit locus still work)
        if (t.locus[0] == 0.0) t.locus = t.formants;
        m.emplace(std::move(key), std::move(t));
    };
    auto set_locus = [](Target& t, double f1, double f2, double f3,
                        double lock = 0.85) {
        t.locus = {f1, f2, f3, t.formants[3], t.formants[4]};
        t.locus_lock = lock;
        return t;
    };

    // Nasals: F1 low + widened bandwidth from the nasal pole-zero pair.
    // The antiformant (nasal zero) frequency is what distinguishes
    // /m/ /n/ /ŋ/ acoustically -- it cancels different spectral bands
    // depending on the oral cavity coupling. Values from Klatt 1980
    // Table I and Stevens 1998 §7.
    {
        Target t;
        t.formants = {250, 1100, 2300, 3500, 4500};
        t.bandwidths = {60, 100, 200, 200, 250};
        t.parallel_db.fill(-100); t.bypass_db = -100;
        t.duration_ms = 80; t.voicing = 1.0; t.nasal = 1.0;
        t.nasal_pole_freq = 270;   t.nasal_pole_bw = 100;
        t.nasal_zero_freq = 750;   t.nasal_zero_bw = 150;   // /m/: low antiformant
        t.label = "m";
        m.emplace("m", std::move(t));
    }
    {
        Target t;
        t.formants = {250, 1700, 2700, 3500, 4500};
        t.bandwidths = {60, 100, 200, 200, 250};
        t.parallel_db.fill(-100); t.bypass_db = -100;
        t.duration_ms = 80; t.voicing = 1.0; t.nasal = 1.0;
        t.nasal_pole_freq = 270;   t.nasal_pole_bw = 100;
        t.nasal_zero_freq = 1450;  t.nasal_zero_bw = 200;   // /n/: mid antiformant
        t.label = "n";
        m.emplace("n", std::move(t));
    }
    {
        Target t;
        t.formants = {250, 2300, 2700, 3500, 4500};
        t.bandwidths = {60, 100, 200, 200, 250};
        t.parallel_db.fill(-100); t.bypass_db = -100;
        t.duration_ms = 90; t.voicing = 1.0; t.nasal = 1.0;
        t.nasal_pole_freq = 270;   t.nasal_pole_bw = 100;
        t.nasal_zero_freq = 2000;  t.nasal_zero_bw = 250;   // /ŋ/: high antiformant
        t.label = "ng";
        m.emplace("ng", std::move(t));
    }
    // Palatal nasal /ɲ/ ("ny" in our key set). Used by Italian "gn"
    // (gnocchi, famiglia ... wait, famiglia is /ʎ/, not /ɲ/; gnocchi
    // is /ɲ/), French and Spanish "ñ", Polish "ń". Has /n/-like F1 +
    // /j/-like F2/F3 climb. Stevens 1998 §7.3 measurements for /ɲ/.
    {
        Target t;
        t.formants = {270, 2200, 2900, 3500, 4500};
        // Wider B1/B2 dampens the otherwise-loud cascade transients
        // at the /ny/→V boundary (Italian "gnocchi" /ɲɔkːi/ was
        // clipping with narrow Klatt bandwidths).
        t.bandwidths = {90, 160, 250, 250, 300};
        t.parallel_db.fill(-100); t.bypass_db = -100;
        t.duration_ms = 80; t.voicing = 1.0; t.nasal = 1.0;
        t.nasal_pole_freq = 280;   t.nasal_pole_bw = 120;
        // Palatal antiformant is higher than alveolar /n/'s -- the
        // closed oral cavity in front of the palate is small.
        t.nasal_zero_freq = 1800;  t.nasal_zero_bw = 220;
        t.label = "ny";
        // Moderate palatal pull (was 0.85 -- too sharp F2 climb).
        t.locus = {280, 2200, 2900, 3500, 4500};
        t.locus_lock = 0.70;
        m.emplace("ny", std::move(t));
    }

    // French nasal vowels /ɑ̃ ɛ̃ ɔ̃ œ̃/. Each is an oral vowel with
    // partial nasal coupling (nasal=0.5) producing the characteristic
    // nasalised quality. Formant values from Carton 1974. The nasal
    // pole/zero pair adds the extra spectral shaping that
    // distinguishes nasalised vowels from their oral counterparts
    // (Stevens 1998 §6.6, Beddor 2009).
    auto add_french_nasal = [&](const std::string& key,
                                const std::array<double, 5>& formants,
                                double npf, double nzf) {
        Target t;
        t.formants    = formants;
        t.bandwidths  = {80, 110, 200, 200, 250};
        t.parallel_db.fill(-100); t.bypass_db = -100;
        t.duration_ms = 130;          // typical French vowel length
        t.voicing     = 1.0;
        t.frication   = 0.0;
        t.aspiration  = 0.0;
        t.nasal       = 0.50;         // partial coupling -- the key feature
        t.closure     = false;
        t.label       = key;
        t.nasal_pole_freq = npf;
        t.nasal_pole_bw   = 120;
        t.nasal_zero_freq = nzf;
        t.nasal_zero_bw   = 200;
        t.locus = t.formants;
        t.locus_lock = 0.0;
        m.emplace(key, std::move(t));
    };
    // /ɑ̃/ "an, en"
    add_french_nasal("an_fr", {600, 1100, 2400, 3500, 4500}, 270, 800);
    // /ɛ̃/ "in, ain, ein"
    add_french_nasal("en_fr", {520, 1700, 2500, 3500, 4500}, 270, 1500);
    // /ɔ̃/ "on, om"
    add_french_nasal("on_fr", {440,  800, 2400, 3500, 4500}, 270, 700);
    // /œ̃/ "un, um" -- many modern speakers merge with /ɛ̃/
    add_french_nasal("un_fr", {520, 1400, 2200, 3500, 4500}, 270, 1300);

    // Liquids/glides — vowel-like; cascade only.
    add("l",  {{360, 1300, 2700, 3500, 4500}, {60, 80, 150, 200, 250},
               {-100,-100,-100,-100,-100,-100}, -100, 70, 1.0, 0,0,0,false,""});
    // Dark /l/ ([ɫ]) -- English word-final / pre-consonantal allophone.
    // Velarised: F2 drops to ~900 Hz (tongue dorsum raised toward velum)
    // and F1 a touch higher. Sproat & Fujimura 1993; Recasens 2012.
    add("l_dark", {{420,  900, 2600, 3500, 4500}, {70, 110, 200, 200, 250},
                   {-100,-100,-100,-100,-100,-100}, -100, 80, 1.0, 0,0,0,false,""});
    add("r",  {{420, 1300, 1600, 3500, 4500}, {60, 90, 140, 200, 250},
               {-100,-100,-100,-100,-100,-100}, -100, 80, 1.0, 0,0,0,false,""});
    add("w",  {{290,  610, 2150, 3500, 4500}, {60, 80, 150, 200, 250},
               {-100,-100,-100,-100,-100,-100}, -100, 60, 1.0, 0,0,0,false,""});
    add("y",  {{290, 2070, 2960, 3500, 4500}, {60, 80, 150, 200, 250},
               {-100,-100,-100,-100,-100,-100}, -100, 60, 1.0, 0,0,0,false,""});

    // Japanese alveolar tap /ɾ/ -- the "r" of ra/ri/ru/re/ro. Much
    // closer to a brief /d/ or /l/ than the English /ɹ/ approximant:
    // very short (~35 ms), alveolar locus (F2~1500, F3~2500 -- crucially
    // NOT the lowered-F3 of English /r/), with a brief F1 dip during
    // the tap. Using the English /r/ target here makes Japanese ra/ri
    // sound like "rra" with the wrong throaty quality. Vance 2008 §4.
    add("r_ja", {{300, 1400, 2500, 3500, 4500}, {80, 100, 150, 200, 250},
                 {-100,-100,-100,-100,-100,-100}, -100, 35, 1.0, 0,0,0,false,""});

    // Offglides for diphthongs. These are lax [ɪ]/[ʊ]-shaped targets,
    // NOT consonantal glides like /j/ /w/. Used for the second half of
    // /eɪ/ /oʊ/ /aɪ/ /aʊ/ /ɔɪ/ so the formant trajectory actually
    // glides (real diphthong) instead of sitting at a single nucleus
    // (which is what we had: FACE/GOAT came out monophthongal because
    // EY/OW were single-target). Locus lock = 0.50 so the boundary
    // reaches the offglide partway -- not as hard as a /j/ at 0.95
    // which would over-shoot into a full palatal closure.
    //
    // iglide: front-close, F2 ~ 2100 (palatal target but lower than /j/)
    // uglide: back-close + rounded, F2 ~ 950 (labial but lower than /w/)
    add("iglide", {{370, 2100, 2700, 3500, 4500}, {60, 90, 150, 200, 250},
                   {-100,-100,-100,-100,-100,-100}, -100, 60, 1.0, 0,0,0,false,""});
    add("uglide", {{370,  950, 2400, 3500, 4500}, {60, 90, 150, 200, 250},
                   {-100,-100,-100,-100,-100,-100}, -100, 60, 1.0, 0,0,0,false,""});

    // Voiceless fricatives — parallel branch carries the spectrum.
    // /f/ and /th/: diffuse, low overall amplitude, peaks mid-high.
    add("f",  {{400, 1100, 2400, 3700, 4800}, {200, 200, 300, 400, 500},
               {-100, -100,  40,  50,  55,  55}, 50, 110, 0.0, 1.0,0,0,false,""});
    add("th", {{400, 1700, 2400, 3700, 4800}, {200, 200, 300, 400, 500},
               {-100, -100,  45,  55,  55,  50}, 50, 110, 0.0, 1.0,0,0,false,""});
    // /s/: sharp peak above 4 kHz.
    add("s",  {{400, 1400, 2600, 4500, 6500}, {200, 250, 300, 500, 600},
               {-100, -100, -100,  55,  62,  62}, -100, 140, 0.0, 1.0,0,0,false,""});
    // /sh/: peak ~2.5-3 kHz.
    add("sh", {{400, 1500, 2400, 2900, 4500}, {200, 250, 300, 400, 500},
               {-100, -100,  58,  62,  55,  45}, -100, 140, 0.0, 1.0,0,0,false,""});
    // Mandarin retroflex /ʂ/ (pinyin sh). The tongue-tip curls back,
    // producing a CHARACTERISTICALLY low F3 (~1900-2100 Hz, compare
    // English /ʃ/'s 2700) and a "compact" spectrum where F2 and F3 sit
    // close together. Lin 2007 §2; Lee 1999 measurements for Mandarin
    // retroflex sibilants. Frication peak A2-A3 (1000-2200), much
    // lower than English /ʃ/ which peaks at A3-A4.
    add("sh_r", {{300, 1500, 2000, 2900, 4500}, {200, 250, 300, 400, 500},
                 {-100,  40,  55,  50,  35,  25}, -100, 130, 0.0, 1.0,0,0,false,""});
    // /h/: aspiration through the upcoming vowel's tract shape.
    add("h",  {{500, 1500, 2500, 3500, 4500}, {300, 300, 300, 400, 500},
               {-100,-100,-100,-100,-100,-100}, -100, 70, 0.0, 0, 1.0,0,false,""});

    // Voiced fricatives — voiced cascade + parallel frication.
    add("v",  {{220, 1100, 2400, 3700, 4800}, {60, 100, 200, 300, 400},
               {-100, -100,  35,  45,  45,  40}, 45, 80, 0.6, 0.7,0,0,false,""});
    add("dh", {{270, 1500, 2400, 3700, 4800}, {60, 100, 200, 300, 400},
               {-100, -100,  40,  50,  50,  45}, 45, 80, 0.6, 0.7,0,0,false,""});
    add("z",  {{300, 1500, 2600, 4500, 6000}, {60, 110, 200, 400, 500},
               {-100, -100, -100,  50,  58,  58}, -100, 100, 0.5, 0.9,0,0,false,""});
    add("zh", {{300, 1500, 2300, 2900, 4500}, {60, 110, 200, 300, 400},
               {-100, -100,  55,  60,  53,  43}, -100, 100, 0.5, 0.9,0,0,false,""});

    // Stops — closure body is silenced; release burst + post-burst VOT
    // aspiration handled in sequencer. Targets are formant trajectories
    // the following vowel transitions FROM at release. VOT values from
    // Lisker & Abramson 1964 word-initial measurements -- this is the
    // primary /p/-/b/, /t/-/d/, /k/-/g/ distinction in English.
    auto add_stop = [&](std::string key, Target t, double vot) {
        t.vot_ms = vot;
        m.emplace(key, std::move(t));
    };
    // Burst spectra revised from Stevens 1998 Fig 8.10-8.14:
    //   /p b/  bilabial: DIFFUSE LF-biased, peak ~500-1500 Hz
    //   /t d/  alveolar: HF-biased, sharp peak ~3500-5000 Hz
    //   /k g/  velar:    "compact" mid-frequency peak ~1500-3000 Hz
    // Previously /p/ /b/ were emitting HF-peaked bursts that sounded
    // closer to /t/ /d/. Now /p/ peaks around A1/A2 like a real labial.
    add_stop("p", {{400, 1100, 2300, 3500, 4500}, {60, 90, 150, 200, 250},
                   {  45,  50,  40,  30,  25,  20}, 38, 80, 0.0, 0,0,0,true,"p"}, 60.0);
    add_stop("t", {{400, 1700, 2700, 3500, 4500}, {60, 90, 150, 200, 250},
                   {-100, -100,  35,  55,  60,  60}, 45, 80, 0.0, 0,0,0,true,"t"}, 70.0);
    add_stop("k", {{400, 1900, 2400, 3500, 4500}, {60, 90, 150, 200, 250},
                   {-100,  35,  55,  55,  45,  35}, 40, 80, 0.0, 0,0,0,true,"k"}, 75.0);
    add_stop("b", {{300,  900, 2100, 3500, 4500}, {60, 90, 150, 200, 250},
                   {  35,  40,  30,  20,  15,  10}, 28, 70, 0.6, 0,0,0,true,"b"}, 8.0);
    add_stop("d", {{300, 1700, 2600, 3500, 4500}, {60, 90, 150, 200, 250},
                   {-100, -100,  25,  45,  50,  50}, 35, 70, 0.6, 0,0,0,true,"d"}, 12.0);
    add_stop("g", {{300, 1900, 2400, 3500, 4500}, {60, 90, 150, 200, 250},
                   {-100,  25,  45,  45,  35,  25}, 30, 70, 0.6, 0,0,0,true,"g"}, 20.0);

    // Affricates — proper closure + extended burst so /tʃ/ is
    // acoustically distinct from /ʃ/. The sequencer silences the
    // closure body then bursts noise for burst_ms (~60 ms for
    // affricates vs ~20 ms for plain stops).
    {
        Target ch;
        ch.formants    = {400, 1700, 2400, 3500, 4500};
        ch.bandwidths  = {200, 250, 300, 400, 500};
        ch.parallel_db = {-100, -100, 58, 62, 55, 45};
        ch.bypass_db   = -100;
        ch.duration_ms = 130;
        ch.voicing     = 0.0;
        ch.frication   = 1.0;
        ch.closure     = true;
        ch.burst_ms    = 60.0;
        ch.vot_ms      = 50.0;     // voiceless affricate -- long VOT
        ch.label       = "ch";
        m.emplace("ch", std::move(ch));
    }
    // Mandarin retroflex /tʂʰ/ (pinyin ch) and /tʂ/ (pinyin zh) -- same
    // low-F3 compact spectrum as sh_r. Aspirated and unaspirated forms.
    {
        Target ch_r;
        ch_r.formants    = {300, 1500, 2000, 2900, 4500};
        ch_r.bandwidths  = {200, 250, 300, 400, 500};
        ch_r.parallel_db = {-100,  40,  55,  50,  35,  25};
        ch_r.bypass_db   = -100;
        ch_r.duration_ms = 130;
        ch_r.voicing     = 0.0;
        ch_r.frication   = 1.0;
        ch_r.closure     = true;
        ch_r.burst_ms    = 60.0;
        ch_r.vot_ms      = 50.0;
        ch_r.label       = "ch_r";
        m.emplace("ch_r", std::move(ch_r));
    }
    {
        Target jh_r;
        jh_r.formants    = {300, 1500, 2000, 2900, 4500};
        jh_r.bandwidths  = {60, 250, 300, 400, 500};
        jh_r.parallel_db = {-100,  35,  50,  45,  30,  20};
        jh_r.bypass_db   = -100;
        jh_r.duration_ms = 120;
        jh_r.voicing     = 0.0;     // Mandarin pinyin "zh" is voiceless
        jh_r.frication   = 1.0;
        jh_r.closure     = true;
        jh_r.burst_ms    = 55.0;
        jh_r.vot_ms      = 15.0;    // unaspirated retroflex
        jh_r.label       = "jh_r";
        m.emplace("jh_r", std::move(jh_r));
    }
    {
        Target jh;
        jh.formants    = {300, 1700, 2400, 3500, 4500};
        jh.bandwidths  = {60, 200, 300, 400, 500};
        jh.parallel_db = {-100, -100, 55, 60, 53, 43};
        jh.bypass_db   = -100;
        jh.duration_ms = 120;
        jh.voicing     = 0.5;
        jh.frication   = 0.9;
        jh.closure     = true;
        jh.burst_ms    = 55.0;
        jh.vot_ms      = 15.0;     // voiced affricate -- short VOT
        jh.label       = "jh";
        m.emplace("jh", std::move(jh));
    }

    // Silence
    add("sil", {{500, 1500, 2500, 3500, 4500}, {200, 200, 200, 200, 200},
                {-100,-100,-100,-100,-100,-100}, -100, 80, 0.0, 0,0,0,false,""});

    // Glottal stop /ʔ/. A real glottal stop is a brief closure of the
    // vocal folds with an abrupt voicing onset on release -- exactly
    // what English speakers produce at the start of an utterance-initial
    // vowel ("apple" is really [ʔæpəl]) or at a vowel-vowel hiatus
    // ("the apple" -> [ðə ʔæpəl]). Modelled as a short closure with no
    // burst -- the cascade just stops then restarts.
    {
        Target q;
        q.formants    = {300, 1500, 2500, 3500, 4500};
        q.bandwidths  = {60, 100, 150, 200, 250};
        q.parallel_db = {-100,-100,-100,-100,-100,-100};
        q.bypass_db   = -100;
        q.duration_ms = 40.0;
        q.voicing     = 0.0;
        q.frication   = 0.0;
        q.aspiration  = 0.0;
        q.nasal       = 0.0;
        q.closure     = true;
        q.burst_ms    = 2.0;       // basically no burst -- just silence
        q.label       = "q";
        m.emplace("q", std::move(q));
    }

    // German /ç/ "ich-laut", voiceless palatal fricative. Distinct from
    // /ʃ/ (post-alveolar) and /ɕ/ (alveolo-palatal): /ç/ is articulated
    // with the tongue body against the hard palate, giving a narrower
    // spectrum with a clear peak around 2800-3500 Hz -- higher than /ʃ/
    // and acoustically "thinner". Stevens 1998 §6.4; Jongman, Wayland &
    // Wong 2000 for spectral moments.
    {
        Target xi;
        xi.formants    = {300, 2500, 3200, 3800, 4500};
        xi.bandwidths  = {200, 200, 250, 350, 450};
        // Heavy parallel-branch peak at A4 (3500 Hz) with significant
        // A5 (4500). Lower A3 than /ʃ/ -- gives the thinner timbre.
        xi.parallel_db = {-100, -100, 35, 60, 58, 40};
        xi.bypass_db   = -100;
        xi.duration_ms = 100;
        xi.voicing     = 0.0;
        xi.frication   = 1.0;
        xi.aspiration  = 0.0;
        xi.nasal       = 0.0;
        xi.closure     = false;
        xi.label       = "xi";
        xi.locus       = {300, 2500, 3200, 3800, 4500};
        xi.locus_lock  = 0.55;
        m.emplace("xi", std::move(xi));
    }

    // German /x/ "ach-laut", voiceless velar/uvular fricative. The other
    // German "ch": occurs after back vowels (Bach, ach, Buch, doch,
    // Sprache). Constriction at the velum (or further back for southern
    // dialects -> [χ]), giving a much LOWER spectral peak than /ç/'s
    // ich-laut -- main energy 1500-2200 Hz, not 3200. Without this
    // phoneme the engine collapsed it to /h/, so Bach sounded like
    // "Bah" with no fricative noise at all. Stevens 1998 §6.4;
    // Jongman et al. 2000; Wiese 1996 §1.4 on German /x/-/ç/ allophony.
    {
        Target xa;
        xa.formants    = {500, 1500, 2200, 3500, 4500};
        xa.bandwidths  = {300, 250, 300, 400, 500};
        // Velar/uvular friction: peak at A3 (2200 Hz), strong A2.
        // No A4/A5/A6 -- the upper spectrum is empty for /x/.
        xa.parallel_db = {-100,  45,  55,  40, -100, -100};
        xa.bypass_db   = -100;
        xa.duration_ms = 110;
        xa.voicing     = 0.0;
        xa.frication   = 1.0;
        xa.aspiration  = 0.0;
        xa.nasal       = 0.0;
        xa.closure     = false;
        xa.label       = "xa";
        xa.locus       = {300, 1500, 2200, 3500, 4500};
        xa.locus_lock  = 0.55;
        m.emplace("xa", std::move(xa));
    }

    // German /r/ -- voiced uvular FRICATIVE [ʁ] (not a trill). The
    // earlier version added a slight trill modulation but in modern
    // Standard German the trill [ʀ] is essentially stage-German only.
    // Trouvain & Möbius 2006 found [ʁ] >80% of the time in
    // spontaneous speech across regions. Speakers we tested could
    // hear the trill modulation and flagged it as wrong, so we now
    // produce a pure fricative.
    //
    // Acoustically [ʁ] is a continuous voiced fricative with:
    //   * Uvular constriction -> F2 ~ 1100, F3 ~ 2400
    //   * Frication noise (parallel branch) with a low-mid peak
    //     around 1000-1800 Hz (the back-cavity resonance)
    //   * Significant aspiration component from the partly-open glottis
    //
    // Position handling -- espeak does this for us:
    //   * Pre-vocalic (Rad, rot, grün)          : emits /ʁ/  -> ru
    //   * Intervocalic (warum, herum, fahren)   : emits /ɾ/ -> ru
    //   * Coda after long vowels (mehr, Bier)   : vocalized -> ɐ -> ah
    //   * -er endings (Mutter, Vater, Berliner) : vocalized -> ɜ -> ah
    {
        Target ru;
        ru.formants    = {500, 1100, 2400, 3500, 4500};
        ru.bandwidths  = {80,  150,  200,  200,  250};
        // Now that the IPA parser actually routes /ʁ/ here, the
        // original voiced uvular fricative settings work fine -- gentle
        // frication + voicing + uvular formants. The "/ru/ is silent"
        // problem turned out to be the IPA token never reaching the
        // phoneme, not anything wrong with this phoneme's acoustics.
        ru.parallel_db = {-100,  35,  40,  20, -100, -100};
        ru.bypass_db   = -100;
        ru.duration_ms = 75;
        ru.voicing     = 1.0;
        ru.frication   = 0.30;
        ru.aspiration  = 0.18;
        ru.nasal       = 0.0;
        ru.closure     = false;
        ru.label       = "ru";
        ru.locus       = {500, 1100, 2400, 3500, 4500};
        ru.locus_lock  = 0.60;
        ru.trill_rate_hz       = 0.0;
        ru.trill_depth         = 0.0;
        ru.trill_open_fraction = 1.0;
        m.emplace("ru", std::move(ru));
    }

    // Polish-style alveolar trill /r/ ("rr" in our internal phoneme
    // set). Same formant targets as English /r/ in tap position
    // (F1=380, F2=1300, F3=1700) but with the trill modulation engaged
    // -- the sequencer applies a square-wave AM at trill_rate_hz with
    // F1 dipping to 250 Hz during each closure. Solé 2002 reports
    // 24-32 Hz tap rates for Spanish/Catalan; Polish trills tend
    // toward the upper end of that range (Wierzchowska 1980, Bargiełówna).
    {
        Target rr;
        rr.formants    = {380, 1300, 1700, 3500, 4500};
        rr.bandwidths  = {80,  100,  140,  200,  250};
        rr.parallel_db = {-100,-100,-100,-100,-100,-100};
        rr.bypass_db   = -100.0;
        rr.duration_ms = 75.0;           // ~2 taps at 30 Hz -- the
                                         // original 110 ms / 3-4 taps
                                         // was too rolled for Italian
                                         // single /r/ (Roma, vorrei)
                                         // and slightly long for Polish.
                                         // Geminates pick up extra
                                         // taps from the ː closure pad.
        rr.voicing     = 1.0;
        rr.frication   = 0.0;
        rr.aspiration  = 0.0;
        rr.nasal       = 0.0;
        rr.closure     = false;
        rr.label       = "rr";
        rr.locus       = {380, 1300, 1700, 3500, 4500};
        rr.locus_lock  = 0.65;           // slightly weaker than /r/ approx
        rr.trill_rate_hz       = 30.0;   // tap rate
        rr.trill_depth         = 0.75;   // 75% AM
        rr.trill_open_fraction = 0.60;   // 40% closed, 60% open
        m.emplace("rr", std::move(rr));
    }

    // ---- assign F1/F2/F3 loci to each consonant -----------------------
    // (Delattre/Liberman/Cooper 1955; Klatt 1980 Table I; Stevens 1998 §6)
    //
    // Bilabials -- F2 low (~800).
    set_locus(m["p"],  250,  800, 2200, 0.90);
    set_locus(m["b"],  250,  800, 2200, 0.85);
    set_locus(m["m"],  250,  900, 2200, 0.85);
    set_locus(m["f"],  250, 1000, 2400, 0.55);
    set_locus(m["v"],  250, 1000, 2400, 0.55);
    set_locus(m["w"],  290,  610, 2150, 0.95);

    // Alveolars -- F2 ~1700, F3 ~2700.
    set_locus(m["t"],  250, 1700, 2700, 0.90);
    set_locus(m["d"],  250, 1700, 2700, 0.85);
    set_locus(m["n"],  250, 1700, 2700, 0.85);
    set_locus(m["s"],  250, 1700, 2700, 0.50);
    set_locus(m["z"],  250, 1700, 2700, 0.50);
    set_locus(m["th"], 250, 1700, 2700, 0.55);
    set_locus(m["dh"], 250, 1700, 2700, 0.55);
    set_locus(m["l"],  360, 1300, 2700, 0.80);
    set_locus(m["l_dark"], 420,  900, 2600, 0.75);
    set_locus(m["r"],  420, 1300, 1600, 0.80);   // low F3 distinctive

    // Post-alveolar.
    set_locus(m["sh"], 250, 2100, 2700, 0.55);
    set_locus(m["zh"], 250, 2100, 2700, 0.55);
    set_locus(m["ch"], 250, 2100, 2700, 0.60);
    set_locus(m["jh"], 250, 2100, 2700, 0.60);

    // Velars -- F2/F3 converge around 1800/2400 (the velar pinch).
    set_locus(m["k"],  250, 1800, 2400, 0.90);
    set_locus(m["g"],  250, 1800, 2400, 0.85);
    set_locus(m["ng"], 250, 1800, 2400, 0.85);

    // Palatal glide /y/ -- F2/F3 like /i/.
    set_locus(m["y"],  290, 2070, 2960, 0.95);

    // Diphthong offglides: lock=0.50 so the preceding vowel transitions
    // partway toward the offglide instead of either ignoring it (lock=0)
    // or snapping to it like a hard consonant boundary.
    set_locus(m["iglide"], 370, 2100, 2700, 0.50);
    set_locus(m["uglide"], 370,  950, 2400, 0.50);

    // Japanese tap: alveolar locus (not the English r-locus at F3=1600).
    // High locus_lock so the surrounding vowels actually bend toward
    // the alveolar position briefly, giving the characteristic
    // "/d/-flavored" tap quality.
    set_locus(m["r_ja"], 250, 1700, 2600, 0.85);

    // /h/ is transparent; lock=0 leaves linear interp alone.
    m["h"].locus_lock = 0.0;

    // ---- Polish alveolo-palatal sibilants ------------------------------
    //
    // Polish has three sibilant series: alveolar (s/z/ts/dz), retroflex
    // (sz/ż/cz/dż -> sh/zh/ch/jh), and alveolo-palatal (ś/ź/ć/dź).
    // Alveolo-palatals sit acoustically between /s/ and /sh/ with peak
    // energy ~3500 Hz and strong palatalisation of neighbouring vowels
    // (raised F2). Jassem 2003, Wierzchowska 1980.
    {
        Target sj;   // /ɕ/ — voiceless alveolo-palatal fric
        sj.formants    = {320, 2200, 3000, 3500, 4500};
        sj.bandwidths  = {200, 250, 300, 400, 500};
        sj.parallel_db = {-100, -100, 50, 60, 55, 45};
        sj.bypass_db   = -100;
        sj.duration_ms = 130;
        sj.voicing     = 0.0;
        sj.frication   = 1.0;
        sj.aspiration  = 0;
        sj.nasal       = 0;
        sj.closure     = false;
        sj.label       = "sj";
        sj.locus       = {250, 2200, 3000, 3500, 4500};
        sj.locus_lock  = 0.60;
        m.emplace("sj", std::move(sj));
    }
    {
        Target zj;   // /ʑ/ — voiced alveolo-palatal fric
        zj.formants    = {300, 2200, 2800, 3500, 4500};
        zj.bandwidths  = {60, 200, 300, 400, 500};
        zj.parallel_db = {-100, -100, 45, 55, 50, 40};
        zj.bypass_db   = -100;
        zj.duration_ms = 100;
        zj.voicing     = 0.5;
        zj.frication   = 0.9;
        zj.locus       = {300, 2200, 2800, 3500, 4500};
        zj.locus_lock  = 0.60;
        zj.label       = "zj";
        m.emplace("zj", std::move(zj));
    }
    {
        Target cj;   // /tɕ/ — voiceless alveolo-palatal affricate
        cj.formants    = {320, 2200, 3000, 3500, 4500};
        cj.bandwidths  = {200, 250, 300, 400, 500};
        cj.parallel_db = {-100, -100, 50, 60, 55, 45};
        cj.bypass_db   = -100;
        cj.duration_ms = 120;
        cj.voicing     = 0.0;
        cj.frication   = 1.0;
        cj.closure     = true;
        cj.burst_ms    = 55.0;
        cj.locus       = {250, 2200, 3000, 3500, 4500};
        cj.locus_lock  = 0.65;
        cj.label       = "cj";
        m.emplace("cj", std::move(cj));
    }
    {
        Target dj;   // /dʑ/ — voiced alveolo-palatal affricate
        dj.formants    = {300, 2200, 2800, 3500, 4500};
        dj.bandwidths  = {60, 200, 300, 400, 500};
        dj.parallel_db = {-100, -100, 45, 55, 50, 40};
        dj.bypass_db   = -100;
        dj.duration_ms = 120;
        dj.voicing     = 0.5;
        dj.frication   = 0.9;
        dj.closure     = true;
        dj.burst_ms    = 50.0;
        dj.locus       = {300, 2200, 2800, 3500, 4500};
        dj.locus_lock  = 0.65;
        dj.label       = "dj";
        m.emplace("dj", std::move(dj));
    }

    return m;
}();

// ~17% upward shift on F1-F3, ~12% on F4-F5 for female cons targets.
// Both the formant target and the locus get the same scaling.
Target scale_female(Target t) {
    for (int i = 0; i < 5; ++i) {
        const double s = (i < 3) ? 1.17 : 1.12;
        t.formants[i] *= s;
        t.locus[i] *= s;
    }
    return t;
}

}  // namespace

std::optional<Target> vowel_target(std::string_view key, Sex sex,
                                   Variant variant) {
    const std::unordered_map<std::string, std::array<double, 3>>* table = nullptr;
    const std::array<double, 2>* extras = nullptr;
    switch (variant) {
        case Variant::USEnglish:
            table = (sex == Sex::Male) ? &kHillenbrandM : &kHillenbrandF;
            extras = (sex == Sex::Male) ? &kExtraM : &kExtraF;
            break;
        case Variant::UKEnglish:
            table = (sex == Sex::Male) ? &kDeterdingM : &kDeterdingF;
            extras = (sex == Sex::Male) ? &kExtraM : &kExtraF;
            break;
        case Variant::Polish:
            table = (sex == Sex::Male) ? &kJassemM : &kJassemF;
            extras = (sex == Sex::Male) ? &kExtraM : &kExtraF;
            break;
        case Variant::German:
            table = (sex == Sex::Male) ? &kPaetzoldM : &kPaetzoldF;
            extras = (sex == Sex::Male) ? &kExtraM : &kExtraF;
            break;
        case Variant::Mandarin:
            table = (sex == Sex::Male) ? &kMandarinM : &kMandarinF;
            extras = (sex == Sex::Male) ? &kExtraM : &kExtraF;
            break;
        case Variant::Japanese:
            table = (sex == Sex::Male) ? &kJapaneseM : &kJapaneseF;
            extras = (sex == Sex::Male) ? &kExtraM : &kExtraF;
            break;
        case Variant::Italian:
            table = (sex == Sex::Male) ? &kItalianM : &kItalianF;
            extras = (sex == Sex::Male) ? &kExtraM : &kExtraF;
            break;
        case Variant::French:
            table = (sex == Sex::Male) ? &kFrenchM : &kFrenchF;
            extras = (sex == Sex::Male) ? &kExtraM : &kExtraF;
            break;
    }
    auto it = table->find(std::string(key));
    if (it == table->end()) return std::nullopt;
    return build_vowel(it->second, *extras, std::string(key));
}

std::optional<Target> consonant_target(std::string_view key, Sex sex,
                                       Variant variant) {
    (void)variant;   // consonants share inventory; Polish adds extras below
    auto it = kConsonantsMale.find(std::string(key));
    if (it == kConsonantsMale.end()) return std::nullopt;
    if (sex == Sex::Male) return it->second;
    return scale_female(it->second);
}

std::optional<Target> get_target(std::string_view key, Sex sex,
                                 Variant variant) {
    if (auto v = vowel_target(key, sex, variant)) return v;
    return consonant_target(key, sex, variant);
}

}  // namespace klattalker
