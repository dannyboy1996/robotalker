#include "klattalker/intensity.hpp"
#include <cmath>
#include <unordered_map>

namespace klattalker {

namespace {

// dB-relative to a 1.0 vowel reference. Values from Klatt 1987 §IV.D
// + MITalk Ch.6 amplitude tables.
//
//   vowels       0 dB  (reference)
//   /r,l,w,y/   -3 dB  (sonorant glides)
//   nasals      -5 dB
//   voiced fric -8 dB
//   /h/         -8 dB  (aspiration ride)
//   uvc fric   -12 dB  for /f,th/   -10 dB for /s,sh/  (sibilants louder)
//   stops      (closure -inf; burst handled separately)
const std::unordered_map<std::string, double>& voicing_db() {
    static const std::unordered_map<std::string, double> m = {
        // glides / liquids
        {"l", -3}, {"r", -3}, {"w", -3}, {"y", -3},
        // nasals
        {"m", -5}, {"n", -5}, {"ng", -5},
        // voiced fricatives — voicing path attenuated
        {"v", -8}, {"dh", -8}, {"z", -8}, {"zh", -8}, {"jh", -8},
        // voiceless: no voicing path
        {"f", -100}, {"th", -100}, {"s", -100}, {"sh", -100}, {"h", -100},
        {"p", -100}, {"t", -100}, {"k", -100}, {"ch", -100},
        // voiced stops have small voice bar leak
        {"b", -15}, {"d", -15}, {"g", -15},
    };
    return m;
}

// dB-relative to a noise reference of 1.0 (same convention as
// inherent_voicing_amp). Voiceless fricatives are the natural baseline.
const std::unordered_map<std::string, double>& frication_db() {
    static const std::unordered_map<std::string, double> m = {
        // strident fricatives — loudest
        {"s",  -2}, {"sh", -2}, {"z", -6}, {"zh", -6},
        // non-strident fricatives — quieter
        {"f",  -8}, {"th", -8}, {"v", -12}, {"dh", -12},
        // /h/ is aspiration not frication, but engine routes it as
        // aspiration; we still tag the per-phoneme noise level
        {"h", -10},
        // affricates — combined burst + fric
        {"ch", -3}, {"jh", -6},
    };
    return m;
}

double db_to_lin(double db) {
    if (db <= -90.0) return 0.0;
    return std::pow(10.0, db / 20.0);
}

}  // namespace

double inherent_voicing_amp(const std::string& key) {
    auto it = voicing_db().find(key);
    if (it == voicing_db().end()) return 1.0;   // vowels + unknowns
    return db_to_lin(it->second);
}

double inherent_frication_amp(const std::string& key) {
    auto it = frication_db().find(key);
    if (it == frication_db().end()) return 1.0;   // already-fricative phones
    return db_to_lin(it->second);
}

}  // namespace klattalker
