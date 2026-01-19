#ifndef VISEME_H
#define VISEME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Vowel indices
#define VISEME_A 0
#define VISEME_I 1
#define VISEME_U 2
#define VISEME_E 3
#define VISEME_O 4
#define VISEME_COUNT 5

// Analysis parameters
#define VISEME_SAMPLE_RATE 24000
#define VISEME_FRAME_SIZE_MS 10
#define VISEME_FRAME_SIZE_SAMPLES (VISEME_SAMPLE_RATE * VISEME_FRAME_SIZE_MS / 1000) // 240

// Viseme ring buffer (indexed by frame index, i.e. sample/VISEME_FRAME_SIZE_SAMPLES)
// Sized to comfortably exceed the audio ring buffer horizon (~87s).
#define VISEME_RING_CAPACITY 16384
#define VISEME_RING_MASK (VISEME_RING_CAPACITY - 1)

typedef struct {
  float weights[VISEME_COUNT]; // A, I, U, E, O [0-1]
} VisemeFrame;

typedef struct {
  // Viseme frames aligned to the audio sample timeline.
  // Each slot is tagged with the absolute frame index to avoid ambiguity on wrap.
  VisemeFrame ring[VISEME_RING_CAPACITY];
  int64_t ring_frame_index[VISEME_RING_CAPACITY];

  // Partial frame accumulator for chunk boundary alignment.
  int16_t pending[VISEME_FRAME_SIZE_SAMPLES];
  size_t pending_len;
  int64_t pending_start_sample;
  int64_t expected_next_sample;
  bool has_expected;

  // Analysis parameters
  float gain;      // Input gain multiplier (default 1.0)
  float threshold; // RMS below this = silence (default 0.02)
  float smoothing; // Exponential smoothing factor (default 0.3)

  // Internal state
  VisemeFrame smoothed; // Current smoothed output
  VisemeFrame last_value;
  bool has_last_value;
  bool enabled;

  // Stats
  int64_t frames_analyzed;
} VisemeAnalyzer;

// Lifecycle
void viseme_init(VisemeAnalyzer *v);
void viseme_free(VisemeAnalyzer *v);
void viseme_reset(VisemeAnalyzer *v);

// Called from IPC audio ingest with decoded PCM
// chunk_start_sample should be audio.total_written BEFORE writing
void viseme_analyze(VisemeAnalyzer *v, const int16_t *samples, size_t count,
                    int64_t chunk_start_sample);

// Called from render loop
// playback_sample should be audio.total_read
bool viseme_sample(VisemeAnalyzer *v, int64_t playback_sample,
                   VisemeFrame *out);

// Configuration (can be called from IPC)
void viseme_set_enabled(VisemeAnalyzer *v, bool enabled);
void viseme_set_gain(VisemeAnalyzer *v, float gain);
void viseme_set_threshold(VisemeAnalyzer *v, float threshold);
void viseme_set_smoothing(VisemeAnalyzer *v, float smoothing);

#endif // VISEME_H
