#include "klattalker/inflection.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace klattalker {

// Matches hitonanode/FujisakiEstimation src/fujisaki/fujisaki.hpp inline
// definitions exactly.
static inline double phrase_impulse(double omega, double t) {
    if (t <= 0.0) return 0.0;
    return omega * omega * t * std::exp(-omega * t);
}
static inline double accent_step(double omega, double t) {
    if (t <= 0.0) return 0.0;
    return 1.0 - (1.0 + omega * t) * std::exp(-omega * t);
}

void FujisakiModel::render(int sample_rate,
                           std::span<const uint8_t> voiced_mask,
                           std::span<const FujisakiCommand> commands,
                           std::span<double> f0_out) const {
    const std::size_t n = f0_out.size();
    const double mu_b = std::log(baseline_f0);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sample_rate;
        double log_f0 = mu_b;
        for (const auto& c : commands) {
            const double A = c.integrated_amplitude;
            if (c.type == FujisakiCommandType::Phrase) {
                log_f0 += A * phrase_impulse(c.omega, t - c.onset);
            } else {
                const double up = accent_step(c.omega, t - c.onset);
                const double down = accent_step(c.omega, t - c.offset);
                log_f0 += A * (up - down);
            }
        }
        double f0 = std::exp(log_f0);
        if (!voiced_mask.empty() && !voiced_mask[i]) f0 = 0.0;
        f0_out[i] = f0;
    }
}

std::vector<FujisakiCommand> default_fujisaki_commands(
    std::span<const double> stress_times_s,
    std::span<const double> sentence_starts_s,
    std::span<const ContourKind> sentence_contours,
    double total_s,
    ContourKind fallback_contour,
    const FujisakiModel& model,
    double accent_width_s) {
    std::vector<FujisakiCommand> cmds;

    // One phrase command at the start of every sentence so declination
    // resets at "." / "?" / "!". Plus, for each *question* sentence we
    // add a second phrase-rise command ~250 ms before its end --
    // produces the characteristic terminal rise on every question
    // (regardless of utterance position).
    if (sentence_starts_s.empty()) {
        cmds.push_back({FujisakiCommandType::Phrase, 0.0, 0.0,
                        model.default_phrase_Ap,
                        model.default_phrase_omega});
        if (fallback_contour == ContourKind::Question) {
            const double t_q = std::max(0.0, total_s - 0.35);
            cmds.push_back({FujisakiCommandType::Phrase, t_q, t_q,
                            0.6 * model.default_phrase_Ap,
                            model.default_phrase_omega});
        }
    } else {
        // Each phrase command is placed *1/omega seconds BEFORE* its
        // sentence's start, so by the time the actual sentence-initial
        // vowel onset arrives, the Fujisaki impulse has already reached
        // its peak. Without this, F0 would slowly rise over the first
        // ~300 ms of every sentence, making them sound like they "fade
        // in" -- the trippy quality at sentence boundaries.
        const double phrase_lead = 1.0 / std::max(1e-3,
                                                  model.default_phrase_omega);
        for (std::size_t i = 0; i < sentence_starts_s.size(); ++i) {
            const double t_start = sentence_starts_s[i];
            const double t_end = (i + 1 < sentence_starts_s.size())
                ? sentence_starts_s[i + 1] : total_s;
            const ContourKind ck = (i < sentence_contours.size())
                ? sentence_contours[i] : fallback_contour;
            const double t_phrase = t_start - phrase_lead;

            if (ck == ContourKind::Exclamation) {
                cmds.push_back({FujisakiCommandType::Phrase,
                                t_phrase, t_phrase,
                                2.0 * model.default_phrase_Ap,
                                model.default_phrase_omega});
                const double t_drop = std::max(t_start, t_end - 0.20);
                cmds.push_back({FujisakiCommandType::Phrase,
                                t_drop, t_drop,
                                -0.40,
                                model.default_phrase_omega});
            } else {
                cmds.push_back({FujisakiCommandType::Phrase,
                                t_phrase, t_phrase,
                                model.default_phrase_Ap,
                                model.default_phrase_omega});
                if (ck == ContourKind::Question) {
                    const double t_q = std::max(t_start, t_end - 0.35);
                    cmds.push_back({FujisakiCommandType::Phrase, t_q, t_q,
                                    0.8 * model.default_phrase_Ap,
                                    model.default_phrase_omega});
                }
            }
        }
    }

    const double half = 0.5 * accent_width_s;
    if (stress_times_s.empty()) {
        const double c = total_s * 0.5;
        cmds.push_back({FujisakiCommandType::Accent,
                        std::max(0.0, c - half), c + half,
                        model.default_accent_Aa,
                        model.default_accent_omega});
    } else {
        for (double mark : stress_times_s) {
            cmds.push_back({FujisakiCommandType::Accent,
                            std::max(0.0, mark - half), mark + half,
                            model.default_accent_Aa,
                            model.default_accent_omega});
        }
    }
    return cmds;
}

// ===== Klatt rule-based F0 =================================================

void KlattF0Model::render(int sample_rate,
                          std::span<const uint8_t> voiced_mask,
                          std::span<const double> stress_times_s,
                          std::span<const double> sentence_starts_s,
                          std::span<const ContourKind> sentence_contours,
                          ContourKind fallback_contour,
                          std::span<double> f0_out) const {
    const std::size_t n = f0_out.size();
    const double total = std::max(static_cast<double>(n - 1) / sample_rate, 1e-6);
    const double sigma = (accent_fwhm_ms / 1000.0) / 2.355;

    std::vector<double> starts(sentence_starts_s.begin(),
                               sentence_starts_s.end());
    std::vector<ContourKind> contours(sentence_contours.begin(),
                                       sentence_contours.end());
    if (starts.empty() || starts.front() > 0.0) {
        starts.insert(starts.begin(), 0.0);
        contours.insert(contours.begin(), fallback_contour);
    }
    starts.push_back(total + 1.0);
    contours.push_back(fallback_contour);

    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sample_rate;

        auto it = std::upper_bound(starts.begin(), starts.end(), t);
        const std::size_t sentence_idx =
            static_cast<std::size_t>((it - starts.begin()) - 1);
        const double s_start = *(it - 1);
        const double s_end = *it;
        const double s_dur = std::max(1e-6, s_end - s_start);
        const double s_t = t - s_start;
        const ContourKind ck = (sentence_idx < contours.size())
            ? contours[sentence_idx] : fallback_contour;

        double st;
        if (ck == ContourKind::Exclamation) {
            // Excited prosody: start near the accent peak (above the
            // declination topline) and ramp linearly all the way down
            // to the bottom of the speaker's range by sentence end.
            // The total drop spans declination_st + accent_peak_st so
            // it's a markedly larger range than a statement.
            const double frac = s_t / s_dur;
            const double total_drop = declination_st + accent_peak_st;
            st = accent_peak_st - total_drop * frac;
        } else {
            // 1. Declination resets per sentence
            st = -declination_st * (s_t / s_dur);
        }

        // 2. Accent peaks at stressed syllables (skip for exclamation;
        //    the global pitch sweep already supplies the prominence)
        if (ck != ContourKind::Exclamation) {
            for (double mark : stress_times_s) {
                const double dt = (t - mark) / sigma;
                st += accent_peak_st * std::exp(-0.5 * dt * dt);
            }
        }

        // 3. Per-sentence terminal contour for ./? only
        const double tail = terminal_window_ms / 1000.0;
        if (ck != ContourKind::Exclamation && s_t > (s_dur - tail)) {
            const double ramp = std::clamp(
                (s_t - (s_dur - tail)) / tail, 0.0, 1.0);
            if (ck == ContourKind::Question)
                st += terminal_rise_st * ramp;
            else if (ck == ContourKind::Statement)
                st -= terminal_fall_st * ramp;
        }

        double f0 = base_f0 * std::pow(2.0, st / 12.0);
        if (!voiced_mask.empty() && !voiced_mask[i]) f0 = 0.0;
        f0_out[i] = f0;
    }
}

// ---- Mandarin tonal F0 model -------------------------------------------
//
// Each toned syllable gets a (start_level, end_level) pair on the Chao
// 1..5 scale; we linearly interpolate semitones across the syllable's
// sample range, then fill the inter-syllable gaps by holding the last
// value (so an unvoiced consonant after a tone simply keeps the trailing
// pitch -- the listener doesn't perceive a gap as a pitch event).
//
// Tone targets (start, end) -- "half-3" for T3 in connected speech is
// the natural choice; full citation 214 only emerges when T3 is
// utterance-final, and emerging Mandarin TTS surveys (Xu 1997, Lin 2007)
// confirm half-3 is the connected-speech default.
void TonalF0Model::render(int sample_rate,
                          std::span<const uint8_t> voiced_mask,
                          std::span<const std::size_t> phoneme_start_samples,
                          std::span<const int> phoneme_tones,
                          std::span<const uint8_t> phoneme_tone_citation,
                          std::span<double> f0_out) const {
    (void)sample_rate;

    // Chao level -> semitones around midline (level 3).
    // 5 levels span range_st total -> (level - 3) * range_st/4 semitones.
    const double per_level_st = range_st / 4.0;
    auto level_to_hz = [&](double level) {
        const double st = (level - 3.0) * per_level_st;
        return midline_f0 * std::pow(2.0, st / 12.0);
    };

    // A Span gets two interp pieces (start->mid, mid->end). For most
    // tones mid==end and the second piece collapses; T3 citation uses
    // a real mid (the dip) to get the 2->1->4 contour.
    struct Span {
        std::size_t s0, s1;
        double start_hz, mid_hz, end_hz;
        double mid_fraction;        // where in [s0, s1) the mid sits
    };
    std::vector<Span> spans;
    spans.reserve(phoneme_tones.size());

    for (std::size_t i = 0; i < phoneme_tones.size(); ++i) {
        const int t = phoneme_tones[i];
        if (t <= 0) continue;
        const std::size_t s0 = phoneme_start_samples[i];
        const std::size_t s1 = phoneme_start_samples[i + 1];
        double a = 3.0, m = 3.0, b = 3.0;
        double mf = 1.0;            // mid fraction (1.0 = degenerate)
        switch (t) {
            case 1: a = 5.0; m = 5.0; b = 5.0; break;     // T1 55
            case 2: a = 3.0; m = 5.0; b = 5.0; break;     // T2 35
            case 3:
                if (!phoneme_tone_citation.empty()
                    && phoneme_tone_citation[i]) {
                    // Citation T3 (214): low-dip-rise. Dip lands at ~40%
                    // of the syllable, rises to mid-low (3.5) by the end.
                    a = 2.0; m = 1.0; b = 3.5; mf = 0.40;
                } else {
                    // Half-3 (21): low-falling.
                    a = 2.0; m = 1.0; b = 1.0;
                }
                break;
            case 4: a = 5.0; m = 1.0; b = 1.0; break;     // T4 51 falling
            case 5: a = 3.0; m = 2.5; b = 2.5; break;     // T5 neutral
            default: break;
        }
        spans.push_back({s0, s1, level_to_hz(a),
                         level_to_hz(m), level_to_hz(b), mf});
    }

    const std::size_t n = f0_out.size();
    if (spans.empty()) {
        // No tones found -- fill with midline so voiced frames still get F0.
        for (std::size_t i = 0; i < n; ++i) {
            f0_out[i] = (voiced_mask.empty() || voiced_mask[i]) ? midline_f0 : 0.0;
        }
        return;
    }

    // Walk samples; at any given sample, find the containing tone span
    // (or use the previous end / next start to bridge gaps).
    std::size_t cur = 0;     // index into spans
    double last_hz = spans.front().start_hz;
    for (std::size_t i = 0; i < n; ++i) {
        while (cur < spans.size() && i >= spans[cur].s1) ++cur;
        double f0 = last_hz;
        if (cur < spans.size()) {
            const Span& sp = spans[cur];
            if (i < sp.s0) {
                // Between tones: linear interp in log-Hz from last_hz to
                // sp.start_hz over the gap, so the transition stays
                // smooth instead of stepping.
                const std::size_t gap_start = (cur == 0) ? 0 : spans[cur - 1].s1;
                const std::size_t gap_len = sp.s0 - gap_start;
                const double t = (gap_len > 0)
                    ? static_cast<double>(i - gap_start) / gap_len : 1.0;
                const double a_log = std::log(last_hz);
                const double b_log = std::log(sp.start_hz);
                f0 = std::exp(a_log + t * (b_log - a_log));
            } else {
                // Inside the tone. Two-piece log-Hz interp:
                //   [s0, s0+frac*len)  : start -> mid
                //   [s0+frac*len, s1)  : mid   -> end
                // For non-T3-citation tones mid==end so it collapses.
                const double len = static_cast<double>(sp.s1 - sp.s0);
                const std::size_t mid_sample = sp.s0
                    + static_cast<std::size_t>(sp.mid_fraction * len);
                double a_log, b_log;
                double t;
                if (i < mid_sample && mid_sample > sp.s0) {
                    t = static_cast<double>(i - sp.s0) / (mid_sample - sp.s0);
                    a_log = std::log(sp.start_hz);
                    b_log = std::log(sp.mid_hz);
                } else {
                    const std::size_t denom = (sp.s1 > mid_sample)
                        ? (sp.s1 - mid_sample) : 1;
                    t = static_cast<double>(i - mid_sample) / denom;
                    a_log = std::log(sp.mid_hz);
                    b_log = std::log(sp.end_hz);
                }
                f0 = std::exp(a_log + t * (b_log - a_log));
                last_hz = f0;
            }
        }
        if (!voiced_mask.empty() && !voiced_mask[i]) f0 = 0.0;
        f0_out[i] = f0;
    }
}

// ---- Japanese F0 model -------------------------------------------------
//
// Per-sentence pattern: initial rise -> declination -> terminal.
// Operates in semitone space around base_f0 (which sits at the floor of
// the declination line). Sentence segmentation comes from
// sentence_starts_s + sentence_contours so each sentence resets its
// pattern independently.
void JapaneseF0Model::render(int sample_rate,
                             std::span<const uint8_t> voiced_mask,
                             std::span<const double> sentence_starts_s,
                             std::span<const ContourKind> sentence_contours,
                             double total_s,
                             std::span<const std::size_t> mora_nucleus_samples,
                             std::span<const uint8_t> mora_nucleus_pitch,
                             std::span<double> f0_out) const {
    const std::size_t n = f0_out.size();
    std::vector<double> ends(sentence_starts_s.size(), total_s);
    for (std::size_t k = 0; k + 1 < sentence_starts_s.size(); ++k) {
        ends[k] = sentence_starts_s[k + 1];
    }

    auto sentence_for = [&](double t, std::size_t& idx) {
        idx = 0;
        for (std::size_t k = 0; k < sentence_starts_s.size(); ++k) {
            if (t >= sentence_starts_s[k]) idx = k; else break;
        }
    };

    // Build per-sample mora-pitch (L/H) by walking mora nuclei. For
    // each mora compute a target semitone offset =
    //   declination(t)  +  L/H offset  +  downstep within H run.
    // The L→H jump between consecutive mora nuclei is then smoothed by
    // linear interp in semitones across the inter-nucleus gap.
    const double term_s = terminal_window_ms * 1e-3;

    std::vector<std::pair<std::size_t, double>> mora_targets;
    mora_targets.reserve(mora_nucleus_samples.size());
    int h_run_idx = 0;       // position within the current H run
    std::size_t prev_s_idx = static_cast<std::size_t>(-1);
    for (std::size_t k = 0; k < mora_nucleus_samples.size(); ++k) {
        const std::size_t s = mora_nucleus_samples[k];
        const double t = static_cast<double>(s) / sample_rate;
        std::size_t s_idx = 0;
        sentence_for(t, s_idx);
        // Reset the H-run counter at sentence boundaries -- each
        // phrase starts a fresh L→H climb.
        if (s_idx != prev_s_idx) { h_run_idx = 0; prev_s_idx = s_idx; }
        const double s_start = sentence_starts_s[s_idx];
        const double s_end   = ends[s_idx];
        const double s_len   = std::max(1e-3, s_end - s_start);
        const double t_in    = t - s_start;
        const double decl = -declination_st * std::min(1.0, t_in / s_len);
        double offset;
        if (mora_nucleus_pitch[k]) {
            // H mora: apply downstep (each successive H sits lower)
            offset = accent_high_st - downstep_per_h_mora_st * h_run_idx;
            ++h_run_idx;
        } else {
            offset = -accent_low_st;
            h_run_idx = 0;
        }
        mora_targets.emplace_back(s, decl + offset);
    }

    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sample_rate;
        std::size_t s_idx = 0;
        sentence_for(t, s_idx);
        const double s_end = ends[s_idx];
        const ContourKind ck =
            (s_idx < sentence_contours.size()) ? sentence_contours[s_idx]
                                               : ContourKind::Statement;

        double st = 0.0;

        // Stepwise contour with a short transition window. Holds the
        // previous mora's pitch through that mora's vowel, then ramps
        // to the next mora's target over `transition_samples` -- right
        // at the boundary, not gradually across the whole interval.
        // The slow linear interp the old version used was producing a
        // contour that rose continuously from start to end and sounded
        // like a generic falling-pitch utterance instead of Japanese.
        const std::size_t transition_samples =
            static_cast<std::size_t>(0.060 * sample_rate);
        if (mora_targets.empty()) {
            // flat at midline
        } else if (i <= mora_targets.front().first) {
            st = mora_targets.front().second;
        } else if (i >= mora_targets.back().first) {
            st = mora_targets.back().second;
        } else {
            for (std::size_t k = 0; k + 1 < mora_targets.size(); ++k) {
                const std::size_t a = mora_targets[k].first;
                const std::size_t b = mora_targets[k + 1].first;
                if (i >= a && i < b) {
                    const std::size_t ramp_start =
                        (b > transition_samples) ? b - transition_samples : a;
                    if (i < ramp_start) {
                        st = mora_targets[k].second;        // hold
                    } else {
                        const double alpha =
                            static_cast<double>(i - ramp_start) /
                            std::max<std::size_t>(1, b - ramp_start);
                        st = (1.0 - alpha) * mora_targets[k].second
                           +        alpha  * mora_targets[k + 1].second;
                    }
                    break;
                }
            }
        }

        // Sentence-terminal contour.
        const double t_from_end = s_end - t;
        if (t_from_end < term_s) {
            const double ramp = (term_s - t_from_end) / std::max(1e-6, term_s);
            if (ck == ContourKind::Question)
                st += terminal_rise_st * ramp;
            else if (ck == ContourKind::Exclamation)
                st -= exclam_fall_st * ramp;
            else
                st -= terminal_fall_st * ramp;
        }

        double f0 = base_f0 * std::pow(2.0, st / 12.0);
        if (!voiced_mask.empty() && !voiced_mask[i]) f0 = 0.0;
        f0_out[i] = f0;
    }
}

}  // namespace klattalker
