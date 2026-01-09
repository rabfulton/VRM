#ifndef VISEME_H
#define VISEME_H

#include "event.h"
#include <stdbool.h>

// Vowel indices
#define VISEME_A 0
#define VISEME_I 1
#define VISEME_U 2
#define VISEME_E 3
#define VISEME_O 4
#define VISEME_COUNT 5

// Viseme frame payload (matches EventStream payload)
typedef struct {
  float weights[VISEME_COUNT]; // A, I, U, E, O [0-1]
} VisemeFrame;

typedef struct {
  // Output stream (consumed by render loop)
  EventStream stream;

  // Analysis parameters
  float gain;      // Input gain multiplier (default 1.0)
  float threshold; // RMS below this = silence (default 0.02)
  float smoothing; // Exponential smoothing factor (default 0.3)

  // Internal state
  VisemeFrame smoothed; // Current smoothed output
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
