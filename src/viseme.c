#include "viseme.h"
#include <alloca.h>
#include <math.h>
#include <string.h>

// Frequency band thresholds (in terms of zero-crossing rate)
// ZCR of ~500Hz tone at 24kHz = 500/24000 * 2 = 0.042 crossings per sample
// ZCR of ~2000Hz tone = 0.167 crossings per sample
#define ZCR_LOW_THRESHOLD 0.05f  // Below this = low frequency content
#define ZCR_HIGH_THRESHOLD 0.12f // Above this = high frequency content

static void viseme_ring_reset(VisemeAnalyzer *v) {
  for (size_t i = 0; i < VISEME_RING_CAPACITY; i++) {
    v->ring_frame_index[i] = INT64_MIN;
    memset(&v->ring[i], 0, sizeof(VisemeFrame));
  }
}

void viseme_init(VisemeAnalyzer *v) {
  if (!v)
    return;
  memset(v, 0, sizeof(VisemeAnalyzer));

  // Default parameters
  v->gain = 1.0f;
  v->threshold = 0.02f; // ~2% of max amplitude
  v->smoothing = 0.3f;  // Moderate smoothing
  v->enabled = true;

  // Zero initial state
  memset(&v->smoothed, 0, sizeof(VisemeFrame));
  memset(&v->last_value, 0, sizeof(VisemeFrame));
  v->has_last_value = false;
  v->pending_len = 0;
  v->pending_start_sample = 0;
  v->expected_next_sample = 0;
  v->has_expected = false;
  viseme_ring_reset(v);
}

void viseme_free(VisemeAnalyzer *v) {
  if (!v)
    return;
  memset(v, 0, sizeof(VisemeAnalyzer));
}

void viseme_reset(VisemeAnalyzer *v) {
  if (!v)
    return;
  viseme_ring_reset(v);
  memset(&v->smoothed, 0, sizeof(VisemeFrame));
  memset(&v->last_value, 0, sizeof(VisemeFrame));
  v->has_last_value = false;
  v->pending_len = 0;
  v->pending_start_sample = 0;
  v->expected_next_sample = 0;
  v->has_expected = false;
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

static void viseme_ring_store(VisemeAnalyzer *v, int64_t frame_start_sample,
                              const VisemeFrame *frame) {
  int64_t frame_index = frame_start_sample / (int64_t)VISEME_FRAME_SIZE_SAMPLES;
  size_t slot = (size_t)frame_index & VISEME_RING_MASK;
  v->ring[slot] = *frame;
  v->ring_frame_index[slot] = frame_index;
  v->last_value = *frame;
  v->has_last_value = true;
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
    return;
  }

  if (v->has_expected && chunk_start_sample != v->expected_next_sample) {
    // Discontinuity: reset alignment state and restart accumulation.
    v->pending_len = 0;
    v->has_expected = false;
  }

  const int16_t *p = samples;
  size_t remaining = count;
  int64_t pos = chunk_start_sample;

  // Initialize an aligned frame accumulator if we're starting fresh.
  if (v->pending_len == 0 && !v->has_expected) {
    int64_t frame_start =
        (pos / (int64_t)VISEME_FRAME_SIZE_SAMPLES) * (int64_t)VISEME_FRAME_SIZE_SAMPLES;
    v->pending_start_sample = frame_start;
    int64_t missing = pos - frame_start;
    if (missing > 0 && missing < (int64_t)VISEME_FRAME_SIZE_SAMPLES) {
      memset(v->pending, 0, (size_t)missing * sizeof(int16_t));
      v->pending_len = (size_t)missing;
    }
    v->has_expected = true;
    v->expected_next_sample = pos;
  }

  while (remaining > 0) {
    // If needed, initialize pending_start_sample on first use.
    if (v->pending_len == 0) {
      v->pending_start_sample =
          (pos / (int64_t)VISEME_FRAME_SIZE_SAMPLES) * (int64_t)VISEME_FRAME_SIZE_SAMPLES;
    }

    size_t need = VISEME_FRAME_SIZE_SAMPLES - v->pending_len;
    size_t take = remaining < need ? remaining : need;
    memcpy(v->pending + v->pending_len, p, take * sizeof(int16_t));
    v->pending_len += take;

    p += take;
    remaining -= take;
    pos += (int64_t)take;
    v->expected_next_sample = pos;

    if (v->pending_len < VISEME_FRAME_SIZE_SAMPLES) {
      break;
    }

    // Compute features for the completed frame.
    float rms = compute_rms(v->pending, VISEME_FRAME_SIZE_SAMPLES, v->gain);
    float zcr = compute_zcr(v->pending, VISEME_FRAME_SIZE_SAMPLES);
    float env_var = compute_envelope_variance(v->pending, VISEME_FRAME_SIZE_SAMPLES);

    VisemeFrame raw;
    if (rms < v->threshold) {
      memset(&raw, 0, sizeof(VisemeFrame));
    } else {
      map_to_vowels(rms, zcr, env_var, &raw);
    }

    float alpha = v->smoothing;
    for (int i = 0; i < VISEME_COUNT; i++) {
      v->smoothed.weights[i] =
          alpha * raw.weights[i] + (1.0f - alpha) * v->smoothed.weights[i];
    }

    viseme_ring_store(v, v->pending_start_sample, &v->smoothed);
    v->frames_analyzed++;

    v->pending_start_sample += (int64_t)VISEME_FRAME_SIZE_SAMPLES;
    v->pending_len = 0;
  }
}

bool viseme_sample(VisemeAnalyzer *v, int64_t playback_sample,
                   VisemeFrame *out) {
  if (!v || !out)
    return false;

  if (!v->enabled) {
    memset(out, 0, sizeof(VisemeFrame));
    return false;
  }

  int64_t frame_index =
      playback_sample / (int64_t)VISEME_FRAME_SIZE_SAMPLES;
  int64_t frame_start_sample =
      frame_index * (int64_t)VISEME_FRAME_SIZE_SAMPLES;
  float t = (float)(playback_sample - frame_start_sample) /
            (float)VISEME_FRAME_SIZE_SAMPLES;
  if (t < 0.0f)
    t = 0.0f;
  if (t > 1.0f)
    t = 1.0f;

  size_t slot_a = (size_t)frame_index & VISEME_RING_MASK;
  bool has_a = (v->ring_frame_index[slot_a] == frame_index);

  VisemeFrame a;
  if (has_a) {
    a = v->ring[slot_a];
  } else if (v->has_last_value) {
    a = v->last_value;
  } else {
    memset(out, 0, sizeof(VisemeFrame));
    return false;
  }

  int64_t next_index = frame_index + 1;
  size_t slot_b = (size_t)next_index & VISEME_RING_MASK;
  bool has_b = (v->ring_frame_index[slot_b] == next_index);
  if (!has_b) {
    *out = a;
    return true;
  }

  VisemeFrame b = v->ring[slot_b];
  for (int i = 0; i < VISEME_COUNT; i++) {
    out->weights[i] = a.weights[i] + t * (b.weights[i] - a.weights[i]);
  }
  return true;
}

void viseme_set_enabled(VisemeAnalyzer *v, bool enabled) {
  if (!v)
    return;
  v->enabled = enabled;
  if (!enabled) {
    memset(&v->smoothed, 0, sizeof(VisemeFrame));
    memset(&v->last_value, 0, sizeof(VisemeFrame));
    v->has_last_value = false;
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
