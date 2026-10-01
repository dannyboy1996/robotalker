// Demo: render a small set of WAVs showing the Klatt engines + Fujisaki
// intonation. All output lands in examples_out/ next to wherever the
// demo is executed.

#include "klattalker/inflection.hpp"
#include "klattalker/synth.hpp"
#include "klattalker/wav.hpp"
#include <cstdio>
#include <filesystem>

using namespace klattalker;

int main() {
    std::filesystem::create_directories("examples_out");
    const int fs = 22050;

    // --- Hillenbrand sustained vowels, M + F, klsyn88 ---------------------
    const char* vowels[] = {"iy","ih","ey","eh","ae","aa",
                            "ao","ow","uh","uw","ah","er"};
    for (auto* sex_label : {"male", "female"}) {
        auto synth = (std::string(sex_label) == "male")
            ? KlattSynth::make_male(70.0, EngineKind::Klsyn88, fs)
            : KlattSynth::make_female(200.0, EngineKind::Klsyn88, fs);
        for (auto* v : vowels) {
            std::vector<PhonemeItem> items;
            PhonemeItem it; it.key = v; it.duration_ms = 600.0;
            items.push_back(it);
            auto audio = synth.synthesize_phonemes(items, ContourKind::Flat);
            std::string path = std::string("examples_out/vowel_") + v
                             + "_" + sex_label + ".wav";
            write_wav_mono(path, audio, fs);
            std::printf("  %s\n", path.c_str());
        }
    }

    // --- engine A/B on the same vowel -------------------------------------
    for (auto kind : {EngineKind::Klsyn80, EngineKind::Klsyn88}) {
        auto synth = KlattSynth::make_male(70.0, kind, fs);
        std::vector<PhonemeItem> items;
        PhonemeItem it; it.key = "ah"; it.duration_ms = 600.0;
        items.push_back(it);
        auto audio = synth.synthesize_phonemes(items, ContourKind::Flat);
        const char* name = (kind == EngineKind::Klsyn80) ? "klsyn80" : "klsyn88";
        std::string path = std::string("examples_out/engine_") + name + "_ah.wav";
        write_wav_mono(path, audio, fs);
        std::printf("  %s\n", path.c_str());
    }

    // --- words via embedded G2P + Fujisaki intonation ---------------------
    {
        auto synth = KlattSynth::make_male(70.0, EngineKind::Klsyn88, fs);
        auto audio = synth.synthesize_text("hello world");
        write_wav_mono("examples_out/hello_world_male.wav", audio, fs);
        std::printf("  examples_out/hello_world_male.wav\n");
    }
    {
        auto synth = KlattSynth::make_female(200.0, EngineKind::Klsyn88, fs);
        auto audio = synth.synthesize_text("are you ready?");
        write_wav_mono("examples_out/question_female.wav", audio, fs);
        std::printf("  examples_out/question_female.wav\n");
    }

    // --- Fujisaki parameter sweep -----------------------------------------
    // Vary the phrase-command magnitude Ap and the accent-command speed beta
    // so you can hear what each knob does on the same utterance.
    {
        const double Ap_values[] = {0.10, 0.30, 0.50};
        for (double Ap : Ap_values) {
            auto synth = KlattSynth::make_male(70.0, EngineKind::Klsyn88, fs);
            synth.fujisaki().default_phrase_Ap = Ap;
            auto audio = synth.synthesize_text("hello world");
            char buf[80];
            std::snprintf(buf, sizeof(buf), "examples_out/fujisaki_Ap_%.2f.wav", Ap);
            write_wav_mono(buf, audio, fs);
            std::printf("  %s\n", buf);
        }
        const double beta_values[] = {10.0, 20.0, 30.0};
        for (double beta : beta_values) {
            auto synth = KlattSynth::make_male(70.0, EngineKind::Klsyn88, fs);
            synth.fujisaki().default_accent_omega = beta;
            auto audio = synth.synthesize_text("hello world");
            char buf[80];
            std::snprintf(buf, sizeof(buf), "examples_out/fujisaki_beta_%.0f.wav", beta);
            write_wav_mono(buf, audio, fs);
            std::printf("  %s\n", buf);
        }
    }

    // --- Explicit FujisakiCommand list ------------------------------------
    // Reproduces the kind of command-set you'd get out of the Fujisaki
    // analyser: one phrase impulse + two accent rectangles. Tweak these
    // values to dial the contour by hand.
    {
        auto synth = KlattSynth::make_male(70.0, EngineKind::Klsyn88, fs);
        std::vector<FujisakiCommand> cmds = {
            // type, onset, offset, integratedAmplitude, omega
            {FujisakiCommandType::Phrase, 0.00, 0.00, 0.40, 3.0},
            {FujisakiCommandType::Accent, 0.20, 0.45, 0.20, 20.0},
            {FujisakiCommandType::Accent, 0.65, 0.95, 0.25, 20.0},
        };
        std::vector<PhonemeItem> items = {
            {"sil", 80, false}, {"h", 80, false}, {"ah", 150, true},
            {"l", 60, false}, {"ow", 220, false}, {"sil", 80, false},
            {"w", 60, false}, {"er", 200, true}, {"l", 60, false},
            {"d", 80, false}, {"sil", 80, false},
        };
        auto audio = synth.synthesize_phonemes(items,
            ContourKind::Statement, cmds);
        write_wav_mono("examples_out/fujisaki_explicit.wav", audio, fs);
        std::printf("  examples_out/fujisaki_explicit.wav\n");
    }

    return 0;
}
