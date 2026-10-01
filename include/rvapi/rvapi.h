/* rvapi.h -- Robotic Voice API public header
 *
 * Flat C interface for the RoboTalker speech synthesizer. Any host
 * with C FFI (Python ctypes, .NET P/Invoke, Node N-API, Rust bindgen,
 * Go cgo, etc.) can drive RoboTalker through this header.
 *
 * Usage outline:
 *
 *     rv_synth_t s = rv_create();
 *     rv_set_voice(s, "kate");
 *     rv_set_language(s, "us");
 *     float*  samples = NULL;
 *     size_t  n_samples = 0;
 *     int     sample_rate = 0;
 *     int rc = rv_synthesize(s, "Hello world.",
 *                            &samples, &n_samples, &sample_rate);
 *     // ... use samples ...
 *     rv_free_samples(samples);
 *     rv_destroy(s);
 *
 * All functions are thread-compatible (you may use different rv_synth_t
 * handles concurrently from different threads) but a single handle must
 * not be touched by two threads at once.
 *
 * Returned strings (from rv_version, rv_last_error) are owned by the
 * library and remain valid until the next call into the library.
 */

#ifndef RVAPI_H
#define RVAPI_H

#include <stddef.h>

#if defined(_WIN32)
  #if defined(RVAPI_BUILDING)
    #define RVAPI __declspec(dllexport)
  #elif defined(RVAPI_DYNAMIC)
    #define RVAPI __declspec(dllimport)
  #else
    #define RVAPI
  #endif
#else
  #if defined(RVAPI_BUILDING) && (defined(__GNUC__) || defined(__clang__))
    #define RVAPI __attribute__((visibility("default")))
  #else
    #define RVAPI
  #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Return codes */
#define RV_OK                  0
#define RV_ERR_BAD_HANDLE     -1
#define RV_ERR_UNKNOWN_VOICE  -2
#define RV_ERR_UNKNOWN_LANG   -3
#define RV_ERR_EMPTY_TEXT     -4
#define RV_ERR_INTERNAL       -5

/* Opaque synthesizer handle. */
typedef struct rv_synth_s* rv_synth_t;

/* Library version (semver-style string, never NULL). */
RVAPI const char* rv_version(void);

/* Last error message for this thread (never NULL; "" if no error). */
RVAPI const char* rv_last_error(void);

/* Construct a synthesizer with the default voice (dan) and language (us).
 * Returns NULL on out-of-memory. */
RVAPI rv_synth_t rv_create(void);

/* Free a synthesizer handle. Safe to call with NULL. */
RVAPI void rv_destroy(rv_synth_t s);

/* Set the speaking voice.
 *   "dan"   default modal male
 *   "lily"  default modal female
 *   "john"  deep modal male
 *   "kate"  breathy female
 *   "josh"  child voice
 *   "frank" elderly male
 *   "doris" elderly female
 * Returns RV_OK or RV_ERR_UNKNOWN_VOICE.
 */
RVAPI int rv_set_voice(rv_synth_t s, const char* voice_name);

/* Set the language variant.
 *   "us"|"en"     US English (CMU dict)
 *   "uk"          RP British
 *   "pl"          Polish
 *   "de"          German
 *   "zh"|"cmn"    Mandarin Chinese
 *   "ja"          Japanese
 *   "it"          Italian
 *   "fr"          French
 * Returns RV_OK or RV_ERR_UNKNOWN_LANG.
 */
RVAPI int rv_set_language(rv_synth_t s, const char* language);

/* Override the base F0 in Hz (use a value <= 0 to restore the voice
 * preset default). Returns RV_OK. */
RVAPI int rv_set_base_f0(rv_synth_t s, double f0_hz);

/* Synthesize `text` (UTF-8). Allocates a float buffer of mono samples
 * and writes the pointer + length + sample rate.
 *
 *   *out_samples     -> heap-allocated float buffer; FREE WITH rv_free_samples
 *   *out_n_samples   -> number of float samples written
 *   *out_sample_rate -> rate the buffer should be played at (Hz)
 *
 * Returns RV_OK on success; otherwise see RV_ERR_* and rv_last_error.
 * On failure, *out_samples is set to NULL.
 */
RVAPI int rv_synthesize(rv_synth_t   s,
                        const char*  text,
                        float**      out_samples,
                        size_t*      out_n_samples,
                        int*         out_sample_rate);

/* Free a buffer returned by rv_synthesize. Safe to call with NULL. */
RVAPI void rv_free_samples(float* samples);

/* Write the synthesized audio directly to a WAV file (16-bit PCM mono).
 * Convenience wrapper around rv_synthesize. Returns RV_OK or RV_ERR_*. */
RVAPI int rv_synthesize_to_wav(rv_synth_t  s,
                               const char* text,
                               const char* wav_path);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* RVAPI_H */
