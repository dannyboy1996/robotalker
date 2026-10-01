#include "klattalker/sequencer.hpp"
#include "klattalker/intensity.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <unordered_set>

namespace klattalker {

namespace {
// Klatt convention: amplitudes are dB-relative to a fixed reference.
// The DECtalk-style fixed-gain output stage hard-clips at ±1.0 with a
// 0.12 master gain, so the parallel branch (driven by gaussian noise
// with std ~1.4 after pre-emphasis) needs to sit well below the
// cascade vowel level to avoid sustained clipping during /s/, /sh/,
// etc. A 78 dB reference puts a steady /s/ at roughly the same RMS
// as a vowel after the master gain.
double db_to_lin(double db) {
    if (db <= -99.0) return 0.0;
    return std::pow(10.0, (db - 78.0) / 20.0);
}
}  // namespace

ControlTracks render_tracks(const std::vector<PhonemeItem>& items,
                            Sex sex,
                            Variant variant,
                            int sample_rate,
                            double frame_ms) {
    constexpr int N_FORMANTS = 5;
    constexpr int N_PARALLEL = 6;

    // resolve each item to a Target + frame count
    std::vector<Target> targets;
    std::vector<int> frames_per;
    targets.reserve(items.size());
    frames_per.reserve(items.size());
    for (const auto& it : items) {
        auto tgt = get_target(it.key, sex, variant);
        if (!tgt) {
            throw std::invalid_argument("unknown phoneme: " + it.key);
        }
        const double dur = (it.duration_ms > 0.0)
                           ? it.duration_ms : tgt->duration_ms;
        int nf = static_cast<int>(std::round(dur / frame_ms));
        if (nf < 1) nf = 1;
        targets.push_back(*tgt);
        frames_per.push_back(nf);
    }

    const int total_frames = std::accumulate(
        frames_per.begin(), frames_per.end(), 0);
    const int samples_per_frame = static_cast<int>(
        std::round(sample_rate * frame_ms / 1000.0));

    // collect stress times — midpoint of stressed events. Also collect
    // sentence-start times: utterance begins at t=0, plus a new entry
    // immediately after each sentence-break sil. The sentence_contours
    // vector parallels sentence_starts_s and carries the question /
    // statement kind that should apply to each sentence.
    std::vector<double> stress_times_s;
    std::vector<double> sentence_starts_s{0.0};
    std::vector<ContourKind> sentence_contours{ContourKind::Statement};
    int cum = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].stressed) {
            int mid = cum + frames_per[i] / 2;
            stress_times_s.push_back(mid * frame_ms / 1000.0);
        }
        cum += frames_per[i];
        if (items[i].is_sentence_break) {
            // assign this sentence break's kind to the CURRENT sentence
            // (the one we just finished). Then start the next.
            sentence_contours.back() = items[i].sentence_end_kind;
            if (i + 1 < items.size()) {
                sentence_starts_s.push_back(cum * frame_ms / 1000.0);
                sentence_contours.push_back(ContourKind::Statement);
            }
        }
    }

    // frame-rate scratch buffers
    std::vector<std::vector<double>> ff(N_FORMANTS,
        std::vector<double>(total_frames, 0.0));
    std::vector<std::vector<double>> fb(N_FORMANTS,
        std::vector<double>(total_frames, 0.0));
    std::vector<std::vector<double>> pa(N_PARALLEL,
        std::vector<double>(total_frames, 0.0));
    std::vector<double> ba(total_frames, 0.0);
    std::vector<double> vc(total_frames, 0.0);
    std::vector<double> fr(total_frames, 0.0);
    std::vector<double> as(total_frames, 0.0);
    std::vector<double> ns(total_frames, 0.0);
    // Default nasal pole/zero values for non-nasal phonemes. We use
    // /m/'s antiformant (750 Hz, 150 Hz BW) rather than 280 Hz, because
    // the AntiResonator's normalisation 1/(1-B-C) is wildly large
    // (~143) at low frequencies but moderate (~22) at 750 Hz. With the
    // low default, every nasal->vowel transition produced ~0.6
    // sample-jump clicks from the Anorm coefficient changes. With the
    // high default, the resonator coefficients stay stable across
    // nasal phonemes and the nasal_amount crossfade does the gating
    // cleanly without touching the filter math.
    std::vector<double> npf(total_frames, 270.0);
    std::vector<double> npb(total_frames, 100.0);
    std::vector<double> nzf(total_frames, 750.0);
    std::vector<double> nzb(total_frames, 150.0);
    std::vector<uint8_t> cl(total_frames, 0);

    // Klatt segmental-intensity factors per phoneme.
    auto intensity_v = [&](std::size_t k) {
        return inherent_voicing_amp(items[k].key);
    };
    auto intensity_f = [&](std::size_t k) {
        return inherent_frication_amp(items[k].key);
    };

    // ---- coarticulation: compute boundary-formant values --------------
    //
    // At each consecutive-phoneme boundary we compute the formant value
    // the trajectory should pass through. The locus theory (Delattre,
    // Liberman & Cooper 1955) says each consonant has a "locus" that
    // adjacent vowel formants transition toward. A consonant's
    // locus_lock measures how strongly it pulls.
    //
    // If both sides are vowels (lock=0), boundary is the midpoint of
    // the two targets — smooth glide. Otherwise the consonant's locus
    // dominates by its lock weight.
    auto boundary_value = [&](std::size_t left, std::size_t right, int j)
                              -> double {
        const Target& L = targets[left];
        const Target& R = targets[right];
        const double lock_L = L.locus_lock;
        const double lock_R = R.locus_lock;
        if (lock_L < 1e-6 && lock_R < 1e-6) {
            return 0.5 * (L.formants[j] + R.formants[j]);
        }
        const double wL = lock_L > 1e-6 ? lock_L : 0.0;
        const double wR = lock_R > 1e-6 ? lock_R : 0.0;
        return (L.locus[j] * wL + R.locus[j] * wR) / (wL + wR);
    };

    // Transition zone half-width (one side); 40 ms for C-V and V-C
    // boundaries (Klatt 1980 §III) but only 20 ms for C-C boundaries.
    // Real consonant clusters /st sk pl tr/ etc. have heavily
    // overlapping gestures (Browman & Goldstein 1986 "Towards an
    // articulatory phonology") rather than full sequential transitions
    // -- the long 40 ms transition for clusters was producing audible
    // glide-y schwa-like blur between the consonants.
    const double trans_ms_cv = 40.0;
    const double trans_ms_cc = 20.0;
    const int trans_frames_cv = static_cast<int>(std::round(trans_ms_cv / frame_ms));
    const int trans_frames_cc = static_cast<int>(std::round(trans_ms_cc / frame_ms));

    // Vowel set for the C/V check below.
    static const std::unordered_set<std::string> vowels_for_trans = {
        "iy","ih","ey","eh","ae","aa","ao","ow","uh","uw","ah","er","oq",
        "yi","yu","oe","ou","ee_zh","i_apic",
        "iglide","uglide",
    };
    auto is_vowel_seg = [&](std::size_t k) {
        return k < items.size() && vowels_for_trans.count(items[k].key) > 0;
    };

    int frame_idx = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Target& cur = targets[i];
        const Target& prv = (i > 0) ? targets[i - 1] : cur;
        const double cur_iv = intensity_v(i);
        const double nxt_iv = (i + 1 < items.size()) ? intensity_v(i + 1) : cur_iv;
        const double prv_iv = (i > 0) ? intensity_v(i - 1) : cur_iv;
        const double cur_if = intensity_f(i);
        const double nxt_if = (i + 1 < items.size()) ? intensity_f(i + 1) : cur_if;
        const double prv_if = (i > 0) ? intensity_f(i - 1) : cur_if;
        const int nf = frames_per[i];

        // boundary anchors for this phoneme's formants
        std::array<double, 5> left_anchor{}, right_anchor{};
        for (int j = 0; j < N_FORMANTS; ++j) {
            left_anchor[j]  = (i > 0)
                ? boundary_value(i - 1, i, j) : cur.formants[j];
            right_anchor[j] = (i + 1 < items.size())
                ? boundary_value(i, i + 1, j) : cur.formants[j];
        }

        // Pick transition-zone widths that fit inside this phoneme.
        // C-V or V-C boundaries get the full 40 ms; C-C boundaries get
        // a shorter 20 ms reflecting gestural overlap in clusters.
        const bool cur_is_vowel = is_vowel_seg(i);
        const bool prv_is_vowel = (i > 0) && is_vowel_seg(i - 1);
        const bool nxt_is_vowel = (i + 1 < items.size())
                                  && is_vowel_seg(i + 1);
        const int t_on_max  = (cur_is_vowel || prv_is_vowel)
            ? trans_frames_cv : trans_frames_cc;
        const int t_off_max = (cur_is_vowel || nxt_is_vowel)
            ? trans_frames_cv : trans_frames_cc;
        const int t_on  = std::min(t_on_max, nf / 2);
        const int t_off = std::min(t_off_max, nf - t_on);

        const Target& nxt = (i + 1 < items.size()) ? targets[i + 1] : cur;

        for (int k = 0; k < nf; ++k) {
            const int idx = frame_idx + k;

            // -------- formants: steady-state + transition zones --------
            for (int j = 0; j < N_FORMANTS; ++j) {
                double fval, bval;
                if (k < t_on) {
                    // onset: left_anchor -> target
                    const double a = static_cast<double>(k) / std::max(1, t_on);
                    fval = (1 - a) * left_anchor[j] + a * cur.formants[j];
                    bval = cur.bandwidths[j];
                } else if (k >= nf - t_off) {
                    // offset: target -> right_anchor
                    const double a = static_cast<double>(k - (nf - t_off))
                                     / std::max(1, t_off);
                    fval = (1 - a) * cur.formants[j] + a * right_anchor[j];
                    bval = cur.bandwidths[j];
                } else {
                    // steady-state
                    fval = cur.formants[j];
                    bval = cur.bandwidths[j];
                }
                ff[j][idx] = fval;
                fb[j][idx] = bval;
            }

            // -------- trill modulation ---------------------------------
            // Square-wave AM of the voicing track + brief F1 dip during
            // the closure portion of each tap. Phase counts in seconds
            // from the start of this phoneme so the modulation lines up
            // with the tongue-tip rate, independent of frame size.
            double trill_voicing_mod = 1.0;
            if (cur.trill_rate_hz > 0.0) {
                const double t_in_phoneme = k * frame_ms / 1000.0;
                double phase = t_in_phoneme * cur.trill_rate_hz;
                phase -= std::floor(phase);     // wrap to [0, 1)
                const bool closed = phase < (1.0 - cur.trill_open_fraction);
                if (closed) {
                    trill_voicing_mod = 1.0 - cur.trill_depth;
                    // F1 drops to ~250 Hz during closure (tongue-tip stop)
                    ff[0][idx] = 250.0;
                    // widen B1 to model the closure-phase damping
                    fb[0][idx] = std::max(fb[0][idx], 150.0);
                }
            }

            // -------- short bidirectional cross-fade -------------------
            //
            // Continuity at phoneme boundaries requires the alphas on
            // both sides to sum to 1.0. That means max alpha at the
            // boundary must be 0.5 -- with 0.25 you get a discontinuous
            // step of 0.5 in one sample (the audible click at nasal /
            // vowel boundaries). We use the full 0.5 max but apply it
            // only over the first/last EIGHTH of each phoneme so the
            // middle 75% stays at pure cur (no "drunk" perma-blending).
            double alpha = 0.0;
            const Target* blend_target = &cur;
            double blend_iv = cur_iv, blend_if = cur_if;
            const int blend = std::max(1, nf / 8);
            if (k < blend) {
                const double t = static_cast<double>(k) / blend;
                alpha = 0.5 * (1.0 - t);    // 0.5 at start, 0 by 1/8 in
                blend_target = &prv;
                blend_iv = prv_iv;
                blend_if = prv_if;
            } else if (k >= nf - blend) {
                const double t = static_cast<double>(k - (nf - blend))
                                 / std::max(1, blend);
                alpha = 0.5 * t;            // 0 at 7/8, 0.5 at end
                blend_target = &nxt;
                blend_iv = nxt_iv;
                blend_if = nxt_if;
            }
            for (int j = 0; j < N_PARALLEL; ++j) {
                const double cur_lin = db_to_lin(cur.parallel_db[j]);
                const double bln_lin = db_to_lin(blend_target->parallel_db[j]);
                pa[j][idx] = (1 - alpha) * cur_lin + alpha * bln_lin;
            }
            ba[idx] = (1 - alpha) * db_to_lin(cur.bypass_db)
                    + alpha       * db_to_lin(blend_target->bypass_db);
            vc[idx] = ((1 - alpha) * (cur.voicing * cur_iv)
                    + alpha       * (blend_target->voicing * blend_iv))
                    * trill_voicing_mod;
            fr[idx] = (1 - alpha) * (cur.frication * cur_if)
                    + alpha       * (blend_target->frication * blend_if);
            as[idx] = (1 - alpha) * cur.aspiration + alpha * blend_target->aspiration;
            ns[idx] = (1 - alpha) * cur.nasal      + alpha * blend_target->nasal;
            // Nasal pole/zero are interpolated WITHIN a phoneme too --
            // for non-nasals the targets keep the defaults (270/750) so
            // the interpolation produces no change. For nasals, the
            // crossfade is between consecutive nasals' values, which sit
            // close enough together that the Anorm-related click sources
            // don't materialise.
            npf[idx] = (1 - alpha) * cur.nasal_pole_freq + alpha * blend_target->nasal_pole_freq;
            npb[idx] = (1 - alpha) * cur.nasal_pole_bw   + alpha * blend_target->nasal_pole_bw;
            nzf[idx] = (1 - alpha) * cur.nasal_zero_freq + alpha * blend_target->nasal_zero_freq;
            nzb[idx] = (1 - alpha) * cur.nasal_zero_bw   + alpha * blend_target->nasal_zero_bw;
            cl[idx] = cur.closure;
        }
        frame_idx += nf;
    }

    // /h/ vowel-shape adaptation. Klatt 1980 §3.7: "the [h] segment is
    // produced with the vocal tract in the configuration of the
    // following vowel, with voicing replaced by aspiration noise." So
    // the cascade formants during /h/ should be the FOLLOWING vowel's
    // formants, not /h/'s own static {500, 1500, 2500}. Without this,
    // /hi/ /ha/ /hu/ all sound the same; with it, /h/ becomes a proper
    // voiceless precursor of its vowel.
    {
        auto is_vowel = [](const std::string& k) {
            static const std::unordered_set<std::string> v = {
                "iy","ih","ey","eh","ae","aa","ao","ow","uh","uw","ah",
                "er","oq","yi","yu","oe","ou","ee_zh","i_apic",
            };
            return v.count(k) > 0;
        };
        int cum = 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const int nf = frames_per[i];
            if (items[i].key == "h") {
                // find the next vowel within the same utterance
                std::size_t v_idx = items.size();
                for (std::size_t k = i + 1; k < items.size(); ++k) {
                    if (items[k].key == "sil") break;
                    if (is_vowel(items[k].key)) { v_idx = k; break; }
                }
                if (v_idx < items.size()) {
                    const auto& V = targets[v_idx];
                    for (int f = 0; f < nf; ++f) {
                        for (int j = 0; j < N_FORMANTS; ++j) {
                            ff[j][cum + f] = V.formants[j];
                            fb[j][cum + f] = V.bandwidths[j];
                        }
                    }
                }
            }
            cum += nf;
        }
    }

    // American English /æ/-raising before nasals. "can" /kæn/ is
    // actually realised as [kẽən] -- F1 drops (more close) and F2
    // rises (more front) during the vowel. Labov 1991 / Boberg 2008
    // documented this as one of the most reliable AmE features. We
    // apply a simple uniform shift to /ae/ formants when followed by
    // /m n ŋ/.
    if (variant == Variant::USEnglish) {
        int cum = 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const int nf = frames_per[i];
            if (items[i].key == "ae" && i + 1 < items.size()) {
                const std::string& nx = items[i + 1].key;
                if (nx == "m" || nx == "n" || nx == "ng") {
                    for (int f = 0; f < nf; ++f) {
                        ff[0][cum + f] -= 70.0;   // F1 lowered
                        ff[1][cum + f] += 200.0;  // F2 raised
                    }
                }
            }
            cum += nf;
        }
    }

    // Stop / affricate closures: silence the body, paint a burst into
    // the last burst_ms frames. The burst length comes from the
    // *current* phoneme's target (~20 ms for plain stops, ~60 ms for
    // affricates), so /tʃ/ stays acoustically distinct from /ʃ/.
    {
        // map frame -> source-phoneme index to look up burst_ms
        std::vector<std::size_t> frame_to_event(total_frames, 0);
        std::size_t cum_idx = 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const int nf = frames_per[i];
            for (int k = 0; k < nf; ++k) frame_to_event[cum_idx + k] = i;
            cum_idx += nf;
        }
        int i = 0;
        while (i < total_frames) {
            if (cl[i]) {
                int j = i;
                while (j < total_frames && cl[j]) ++j;
                const std::size_t ev = frame_to_event[i];
                const double burst_ms = targets[ev].burst_ms;
                // Sentence-final stops are often unreleased in English
                // ("the cat" final /t/ commonly [t̚]). When this stop
                // is followed immediately by a sentence-break sil we
                // skip the burst -- the closure just trails off into
                // silence. Within an utterance we always release.
                // Crystal & House 1988 measured ~60% unreleased rate
                // in conversational utterance-final position.
                // Always release plain stops at end of word/sentence.
                // The previous "60% unreleased" behaviour (Crystal &
                // House 1988) was modelled on conversational English;
                // for TTS reading aloud it sounded like the final /k/
                // in "Kraftwerk" or /t/ in "stop" had been swallowed.
                // Read-aloud speech releases nearly every final stop.
                const int burst_frames = std::max(
                    1, static_cast<int>(std::round(burst_ms / frame_ms)));
                // Voice bar: voiced stops /b d g/ retain a low-amplitude
                // voicing buzz during the closure body (Klatt 1980 §3.2;
                // Stevens 1998 §8). This is what acoustically separates
                // /b/ from /p/ even when their VOTs are similar. The
                // larynx vibrates against the closed oral cavity,
                // producing energy mainly below F1. We model it by
                // leaving a quiet (~0.25) voicing track during closure;
                // the cascade still has the stop's low F1 (~300 Hz),
                // so the output naturally peaks low. Voiceless stops
                // remain fully silent during closure.
                const bool voiced = targets[ev].voicing > 0.1;
                const double bar_amp = voiced ? 0.25 : 0.0;
                for (int k = i; k < j; ++k) {
                    vc[k] = bar_amp;
                    fr[k] = 0.0;
                    as[k] = 0.0;
                }
                // Burst: noise release. Burst itself is unvoiced even
                // for voiced stops (the closure has the voicing, the
                // burst is the transient). For affricates (/tʃ/, /dʒ/)
                // the "burst" IS the fricative portion -- 60 ms long.
                // Real /tʃ/ word-finally trails off rather than cutting
                // sharply; we apply a cosine ramp from 1.0 → 0 over the
                // last third of the affricate burst when at end of word
                // or sentence. Klatt 1976 "Linguistic uses of segmental
                // duration in English" §IV documents the prepausal
                // affricate offset.
                const bool is_affricate =
                    items[ev].key == "ch" || items[ev].key == "jh"
                    || items[ev].key == "cj" || items[ev].key == "dj";
                const bool word_or_phrase_end =
                    (ev + 1 == items.size())
                    || items[ev + 1].key == "sil"
                    || items[ev].word_final;
                const int b0 = std::max(i, j - burst_frames);
                if (is_affricate && word_or_phrase_end) {
                    const int taper_start = b0 + 2 * (j - b0) / 3;
                    for (int k = b0; k < j; ++k) {
                        if (k < taper_start) {
                            fr[k] = 1.0;
                        } else {
                            const double t = static_cast<double>(k - taper_start)
                                / std::max(1, j - taper_start);
                            // cos(0)=1, cos(pi/2)=0 -- smooth taper
                            fr[k] = std::cos(t * 1.5707963);
                        }
                        vc[k] = 0.0;
                    }
                } else {
                    for (int k = b0; k < j; ++k) {
                        fr[k] = 1.0;
                        vc[k] = 0.0;
                    }
                }
                i = j;
            } else {
                ++i;
            }
        }
    }

    // Voice-onset-time aspiration. For each voiceless-stop closure,
    // steal the first vot_ms of the FOLLOWING phoneme and replace its
    // voicing with aspiration (the breathy [h]-like delay that makes
    // /p t k/ audibly different from /b d g/). Formants in those frames
    // already glide toward the next vowel via the transition logic, so
    // the aspiration gets the right vocal-tract shape automatically.
    // Aspiration ramps down to 0 over the VOT so voicing can fade in
    // without a sharp transition.
    {
        int cum = 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const int nf = frames_per[i];
            double vot = targets[i].vot_ms;
            // English /s/-stop clusters: /p t k/ after /s/ within a
            // word are UNASPIRATED (Lisker & Abramson 1964; classic
            // "spy" vs "pie" minimal pair test). Drop VOT to ~10 ms
            // when the previous item is /s/ and didn't end the word.
            const bool is_vless_stop = (items[i].key == "p"
                                     || items[i].key == "t"
                                     || items[i].key == "k");
            if (is_vless_stop && i > 0
                && items[i - 1].key == "s"
                && !items[i - 1].word_final) {
                vot = 10.0;
            }
            if (targets[i].closure && vot > 0.0 && i + 1 < items.size()) {
                const int vot_frames = std::max(
                    1, static_cast<int>(std::round(vot / frame_ms)));
                const int next_start = cum + nf;
                const int next_end = std::min(
                    next_start + vot_frames,
                    next_start + frames_per[i + 1]);
                const int actual = next_end - next_start;
                for (int k = next_start; k < next_end; ++k) {
                    const double t = (actual > 1)
                        ? static_cast<double>(k - next_start) / (actual - 1)
                        : 1.0;
                    // Aspiration decays exponentially with τ ≈ 25-30 ms
                    // after stop release (Stevens 1998 §8.2; Klatt 1980
                    // §3.7). Linear 1→0 ramp the previous version used
                    // sounded too uniform / mechanical. Voicing builds
                    // up symmetrically so the crossfade is smooth.
                    // Normalised time t goes 0..1 across the VOT; we
                    // apply 3 time constants of decay over the window.
                    as[k] = std::exp(-3.0 * t);             // ~0.05 at end
                    vc[k] *= 1.0 - std::exp(-3.0 * t);      // ~0.95 at end
                }
            }
            cum += nf;
        }
    }

    // Vowel nasalisation before a nasal coda. "can" /kæn/ is really
    // [kæ̃n], "sing" is [sɪ̃ŋ]. The vowel preceding /m n ŋ/ gets a
    // gentle nasal ramp during its last ~40 ms. Without this the
    // vowel-nasal boundary sounds abrupt and not English-like.
    // (Cohn 1993; Beddor 2009 measured ~30-60 ms of anticipatory
    // nasalisation in American English.)
    {
        const int ramp_frames = std::max(1,
            static_cast<int>(std::round(40.0 / frame_ms)));
        for (std::size_t i = 0; i + 1 < items.size(); ++i) {
            const std::string& nx = items[i + 1].key;
            if (nx != "m" && nx != "n" && nx != "ng") continue;
            // Find the frame range of phoneme i (the vowel before nasal).
            std::size_t cum = 0;
            for (std::size_t k = 0; k < i; ++k) cum += frames_per[k];
            const std::size_t v_start = cum;
            const std::size_t v_end   = v_start + frames_per[i];
            // Skip if this isn't a vowel-like segment (the next-is-nasal
            // check is fast; the explicit vowel check would be cheap too
            // but the eighth-blend already gates this on adjacency).
            const std::size_t ramp_start = (v_end > static_cast<std::size_t>(ramp_frames))
                ? v_end - ramp_frames : v_start;
            for (std::size_t f = ramp_start; f < v_end; ++f) {
                const double t = static_cast<double>(f - ramp_start)
                                 / std::max(1, ramp_frames);
                // Bring ns[f] up toward ~0.35 over the ramp -- a level
                // perceptible but not full nasal coupling.
                ns[f] = std::max(ns[f], 0.35 * t);
            }
        }
    }

    // upsample frame -> sample by repetition
    const int n_samples = total_frames * samples_per_frame;
    auto upsample_v = [&](const std::vector<double>& v) {
        std::vector<double> out(n_samples, 0.0);
        for (int k = 0; k < total_frames; ++k) {
            for (int s = 0; s < samples_per_frame; ++s)
                out[k * samples_per_frame + s] = v[k];
        }
        return out;
    };

    ControlTracks T;
    T.formant_freqs.resize(N_FORMANTS);
    T.formant_bws.resize(N_FORMANTS);
    T.parallel_amps.resize(N_PARALLEL);
    for (int j = 0; j < N_FORMANTS; ++j) {
        T.formant_freqs[j] = upsample_v(ff[j]);
        T.formant_bws[j]   = upsample_v(fb[j]);
    }
    for (int j = 0; j < N_PARALLEL; ++j) {
        T.parallel_amps[j] = upsample_v(pa[j]);
    }
    T.bypass_amp = upsample_v(ba);
    T.voicing    = upsample_v(vc);
    T.frication  = upsample_v(fr);
    T.aspiration = upsample_v(as);
    T.nasal           = upsample_v(ns);
    T.nasal_pole_freq = upsample_v(npf);
    T.nasal_pole_bw   = upsample_v(npb);
    T.nasal_zero_freq = upsample_v(nzf);
    T.nasal_zero_bw   = upsample_v(nzb);
    T.voiced_mask.assign(n_samples, false);
    for (int i_ = 0; i_ < n_samples; ++i_) T.voiced_mask[i_] = T.voicing[i_] > 0.05;
    T.n_samples = static_cast<std::size_t>(n_samples);
    T.total_s = static_cast<double>(n_samples) / sample_rate;
    T.stress_times_s = std::move(stress_times_s);
    T.sentence_starts_s = std::move(sentence_starts_s);
    T.sentence_contours = std::move(sentence_contours);

    // Per-item start sample index for the tonal F0 model. Built from the
    // frames-per-item table; samples-per-frame is constant so the math
    // is simple. Last entry == n_samples for half-open ranges.
    T.phoneme_start_samples.resize(items.size() + 1);
    T.phoneme_tones.resize(items.size());
    T.phoneme_tone_citation.resize(items.size());
    {
        std::size_t cum = 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            T.phoneme_start_samples[i] = cum;
            T.phoneme_tones[i] = items[i].tone;
            T.phoneme_tone_citation[i] = items[i].tone_citation ? 1 : 0;
            if (items[i].mora_nucleus) {
                T.mora_nucleus_samples.push_back(cum);
                T.mora_nucleus_pitch.push_back(
                    static_cast<uint8_t>(items[i].mora_pitch));
            }
            cum += static_cast<std::size_t>(frames_per[i]) * samples_per_frame;
        }
        T.phoneme_start_samples.back() = static_cast<std::size_t>(n_samples);
    }
    return T;
}

}  // namespace klattalker
