#include "klattalker/synth.hpp"
#include "klattalker/wav.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

// On Windows the standard argv comes through the system ANSI code page
// (CP-1252 in en-US, CP-1250 in Polish locales, etc.), which mangles
// any non-ASCII characters that aren't in the host code page. We
// instead fetch the original UTF-16 command line, split it with
// CommandLineToArgvW, and convert each arg to UTF-8 -- which is what
// our whole pipeline (espeak-ng, file I/O, internal string handling)
// expects. After this the CLI accepts Polish, German umlauts, and any
// other UTF-8 input verbatim regardless of the host code page.
static std::vector<std::string> get_utf8_argv(int /*orig_argc*/) {
    std::vector<std::string> out;
    int argc_w = 0;
    LPWSTR* argv_w = ::CommandLineToArgvW(::GetCommandLineW(), &argc_w);
    if (!argv_w) return out;
    out.reserve(static_cast<std::size_t>(argc_w));
    for (int i = 0; i < argc_w; ++i) {
        const int sz = ::WideCharToMultiByte(
            CP_UTF8, 0, argv_w[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s;
        if (sz > 1) {
            s.resize(static_cast<std::size_t>(sz) - 1);
            ::WideCharToMultiByte(
                CP_UTF8, 0, argv_w[i], -1, s.data(), sz, nullptr, nullptr);
        }
        out.push_back(std::move(s));
    }
    ::LocalFree(argv_w);
    // Also retag stdout so any UTF-8 we print (filenames, error
    // messages) renders correctly in modern terminals.
    ::SetConsoleOutputCP(CP_UTF8);
    return out;
}
#endif

using namespace klattalker;

static void usage(const char* argv0) {
    std::fprintf(stderr,
        "Usage: %s --text \"sentence\" [options]\n"
        "  Options:\n"
        "    --voice dan|lily|john|kate|josh|frank|doris   [dan]\n"
        "    --engine klsyn80|klsyn88 [klsyn88]\n"
        "    --intonation fujisaki|klatt  [fujisaki]\n"
        "    --language us|uk|pl|de|zh|ja|it|fr  US English (CMU dict) / RP British /\n"
        "                             Polish / German [us]\n"
        "    --quality cq|hq          cq=11025 Hz, hq=22050 Hz [hq]\n"
        "    --sample-rate HZ         explicit override [22050]\n"
        "    --f0 HZ                  base F0 [90 male / 200 female]\n"
        "    --alpha RAD              Fujisaki phrase-command omega [3.0]\n"
        "    --beta  RAD              Fujisaki accent-command omega [20.0]\n"
        "    --Ap N                   Fujisaki phrase magnitude [0.30]\n"
        "    --Aa N                   Fujisaki accent magnitude [0.20]\n"
        "    --out PATH               output WAV path [out.wav]\n",
        argv0);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    // Swap argv for UTF-8 versions read from the original Windows
    // command line. We keep them alive in static storage so the rest
    // of the function can use the same `argv[i]` pointers it always did.
    static std::vector<std::string> utf8_args = get_utf8_argv(argc);
    static std::vector<char*> utf8_argv_ptrs;
    if (!utf8_args.empty()) {
        utf8_argv_ptrs.clear();
        utf8_argv_ptrs.reserve(utf8_args.size());
        for (auto& s : utf8_args) utf8_argv_ptrs.push_back(s.data());
        argc = static_cast<int>(utf8_args.size());
        argv = utf8_argv_ptrs.data();
    }
#endif
    std::string text, voice = "dan";
    std::string engine_s = "klsyn88";
    std::string out_path = "out.wav";
    std::string quality_s;
    std::string intonation_s = "fujisaki";
    std::string language_s = "us";
    double f0 = 0.0;
    double alpha = 3.0, beta = 20.0, Ap = 0.30, Aa = 0.20;
    // Track which Fujisaki params were explicitly set on the CLI so we
    // can leave the synth's variant-specific defaults intact otherwise.
    bool alpha_set = false, beta_set = false, Ap_set = false, Aa_set = false;
    int sample_rate = 22050;
    bool sample_rate_explicit = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--text")             text = next("--text");
        else if (a == "--voice")       voice = next("--voice");
        else if (a == "--engine")      engine_s = next("--engine");
        else if (a == "--f0")          f0 = std::atof(next("--f0"));
        else if (a == "--intonation")  intonation_s = next("--intonation");
        else if (a == "--alpha") { alpha = std::atof(next("--alpha")); alpha_set = true; }
        else if (a == "--beta")  { beta  = std::atof(next("--beta"));  beta_set  = true; }
        else if (a == "--Ap")    { Ap    = std::atof(next("--Ap"));    Ap_set    = true; }
        else if (a == "--Aa")    { Aa    = std::atof(next("--Aa"));    Aa_set    = true; }
        else if (a == "--quality")     quality_s = next("--quality");
        else if (a == "--language" || a == "--lang")
                                        language_s = next("--language");
        else if (a == "--sample-rate") {
            sample_rate = std::atoi(next("--sample-rate"));
            sample_rate_explicit = true;
        }
        else if (a == "--out" || a == "-o") out_path = next("--out");
        else if (a == "--help" || a == "-h") { usage(argv[0]); return 0; }
        else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    if (text.empty()) {
        std::fprintf(stderr, "need --text\n");
        usage(argv[0]);
        return 2;
    }

    if (!sample_rate_explicit) {
        if (quality_s == "cq") sample_rate = 11025;
        else if (quality_s == "hq") sample_rate = 22050;
        else if (!quality_s.empty()) {
            std::fprintf(stderr, "unknown --quality %s (use cq|hq)\n",
                         quality_s.c_str());
            return 2;
        }
    }

    EngineKind engine = (engine_s == "klsyn80") ? EngineKind::Klsyn80
                                                : EngineKind::Klsyn88;
    // Contour kind is now driven entirely by sentence-final punctuation
    // inside text_to_phonemes. The fallback for the very last sentence
    // (or text that doesn't end with punctuation) is Statement.
    ContourKind contour = ContourKind::Statement;

    InflectionKind inf = InflectionKind::Fujisaki;
    if (intonation_s == "klatt" || intonation_s == "klatt_rules") {
        inf = InflectionKind::KlattRules;
    } else if (intonation_s != "fujisaki") {
        std::fprintf(stderr,
            "unknown --intonation %s (use fujisaki|klatt)\n",
            intonation_s.c_str());
        return 2;
    }

    Variant variant = Variant::USEnglish;
    if (language_s == "uk" || language_s == "rp" || language_s == "british") {
        variant = Variant::UKEnglish;
    } else if (language_s == "pl" || language_s == "polish") {
        variant = Variant::Polish;
    } else if (language_s == "de" || language_s == "german"
            || language_s == "deutsch") {
        variant = Variant::German;
    } else if (language_s == "zh" || language_s == "cn"
            || language_s == "cmn" || language_s == "mandarin"
            || language_s == "chinese") {
        variant = Variant::Mandarin;
    } else if (language_s == "ja" || language_s == "jp"
            || language_s == "japanese") {
        variant = Variant::Japanese;
    } else if (language_s == "it" || language_s == "italian"
            || language_s == "italiano") {
        variant = Variant::Italian;
    } else if (language_s == "fr" || language_s == "french"
            || language_s == "français") {
        variant = Variant::French;
    } else if (language_s != "us" && language_s != "english") {
        std::fprintf(stderr,
            "unknown --language %s (use us|uk|pl|de|zh|ja|it|fr)\n",
            language_s.c_str());
        return 2;
    }

    // Voice presets (case-insensitive):
    //   dan   -> default modal male  (was "male")
    //   lily  -> default modal female (was "female")
    //   john  -> deep modal male
    //   kate  -> breathy female
    //   josh  -> child voice
    std::string v_lower;
    v_lower.reserve(voice.size());
    for (char c : voice) v_lower.push_back(static_cast<char>(std::tolower(
        static_cast<unsigned char>(c))));
    const bool is_john  = (v_lower == "john");
    const bool is_kate  = (v_lower == "kate");
    const bool is_josh  = (v_lower == "josh");
    const bool is_frank = (v_lower == "frank");
    const bool is_doris = (v_lower == "doris");
    const bool is_dan   = (v_lower == "dan");
    const bool is_lily  = (v_lower == "lily");
    if (!is_john && !is_kate && !is_josh && !is_frank && !is_doris
        && !is_dan && !is_lily) {
        std::fprintf(stderr,
            "unknown --voice %s (use dan|lily|john|kate|josh|frank|doris)\n",
            voice.c_str());
        return 2;
    }

    KlattSynth synth = [&]() {
        if (is_john)  return KlattSynth::make_john(engine, sample_rate, inf, variant);
        if (is_kate)  return KlattSynth::make_kate(engine, sample_rate, inf, variant);
        if (is_josh)  return KlattSynth::make_josh(engine, sample_rate, inf, variant);
        if (is_frank) return KlattSynth::make_frank(engine, sample_rate, inf, variant);
        if (is_doris) return KlattSynth::make_doris(engine, sample_rate, inf, variant);
        if (is_lily)
            return KlattSynth::make_female(f0 > 0 ? f0 : 200.0, engine,
                                           sample_rate, inf, variant);
        return KlattSynth::make_male(f0 > 0 ? f0 : 90.0, engine,
                                     sample_rate, inf, variant);
    }();

    auto& fj = synth.fujisaki();
    // Only override the variant-specific Fujisaki defaults if the user
    // explicitly passed the flag -- otherwise leave the per-language
    // tuning the synth constructor set up.
    if (alpha_set) fj.default_phrase_omega = alpha;
    if (beta_set)  fj.default_accent_omega = beta;
    if (Ap_set)    fj.default_phrase_Ap    = Ap;
    if (Aa_set)    fj.default_accent_Aa    = Aa;

    auto audio = synth.synthesize_text(text, contour);

    if (!write_wav_mono(out_path, audio, sample_rate)) {
        std::fprintf(stderr, "failed to write %s\n", out_path.c_str());
        return 1;
    }
    std::printf("wrote %s (%.2f s)\n", out_path.c_str(),
                static_cast<double>(audio.size()) / sample_rate);

    return 0;
}
