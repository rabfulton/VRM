#include "viseme.h"
#include <alloca.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// Analysis parameters
#define SAMPLE_RATE 24000
#define FRAME_SIZE_MS 10
#define FRAME_SIZE_SAMPLES (SAMPLE_RATE * FRAME_SIZE_MS / 1000) // 240 samples

// Frequency band thresholds (in terms of zero-crossing rate)
// ZCR of ~500Hz tone at 24kHz = 500/24000 * 2 = 0.042 crossings per sample
// ZCR of ~2000Hz tone = 0.167 crossings per sample
#define ZCR_LOW_THRESHOLD 0.05f  // Below this = low frequency content
#define ZCR_HIGH_THRESHOLD 0.12f // Above this = high frequency content

void viseme_init(VisemeAnalyzer *v) {
  if (!v)
    return;
  memset(v, 0, sizeof(VisemeAnalyzer));

  // Initialize event stream for viseme data
  event_stream_init(&v->stream, EVENT_DOMAIN_AUDIO_SAMPLES,
                    sizeof(VisemeFrame));

  // Default parameters
  v->gain = 1.0f;
  v->threshold = 0.02f; // ~2% of max amplitude
  v->smoothing = 0.3f;  // Moderate smoothing
  v->enabled = false;

  // Zero initial state
  memset(&v->smoothed, 0, sizeof(VisemeFrame));
}

void viseme_free(VisemeAnalyzer *v) {
  if (!v)
    return;
  // EventStream doesn't need explicit cleanup
  memset(v, 0, sizeof(VisemeAnalyzer));
}

void viseme_reset(VisemeAnalyzer *v) {
  if (!v)
    return;
  event_stream_reset(&v->stream);
  memset(&v->smoothed, 0, sizeof(VisemeFrame));
  v->frames_analyzed = 0;
}

// Compute RMS energy of a frame (normalized to 0-1)
static float compute_rms(const int16_t *samples, size_t count, float gain) {
  if (count == 0)
    return 0.0f;

  double sum_sq = 0.0;
  for (size_t i = 0; i < count; i++) {
    float s = (float)samples[i] / 32768.0f * gain;
    sum_sq += s * s;
  }

  float rms = sqrtf((float)(sum_sq / count));
  return rms;
}

// Compute zero-crossing rate (crossings per sample)
static float compute_zcr(const int16_t *samples, size_t count) {
  if (count < 2)
    return 0.0f;

  size_t crossings = 0;
  for (size_t i = 1; i < count; i++) {
    if ((samples[i - 1] >= 0 && samples[i] < 0) ||
        (samples[i - 1] < 0 && samples[i] >= 0)) {
      crossings++;
    }
  }

  return (float)crossings / (float)(count - 1);
}

// Simple spectral shape estimation using envelope variance
// Higher variance = more dynamic/consonant-like
// Lower variance = more steady/vowel-like
static float compute_envelope_variance(const int16_t *samples, size_t count) {
  if (count < 16)
    return 0.0f;

  // Compute envelope in blocks of 16 samples
  size_t block_count = count / 16;
  if (block_count < 2)
    return 0.0f;

  float *env = (float *)alloca(block_count * sizeof(float));
  float mean = 0.0f;

  for (size_t b = 0; b < block_count; b++) {
    float block_max = 0.0f;
    for (size_t i = 0; i < 16; i++) {
      float s = fabsf((float)samples[b * 16 + i]);
      if (s > block_max)
        block_max = s;
    }
    env[b] = block_max / 32768.0f;
    mean += env[b];
  }
  mean /= block_count;

  float variance = 0.0f;
  for (size_t b = 0; b < block_count; b++) {
    float diff = env[b] - mean;
    variance += diff * diff;
  }
  variance /= block_count;

  return variance;
}

// Map audio features to vowel weights
// This is a heuristic mapping:
// - RMS controls overall mouth openness
// - ZCR indicates high vs low frequency content
// - Envelope variance helps distinguish vowels from consonants
static void map_to_vowels(float rms, float zcr, float env_var,
                          VisemeFrame *out) {
  memset(out, 0, sizeof(VisemeFrame));

  // Silence check already done by caller
  // Clamp RMS to reasonable range for mouth openness
  float openness = rms * 5.0f; // Scale up - RMS is usually small
  if (openness > 1.0f)
    openness = 1.0f;

  // Use ZCR to bias between vowel types:
  // Low ZCR (< 0.05): "A", "O" - open, back vowels
  // Mid ZCR (0.05-0.12): "U" - rounded
  // High ZCR (> 0.12): "I", "E" - front, closed vowels

  if (zcr < ZCR_LOW_THRESHOLD) {
    // Low frequency - open vowels A/O
    // Use envelope variance to distinguish
    if (env_var > 0.01f) {
      // More dynamic - "A" (often at start of sounds)
      out->weights[VISEME_A] = openness * 0.8f;
      out->weights[VISEME_O] = openness * 0.2f;
    } else {
      // Steadier - "O"
      out->weights[VISEME_A] = openness * 0.3f;
      out->weights[VISEME_O] = openness * 0.7f;
    }
  } else if (zcr < ZCR_HIGH_THRESHOLD) {
    // Mid frequency - "U" and some "A"
    out->weights[VISEME_U] = openness * 0.6f;
    out->weights[VISEME_A] = openness * 0.3f;
    out->weights[VISEME_O] = openness * 0.1f;
  } else {
    // High frequency - front vowels "I"/"E"
    // Higher ZCR = more "I" (brightest)
    float i_bias = (zcr - ZCR_HIGH_THRESHOLD) / 0.1f;
    if (i_bias > 1.0f)
      i_bias = 1.0f;

    out->weights[VISEME_I] = openness * (0.3f + 0.5f * i_bias);
    out->weights[VISEME_E] = openness * (0.5f - 0.3f * i_bias);
    out->weights[VISEME_A] = openness * 0.2f;
  }
}

void viseme_analyze(VisemeAnalyzer *v, const int16_t *samples, size_t count,
                    int64_t chunk_start_sample) {
  if (!v || !samples || count == 0) {
    return;
  }
  if (!v->enabled) {
    printf("Viseme: analyze skipped - not enabled\n");
    return;
  }

  printf("Viseme: analyzing %zu samples starting at %lld (enabled=%d)\n", count,
         (long long)chunk_start_sample, v->enabled);

  // Process in frames
  size_t offset = 0;
  size_t frames_processed = 0;
  float max_rms = 0.0f;

  while (offset + FRAME_SIZE_SAMPLES <= count) {
    const int16_t *frame = samples + offset;
    int64_t frame_time = chunk_start_sample + (int64_t)offset;

    // Compute features
    float rms = compute_rms(frame, FRAME_SIZE_SAMPLES, v->gain);
    float zcr = compute_zcr(frame, FRAME_SIZE_SAMPLES);
    float env_var = compute_envelope_variance(frame, FRAME_SIZE_SAMPLES);

    if (rms > max_rms)
      max_rms = rms;

    // Map to vowels
    VisemeFrame raw;
    if (rms < v->threshold) {
      // Silence - close mouth
      memset(&raw, 0, sizeof(VisemeFrame));
    } else {
      map_to_vowels(rms, zcr, env_var, &raw);
    }

    // Apply smoothing
    float alpha = v->smoothing;
    for (int i = 0; i < VISEME_COUNT; i++) {
      v->smoothed.weights[i] =
          alpha * raw.weights[i] + (1.0f - alpha) * v->smoothed.weights[i];
    }

    // Push to stream
    event_stream_push(&v->stream, frame_time, &v->smoothed);
    v->frames_analyzed++;
    frames_processed++;

    offset += FRAME_SIZE_SAMPLES;
  }

  printf("Viseme: processed %zu frames, max_rms=%.4f, threshold=%.4f, "
         "stream_depth=%zu\n",
         frames_processed, max_rms, v->threshold,
         event_stream_depth(&v->stream));
}

bool viseme_sample(VisemeAnalyzer *v, int64_t playback_sample,
                   VisemeFrame *out) {
  if (!v || !out)
    return false;

  if (!v->enabled) {
    memset(out, 0, sizeof(VisemeFrame));
    return false;
  }

  // Sample from event stream
  bool ok = event_stream_sample(&v->stream, playback_sample, out, NULL);

  // Prune old data (keep ~1 second of history)
  event_stream_prune(&v->stream, playback_sample - SAMPLE_RATE);

  return ok;
}

void viseme_set_enabled(VisemeAnalyzer *v, bool enabled) {
  if (!v)
    return;
  v->enabled = enabled;
  if (!enabled) {
    memset(&v->smoothed, 0, sizeof(VisemeFrame));
  }
}

void viseme_set_gain(VisemeAnalyzer *v, float gain) {
  if (!v)
    return;
  v->gain = gain > 0.0f ? gain : 1.0f;
}

void viseme_set_threshold(VisemeAnalyzer *v, float threshold) {
  if (!v)
    return;
  v->threshold = threshold > 0.0f ? threshold : 0.02f;
}

void viseme_set_smoothing(VisemeAnalyzer *v, float smoothing) {
  if (!v)
    return;
  // Clamp to valid range
  if (smoothing < 0.0f)
    smoothing = 0.0f;
  if (smoothing > 1.0f)
    smoothing = 1.0f;
  v->smoothing = smoothing;
}
