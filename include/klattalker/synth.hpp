#pragma once
#include "klattalker/data.hpp"
#include "klattalker/engine.hpp"
#include "klattalker/inflection.hpp"
#include "klattalker/sequencer.hpp"
#include <memory>
#include <string>
#include <vector>

namespace klattalker {

// Voice-quality parameters consumed by KLGLOTT88 (Klatt & Klatt 1990).
// Male defaults are modal; female defaults add open quotient + spectral
// tilt for the characteristic slightly-breathy quality of an adult
// female voice. The values follow the male/female contrasts in Table 3
// of Klatt & Klatt 1990.
struct VoiceQuality {
    double open_quotient = 0.60;       // OQ: 0.4 modal, 0.7 breathy
    double spectral_tilt_db = 0.0;     // TL: extra rolloff at 3 kHz (dB)
    double flutter = 0.005;            // FL: low-freq F0 modulation depth
    double diplophonia = 0.0;          // DI: alternate-pulse amp asymmetry
    double bandwidth_scale = 1.0;      // multiplier on formant bandwidths
    // Multiplier on formant CENTRE frequencies. Smaller for longer
    // vocal tracts (deep male voices), larger for shorter tracts
    // (children). Default 1.0 = use the phoneme table value as-is.
    double formant_scale = 1.0;
    // Per-voice output gain to keep peak levels in range. Wider
    // bandwidth + larger formant scale (Kate, Josh) push cascade output
    // higher and clip; trim with this.
    double output_gain = 1.0;
};

struct Speaker {
    Sex sex = Sex::Male;
    Variant variant = Variant::USEnglish;
    double base_f0 = 90.0;
    EngineKind engine = EngineKind::Klsyn88;
    InflectionKind intonation = InflectionKind::Fujisaki;
    VoiceQuality voice_quality{};
};

class KlattSynth {
public:
    KlattSynth(Speaker speaker, int sample_rate = 22050);

    // Male defaults: baseline 90 Hz. With Fujisaki defaults (Ap=0.30,
    // omega_p=3) the contour peaks around 150 Hz on the first accent,
    // matching the natural ~90-150 Hz range of an adult male voice.
    static KlattSynth make_male(double base_f0 = 90.0,
                                EngineKind engine = EngineKind::Klsyn88,
                                int sample_rate = 22050,
                                InflectionKind intonation = InflectionKind::Fujisaki,
                                Variant variant = Variant::USEnglish);
    static KlattSynth make_female(double base_f0 = 200.0,
                                  EngineKind engine = EngineKind::Klsyn88,
                                  int sample_rate = 22050,
                                  InflectionKind intonation = InflectionKind::Fujisaki,
                                  Variant variant = Variant::USEnglish);

    // ---- Named voices ----------------------------------------------
    //
    // John -- big modal male, low pitch, slight chest resonance.
    //   Lower F0 (~75 Hz), tighter open quotient, narrower bandwidths,
    //   slightly longer vocal tract (formants scaled 0.93x).
    //
    // Kate -- breathy female, slightly raised pitch.
    //   Higher open quotient (~0.78), pronounced spectral tilt, more
    //   flutter, slightly shorter vocal tract.
    //
    // Josh -- child voice (~8-year-old boy soprano).
    //   High F0 (~290 Hz), short vocal tract -> formant_scale=1.20,
    //   widened bandwidths.
    static KlattSynth make_john(EngineKind engine = EngineKind::Klsyn88,
                                int sample_rate = 22050,
                                InflectionKind intonation = InflectionKind::Fujisaki,
                                Variant variant = Variant::USEnglish);
    static KlattSynth make_kate(EngineKind engine = EngineKind::Klsyn88,
                                int sample_rate = 22050,
                                InflectionKind intonation = InflectionKind::Fujisaki,
                                Variant variant = Variant::USEnglish);
    static KlattSynth make_josh(EngineKind engine = EngineKind::Klsyn88,
                                int sample_rate = 22050,
                                InflectionKind intonation = InflectionKind::Fujisaki,
                                Variant variant = Variant::USEnglish);

    // Frank -- elderly male voice (~70s). Counter to intuition, male
    // F0 RISES with age (110 Hz → ~135 Hz by the 70s, Harnsberger 2008)
    // due to vocal fold stiffening. Flutter is also markedly higher --
    // the older larynx has more period-to-period perturbation. The
    // voice is slightly breathier on average and the vocal tract is
    // less precise (wider formant bandwidths).
    static KlattSynth make_frank(EngineKind engine = EngineKind::Klsyn88,
                                 int sample_rate = 22050,
                                 InflectionKind intonation = InflectionKind::Fujisaki,
                                 Variant variant = Variant::USEnglish);

    // Doris -- elderly female voice. Unlike males, female F0 stays
    // relatively stable with age but drifts down slightly; spectrum
    // becomes breathier with more aspiration leakage. Stoicheff 1981.
    static KlattSynth make_doris(EngineKind engine = EngineKind::Klsyn88,
                                 int sample_rate = 22050,
                                 InflectionKind intonation = InflectionKind::Fujisaki,
                                 Variant variant = Variant::USEnglish);

    int sample_rate() const { return sample_rate_; }
    const Speaker& speaker() const { return speaker_; }

    // Render a phoneme list.
    //   * contour=Statement|Question selects auto-derived default Fujisaki
    //     commands (a second phrase impulse near the end for questions).
    //   * fujisaki_commands overrides the auto-derived set — pass commands
    //     directly to reproduce contours analysed by the FujisakiEstimation
    //     library.
    std::vector<float> synthesize_phonemes(
        const std::vector<PhonemeItem>& items,
        ContourKind contour = ContourKind::Statement,
        std::span<const FujisakiCommand> fujisaki_commands = {});

    std::vector<float> synthesize_text(
        const std::string& text,
        ContourKind contour = ContourKind::Statement,
        std::span<const FujisakiCommand> fujisaki_commands = {});

    // Tweak the Fujisaki defaults (alpha, beta, Ap, Aa) before synthesis.
    FujisakiModel& fujisaki() { return fujisaki_; }
    const FujisakiModel& fujisaki() const { return fujisaki_; }

    // Tweak the Klatt-rules F0 model (declination, accent peak, etc.).
    KlattF0Model& klatt_f0() { return klatt_f0_; }
    const KlattF0Model& klatt_f0() const { return klatt_f0_; }

    TonalF0Model& tonal_f0() { return tonal_f0_; }
    const TonalF0Model& tonal_f0() const { return tonal_f0_; }

    JapaneseF0Model& japanese_f0() { return japanese_f0_; }
    const JapaneseF0Model& japanese_f0() const { return japanese_f0_; }

private:
    Speaker speaker_;
    int sample_rate_;
    std::unique_ptr<Engine> engine_;
    FujisakiModel fujisaki_;
    KlattF0Model klatt_f0_;
    TonalF0Model tonal_f0_;
    JapaneseF0Model japanese_f0_;
};

}  // namespace klattalker
