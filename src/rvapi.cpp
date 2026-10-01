// rvapi.cpp -- C API implementation. Thin wrapper around KlattSynth.
// RVAPI_BUILDING is added by CMake via target_compile_definitions.

#include "rvapi/rvapi.h"

#include "klattalker/synth.hpp"
#include "klattalker/wav.hpp"

#include <cstring>
#include <memory>
#include <string>

namespace {

// Per-thread last-error buffer.
thread_local std::string g_last_error;

void set_error(const char* msg) { g_last_error = msg ? msg : ""; }

klattalker::Variant variant_from_string(const std::string& s, bool& ok) {
    ok = true;
    if (s == "us" || s == "en" || s == "english")     return klattalker::Variant::USEnglish;
    if (s == "uk" || s == "rp" || s == "british")     return klattalker::Variant::UKEnglish;
    if (s == "pl" || s == "polish")                   return klattalker::Variant::Polish;
    if (s == "de" || s == "german" || s == "deutsch") return klattalker::Variant::German;
    if (s == "zh" || s == "cn" || s == "cmn"
        || s == "mandarin" || s == "chinese")         return klattalker::Variant::Mandarin;
    if (s == "ja" || s == "jp" || s == "japanese")    return klattalker::Variant::Japanese;
    if (s == "it" || s == "italian")                  return klattalker::Variant::Italian;
    if (s == "fr" || s == "french")                   return klattalker::Variant::French;
    ok = false;
    return klattalker::Variant::USEnglish;
}

}  // namespace

// Opaque struct definition (the header forward-declares it).
struct rv_synth_s {
    std::string voice = "dan";
    std::string language = "us";
    klattalker::Variant variant = klattalker::Variant::USEnglish;
    double base_f0_override = -1.0;   // <=0 means use voice default
    int sample_rate = 22050;
};

// ---- Public API ------------------------------------------------------------

extern "C" RVAPI const char* rv_version(void) {
    return "0.1.0";
}

extern "C" RVAPI const char* rv_last_error(void) {
    return g_last_error.c_str();
}

extern "C" RVAPI rv_synth_t rv_create(void) {
    try {
        auto* p = new rv_synth_s;
        return p;
    } catch (...) {
        set_error("out of memory");
        return nullptr;
    }
}

extern "C" RVAPI void rv_destroy(rv_synth_t s) {
    delete s;
}

extern "C" RVAPI int rv_set_voice(rv_synth_t s, const char* voice_name) {
    if (!s) { set_error("null handle"); return RV_ERR_BAD_HANDLE; }
    if (!voice_name) { set_error("null voice"); return RV_ERR_UNKNOWN_VOICE; }
    std::string v = voice_name;
    for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (v != "dan" && v != "lily" && v != "john" && v != "kate"
        && v != "josh" && v != "frank" && v != "doris") {
        set_error("unknown voice (use dan|lily|john|kate|josh|frank|doris)");
        return RV_ERR_UNKNOWN_VOICE;
    }
    s->voice = v;
    set_error("");
    return RV_OK;
}

extern "C" RVAPI int rv_set_language(rv_synth_t s, const char* language) {
    if (!s) { set_error("null handle"); return RV_ERR_BAD_HANDLE; }
    if (!language) { set_error("null language"); return RV_ERR_UNKNOWN_LANG; }
    std::string l = language;
    for (auto& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    bool ok = false;
    auto v = variant_from_string(l, ok);
    if (!ok) {
        set_error("unknown language (use us|uk|pl|de|zh|ja|it|fr)");
        return RV_ERR_UNKNOWN_LANG;
    }
    s->language = l;
    s->variant = v;
    set_error("");
    return RV_OK;
}

extern "C" RVAPI int rv_set_base_f0(rv_synth_t s, double f0_hz) {
    if (!s) { set_error("null handle"); return RV_ERR_BAD_HANDLE; }
    s->base_f0_override = f0_hz;
    set_error("");
    return RV_OK;
}

// Internal helper: build a KlattSynth from the handle's current state.
static std::unique_ptr<klattalker::KlattSynth> build_synth(rv_synth_t s) {
    using namespace klattalker;
    const auto eng = EngineKind::Klsyn88;
    const auto inf = InflectionKind::Fujisaki;
    if (s->voice == "john")
        return std::make_unique<KlattSynth>(KlattSynth::make_john(eng, s->sample_rate, inf, s->variant));
    if (s->voice == "kate")
        return std::make_unique<KlattSynth>(KlattSynth::make_kate(eng, s->sample_rate, inf, s->variant));
    if (s->voice == "josh")
        return std::make_unique<KlattSynth>(KlattSynth::make_josh(eng, s->sample_rate, inf, s->variant));
    if (s->voice == "frank")
        return std::make_unique<KlattSynth>(KlattSynth::make_frank(eng, s->sample_rate, inf, s->variant));
    if (s->voice == "doris")
        return std::make_unique<KlattSynth>(KlattSynth::make_doris(eng, s->sample_rate, inf, s->variant));
    if (s->voice == "lily") {
        const double f0 = (s->base_f0_override > 0) ? s->base_f0_override : 200.0;
        return std::make_unique<KlattSynth>(KlattSynth::make_female(f0, eng, s->sample_rate, inf, s->variant));
    }
    // dan / default
    const double f0 = (s->base_f0_override > 0) ? s->base_f0_override : 90.0;
    return std::make_unique<KlattSynth>(KlattSynth::make_male(f0, eng, s->sample_rate, inf, s->variant));
}

extern "C" RVAPI int rv_synthesize(rv_synth_t   s,
                                    const char*  text,
                                    float**      out_samples,
                                    size_t*      out_n_samples,
                                    int*         out_sample_rate) {
    if (out_samples) *out_samples = nullptr;
    if (out_n_samples) *out_n_samples = 0;
    if (out_sample_rate) *out_sample_rate = 0;
    if (!s) { set_error("null handle"); return RV_ERR_BAD_HANDLE; }
    if (!text || !*text) { set_error("empty text"); return RV_ERR_EMPTY_TEXT; }
    if (!out_samples || !out_n_samples || !out_sample_rate) {
        set_error("null output pointer");
        return RV_ERR_INTERNAL;
    }
    try {
        auto synth = build_synth(s);
        auto audio = synth->synthesize_text(text, klattalker::ContourKind::Statement);
        // Heap-allocate a float buffer for the caller to free.
        float* buf = static_cast<float*>(std::malloc(audio.size() * sizeof(float)));
        if (!buf) {
            set_error("out of memory");
            return RV_ERR_INTERNAL;
        }
        std::memcpy(buf, audio.data(), audio.size() * sizeof(float));
        *out_samples = buf;
        *out_n_samples = audio.size();
        *out_sample_rate = s->sample_rate;
        set_error("");
        return RV_OK;
    } catch (const std::exception& e) {
        set_error(e.what());
        return RV_ERR_INTERNAL;
    } catch (...) {
        set_error("unknown internal error");
        return RV_ERR_INTERNAL;
    }
}

extern "C" RVAPI void rv_free_samples(float* samples) {
    std::free(samples);
}

extern "C" RVAPI int rv_synthesize_to_wav(rv_synth_t  s,
                                           const char* text,
                                           const char* wav_path) {
    if (!wav_path) { set_error("null path"); return RV_ERR_INTERNAL; }
    float* samples = nullptr;
    size_t n = 0;
    int sr = 0;
    int rc = rv_synthesize(s, text, &samples, &n, &sr);
    if (rc != RV_OK) return rc;
    std::vector<float> v(samples, samples + n);
    bool ok = klattalker::write_wav_mono(wav_path, v, sr);
    rv_free_samples(samples);
    if (!ok) { set_error("failed to write wav"); return RV_ERR_INTERNAL; }
    set_error("");
    return RV_OK;
}
