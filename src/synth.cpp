#include "klattalker/synth.hpp"
#include "klattalker/text.hpp"

namespace klattalker {

KlattSynth::KlattSynth(Speaker speaker, int sample_rate)
    : speaker_(speaker), sample_rate_(sample_rate),
      engine_(make_engine(speaker.engine, sample_rate,
                          speaker.voice_quality)) {
    fujisaki_.baseline_f0 = speaker.base_f0;
    klatt_f0_.base_f0 = speaker.base_f0;
    // Mandarin tonal model midline: speaker base_f0 sits at Chao level 3
    // (the middle of the tonal range). Auto-selected when variant=Mandarin
    // -- not exposed as a CLI flag, since it's the only sensible choice.
    tonal_f0_.midline_f0 = speaker.base_f0 * 1.25;
    japanese_f0_.base_f0 = speaker.base_f0;

    // Per-variant Fujisaki tuning. The defaults in FujisakiModel are
    // calibrated for English; other languages have notably different
    // overall F0 dynamics. CLI flags --alpha / --beta / --Ap / --Aa
    // override these on a per-flag basis.
    switch (speaker.variant) {
        case Variant::Polish:
            // Polish stress is predictable (penultimate) and the
            // realisation is mostly durational rather than F0-prominent.
            // Demenko 1999 / Wagner 2014: F0 range is narrower than
            // English and accents look more like brief excursions
            // than English-style focal peaks. Smaller Aa + smaller Ap
            // gives the comparatively level Polish intonation.
            fujisaki_.default_phrase_omega = 2.6;   // gentler phrase rise
            fujisaki_.default_accent_omega = 18.0;  // slightly slower accent
            fujisaki_.default_phrase_Ap    = 0.24;
            fujisaki_.default_accent_Aa    = 0.13;
            break;
        case Variant::German:
            // German declaratives have a clear initial phrase command
            // and a marked final fall (Grice & Baumann 2002 GToBI).
            // The previous tuning (Ap=0.34 Aa=0.18 omega_a=22) was too
            // peaked -- a German speaker called the result "offensively
            // bad". Gentler phrase rise + softer accents matches what
            // Mixdorff 1998 fits for spontaneous German.
            fujisaki_.default_phrase_omega = 2.8;
            fujisaki_.default_accent_omega = 18.0;
            fujisaki_.default_phrase_Ap    = 0.26;
            fujisaki_.default_accent_Aa    = 0.13;
            break;
        case Variant::French:
            // French is syllable-timed (each syllable roughly equal
            // length) with characteristic phrase-final lengthening +
            // pitch rise on the final stressed syllable. The "accent
            // tonique" is mostly a duration cue, not F0, so accent
            // magnitudes are modest. Vaissière 1983; Jun & Fougeron
            // 2002 IF model fits. Larger phrase command than English
            // (French intonation is markedly contoured).
            fujisaki_.default_phrase_omega = 2.6;
            fujisaki_.default_accent_omega = 18.0;
            fujisaki_.default_phrase_Ap    = 0.34;
            fujisaki_.default_accent_Aa    = 0.15;
            break;
        case Variant::Italian:
            // Standard Italian has the most expressive, "melodic"
            // intonation of the European languages we cover here.
            // Avesani 1995 (Italian ToBI) and Marotta & Sorianello 1998
            // document a large phrase command (the gradual L→H rise
            // across the prosodic phrase) and prominent per-accent
            // peaks. The phrase rise is gentler / slower than English
            // because Italian phrases tend to be longer, but the
            // accents themselves are sharper. Mixdorff 2002's Fujisaki
            // fits for Italian sit around Ap=0.38, Aa=0.22 -- we
            // adopt similar values.
            fujisaki_.default_phrase_omega = 2.5;   // gentler phrase rise
            fujisaki_.default_accent_omega = 20.0;
            fujisaki_.default_phrase_Ap    = 0.38;  // bigger phrase command
            fujisaki_.default_accent_Aa    = 0.22;  // prominent accents
            break;
        case Variant::USEnglish:
        case Variant::UKEnglish:
        case Variant::Mandarin:
        case Variant::Japanese:
            // Keep the FujisakiModel struct defaults (calibrated for
            // English). Mandarin and Japanese use their own dedicated
            // F0 models -- Fujisaki defaults here are irrelevant.
            break;
    }

    if (speaker.variant == Variant::Mandarin) {
        speaker_.intonation = InflectionKind::Tonal;
    } else if (speaker.variant == Variant::Japanese) {
        speaker_.intonation = InflectionKind::Japanese;
    }
}

KlattSynth KlattSynth::make_male(double base_f0, EngineKind eng, int fs,
                                 InflectionKind intonation,
                                 Variant variant) {
    Speaker sp;
    sp.sex = Sex::Male;
    sp.variant = variant;
    sp.base_f0 = base_f0;
    sp.engine = eng;
    sp.intonation = intonation;
    // Modal voice — Klatt & Klatt 1990 male defaults.
    sp.voice_quality.open_quotient   = 0.55;
    sp.voice_quality.spectral_tilt_db = 0.0;
    sp.voice_quality.flutter         = 0.005;
    sp.voice_quality.diplophonia     = 0.0;
    sp.voice_quality.bandwidth_scale = 1.0;
    // Mandarin retroflex sibilants + apical vowels push peaks higher
    // than the average; need headroom to avoid clipping.
    if (variant == Variant::Mandarin) sp.voice_quality.output_gain = 0.85;
    return KlattSynth(sp, fs);
}

KlattSynth KlattSynth::make_female(double base_f0, EngineKind eng, int fs,
                                   InflectionKind intonation,
                                   Variant variant) {
    Speaker sp;
    sp.sex = Sex::Female;
    sp.variant = variant;
    sp.base_f0 = base_f0;
    sp.engine = eng;
    sp.intonation = intonation;
    // Adult female voice tends slightly breathy with elevated open
    // quotient, mild spectral tilt at 3 kHz, and ~10% wider formant
    // bandwidths. Values from Klatt & Klatt 1990 Table 3 averages.
    sp.voice_quality.open_quotient   = 0.70;
    sp.voice_quality.spectral_tilt_db = 6.0;
    sp.voice_quality.flutter         = 0.008;
    sp.voice_quality.diplophonia     = 0.0;
    sp.voice_quality.bandwidth_scale = 1.10;
    sp.voice_quality.output_gain     = 0.90;   // headroom to avoid clip
    return KlattSynth(sp, fs);
}

KlattSynth KlattSynth::make_john(EngineKind eng, int fs,
                                 InflectionKind intonation,
                                 Variant variant) {
    Speaker sp;
    sp.sex = Sex::Male;
    sp.variant = variant;
    sp.base_f0 = 75.0;                  // deep
    sp.engine = eng;
    sp.intonation = intonation;
    // Strong modal voice with a slight chest character. Low open
    // quotient + slight negative spectral tilt = the "barrel" sound.
    sp.voice_quality.open_quotient    = 0.48;
    sp.voice_quality.spectral_tilt_db = -1.5;
    sp.voice_quality.flutter          = 0.004;
    sp.voice_quality.diplophonia      = 0.0;
    sp.voice_quality.bandwidth_scale  = 0.92;
    sp.voice_quality.formant_scale    = 0.93;   // longer vocal tract
    return KlattSynth(sp, fs);
}

KlattSynth KlattSynth::make_kate(EngineKind eng, int fs,
                                 InflectionKind intonation,
                                 Variant variant) {
    Speaker sp;
    sp.sex = Sex::Female;
    sp.variant = variant;
    sp.base_f0 = 215.0;                 // slightly elevated
    sp.engine = eng;
    sp.intonation = intonation;
    // Audibly breathy: high OQ, generous spectral tilt, more flutter.
    // Bandwidths a touch wider than default female to soften further.
    sp.voice_quality.open_quotient    = 0.78;
    sp.voice_quality.spectral_tilt_db = 9.0;
    sp.voice_quality.flutter          = 0.012;
    sp.voice_quality.diplophonia      = 0.0;
    sp.voice_quality.bandwidth_scale  = 1.18;
    sp.voice_quality.formant_scale    = 1.04;
    sp.voice_quality.output_gain      = 0.82;
    return KlattSynth(sp, fs);
}

KlattSynth KlattSynth::make_doris(EngineKind eng, int fs,
                                  InflectionKind intonation,
                                  Variant variant) {
    Speaker sp;
    sp.sex = Sex::Female;
    sp.variant = variant;
    sp.base_f0 = 195.0;             // slight drop with age
    sp.engine = eng;
    sp.intonation = intonation;
    // Older female voice: breathier (higher OQ), more flutter, more
    // spectral tilt. Stoicheff 1981 on aging female voice quality.
    sp.voice_quality.open_quotient    = 0.78;
    sp.voice_quality.spectral_tilt_db = 7.5;
    sp.voice_quality.flutter          = 0.018;
    sp.voice_quality.diplophonia      = 0.04;
    sp.voice_quality.bandwidth_scale  = 1.20;
    sp.voice_quality.formant_scale    = 1.04;
    sp.voice_quality.output_gain      = 0.84;
    return KlattSynth(sp, fs);
}

KlattSynth KlattSynth::make_frank(EngineKind eng, int fs,
                                  InflectionKind intonation,
                                  Variant variant) {
    Speaker sp;
    sp.sex = Sex::Male;
    sp.variant = variant;
    sp.base_f0 = 135.0;             // elderly male F0 rises with age
    sp.engine = eng;
    sp.intonation = intonation;
    // Pronounced flutter (period perturbation, the trademark of an
    // older larynx), slightly breathy, broader formants from less
    // precise vocal tract control.
    sp.voice_quality.open_quotient    = 0.68;
    sp.voice_quality.spectral_tilt_db = 5.0;
    sp.voice_quality.flutter          = 0.022;
    sp.voice_quality.diplophonia      = 0.05;     // occasional irregular pulses
    sp.voice_quality.bandwidth_scale  = 1.15;
    sp.voice_quality.formant_scale    = 0.98;
    sp.voice_quality.output_gain      = 0.92;
    return KlattSynth(sp, fs);
}

KlattSynth KlattSynth::make_josh(EngineKind eng, int fs,
                                 InflectionKind intonation,
                                 Variant variant) {
    Speaker sp;
    sp.sex = Sex::Male;                 // small-male source spectrum
    sp.variant = variant;
    sp.base_f0 = 290.0;                 // boy soprano
    sp.engine = eng;
    sp.intonation = intonation;
    // Children have shorter vocal tracts -> formants land ~15-25%
    // higher than adult male. F0 also much higher. Bandwidths wider.
    sp.voice_quality.open_quotient    = 0.62;
    sp.voice_quality.spectral_tilt_db = 4.0;
    sp.voice_quality.flutter          = 0.016;
    sp.voice_quality.diplophonia      = 0.0;
    sp.voice_quality.bandwidth_scale  = 1.20;
    sp.voice_quality.formant_scale    = 1.20;   // short tract
    sp.voice_quality.output_gain      = 0.65;
    return KlattSynth(sp, fs);
}

std::vector<float> KlattSynth::synthesize_phonemes(
    const std::vector<PhonemeItem>& items,
    ContourKind contour,
    std::span<const FujisakiCommand> fujisaki_commands) {

    auto tracks = render_tracks(items, speaker_.sex, speaker_.variant,
                                sample_rate_);
    std::vector<double> f0(tracks.n_samples);

    if (speaker_.intonation == InflectionKind::Tonal) {
        tonal_f0_.render(sample_rate_, tracks.voiced_mask,
                         tracks.phoneme_start_samples,
                         tracks.phoneme_tones,
                         tracks.phoneme_tone_citation, f0);
    } else if (speaker_.intonation == InflectionKind::Japanese) {
        japanese_f0_.render(sample_rate_, tracks.voiced_mask,
                            tracks.sentence_starts_s,
                            tracks.sentence_contours,
                            tracks.total_s,
                            tracks.mora_nucleus_samples,
                            tracks.mora_nucleus_pitch, f0);
    } else if (speaker_.intonation == InflectionKind::KlattRules) {
        klatt_f0_.render(sample_rate_, tracks.voiced_mask,
                         tracks.stress_times_s,
                         tracks.sentence_starts_s,
                         tracks.sentence_contours, contour, f0);
    } else {
        std::vector<FujisakiCommand> auto_cmds;
        if (fujisaki_commands.empty()) {
            auto_cmds = default_fujisaki_commands(
                tracks.stress_times_s,
                tracks.sentence_starts_s,
                tracks.sentence_contours,
                tracks.total_s, contour, fujisaki_);
            fujisaki_commands = auto_cmds;
        }
        fujisaki_.render(sample_rate_, tracks.voiced_mask,
                         fujisaki_commands, f0);
    }

    // Creaky-voice / glottalization F0 modifications. Three flavours,
    // all characteristic of American English (Henton & Bladon 1988;
    // Yuasa 2010; Davidson 2021):
    //
    //   * Sentence-final creak: last ~130 ms before utterance pause
    //     drops F0 ~22%. The classic "vocal fry" tail.
    //   * Glottal-stop creak: ~40 ms BEFORE a /q/ glottal stop, F0
    //     dips ~15% (Dilley et al. 1996 -- speakers preglottalise).
    //   * Sentence-initial creak: first ~80 ms of voiced material in
    //     each sentence starts ~12% below baseline and rises smoothly.
    //
    // Skipped for tonal/mora-timed languages (Mandarin/Japanese) where
    // F0 carries lexical meaning and creak would corrupt it.
    if ((speaker_.variant == Variant::USEnglish
         || speaker_.variant == Variant::UKEnglish)
        && speaker_.intonation != InflectionKind::Tonal
        && speaker_.intonation != InflectionKind::Japanese) {
        // ---- Sentence-final creak --------------------------------
        const double creak_s = 0.130;
        const std::size_t creak_samples =
            static_cast<std::size_t>(creak_s * sample_rate_);
        std::vector<std::size_t> sent_end_samples;
        for (std::size_t k = 1; k < tracks.sentence_starts_s.size(); ++k) {
            sent_end_samples.push_back(static_cast<std::size_t>(
                tracks.sentence_starts_s[k] * sample_rate_));
        }
        sent_end_samples.push_back(tracks.n_samples);
        for (std::size_t end : sent_end_samples) {
            std::size_t last = end;
            while (last > 0 && !tracks.voiced_mask[last - 1]) --last;
            if (last == 0) continue;
            const std::size_t start = (last > creak_samples)
                ? last - creak_samples : 0;
            for (std::size_t i = start; i < last; ++i) {
                const double t = static_cast<double>(i - start) /
                                 std::max<std::size_t>(1, last - start);
                const double mul = 1.0 - 0.22 * (t * t);
                if (tracks.voiced_mask[i]) f0[i] *= mul;
            }
        }
        // ---- Sentence-initial creak ------------------------------
        const double init_creak_s = 0.080;
        const std::size_t init_samples =
            static_cast<std::size_t>(init_creak_s * sample_rate_);
        for (double start_s : tracks.sentence_starts_s) {
            const std::size_t start_samp =
                static_cast<std::size_t>(start_s * sample_rate_);
            // Walk forward to the first voiced sample after sentence start
            std::size_t first = start_samp;
            while (first < tracks.n_samples
                   && !tracks.voiced_mask[first]) ++first;
            if (first >= tracks.n_samples) continue;
            const std::size_t end = std::min(first + init_samples,
                                             tracks.n_samples);
            for (std::size_t i = first; i < end; ++i) {
                const double t = static_cast<double>(i - first) /
                                 std::max<std::size_t>(1, end - first);
                // Start at 0.88 (12% below), rise smoothly back to 1.0
                const double mul = 0.88 + 0.12 * t;
                if (tracks.voiced_mask[i]) f0[i] *= mul;
            }
        }
        // ---- Glottal-stop preglottalisation creak ----------------
        // For each /q/ in items, look up its sample position and
        // modify the ~40 ms of voiced material just before it.
        const double glo_creak_s = 0.040;
        const std::size_t glo_samples =
            static_cast<std::size_t>(glo_creak_s * sample_rate_);
        for (std::size_t k = 0; k < items.size(); ++k) {
            if (items[k].key != "q") continue;
            const std::size_t q_start = tracks.phoneme_start_samples[k];
            const std::size_t pre_start = (q_start > glo_samples)
                ? q_start - glo_samples : 0;
            for (std::size_t i = pre_start; i < q_start; ++i) {
                const double t = static_cast<double>(i - pre_start) /
                                 std::max<std::size_t>(1, q_start - pre_start);
                // Smooth dip down to 0.85 of normal F0
                const double mul = 1.0 - 0.15 * t;
                if (tracks.voiced_mask[i]) f0[i] *= mul;
            }
        }
    }

    return engine_->render(tracks, f0);
}

std::vector<float> KlattSynth::synthesize_text(
    const std::string& text,
    ContourKind contour,
    std::span<const FujisakiCommand> fujisaki_commands) {
    auto items = text_to_phonemes(text, speaker_.variant);
    // Detect fallback contour kind from the final character so single-
    // sentence text without per-chunk markings still gets the right
    // terminal behaviour.
    if (!text.empty()) {
        const char last = text.back();
        if (last == '?')      contour = ContourKind::Question;
        else if (last == '!') contour = ContourKind::Exclamation;
    }
    return synthesize_phonemes(items, contour, fujisaki_commands);
}

}  // namespace klattalker
