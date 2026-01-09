/*
 * Event System - Generic Time-Based Data Scheduling
 * ==================================================
 *
 * This module provides two primitives for scheduling and consuming time-stamped
 * data in a producer-consumer pattern:
 *
 * 1. EventStream - For CONTINUOUS, interpolatable data (e.g., viseme weights,
 *    blend shapes, transform curves). Data is sampled at render time with
 *    automatic interpolation between keyframes.
 *
 * 2. EventQueue - For DISCRETE, one-shot triggers (e.g., play animation,
 *    toggle feature, fire callback). Events are consumed when their scheduled
 *    time arrives.
 *
 * DESIGN PRINCIPLES:
 * - SPSC (Single Producer, Single Consumer) lock-free ring buffers
 * - Render loop stays in control (pull-based, not callback-driven)
 * - Multiple time domains: audio sample indices or wall-clock seconds
 * - Flexible payloads: up to 64 bytes inline, no heap allocation
 *
 * TYPICAL USAGE (Lip Sync Example):
 *   Producer (IPC audio ingest):
 *     event_stream_push(&stream, sample_time, &viseme_weights);
 *
 *   Consumer (render loop):
 *     if (event_stream_sample(&stream, playback_time, &weights, NULL)) {
 *       apply_viseme(weights);
 *     }
 *     event_stream_prune(&stream, playback_time - retention);
 *
 * FUTURE EXTENSIBILITY:
 * - Animation triggers, camera cues, expression sets
 * - Multiple streams per entity (face, body, accessories)
 * - Network-synchronized events with clock correction
 */

#ifndef EVENT_H
#define EVENT_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Time Domains
// ============================================================================

typedef enum {
  EVENT_DOMAIN_AUDIO_SAMPLES, // int64_t samples @ 24kHz
  EVENT_DOMAIN_SECONDS,       // double seconds (stored as int64 micros)
} EventDomain;

// Convert between domains (requires knowing sample rate)
static inline int64_t event_time_to_samples(double seconds, int sample_rate) {
  return (int64_t)(seconds * sample_rate);
}

static inline double event_time_to_seconds(int64_t samples, int sample_rate) {
  return (double)samples / sample_rate;
}

// ============================================================================
// Continuous Streams (for interpolatable data like visemes)
// ============================================================================

#define EVENT_STREAM_CAPACITY 2048 // Power of 2, ~20sec of viseme data @ 100Hz
#define EVENT_STREAM_MASK (EVENT_STREAM_CAPACITY - 1)
#define EVENT_STREAM_MAX_PAYLOAD 64

// Keyframe in a continuous stream
typedef struct {
  int64_t time;                              // Timestamp in domain units
  uint8_t payload[EVENT_STREAM_MAX_PAYLOAD]; // Inline payload
} StreamKeyframe;

// SPSC ring buffer for continuous data
typedef struct {
  StreamKeyframe buffer[EVENT_STREAM_CAPACITY];
  _Atomic size_t write_pos; // Producer writes here
  size_t read_pos; // Consumer reads here (single consumer, no atomic needed)
  size_t payload_size; // Actual payload size in bytes
  EventDomain domain;
  int64_t last_sample_time; // For hold-last-value behavior
  bool has_last_value;      // Whether we have any data to hold
  uint8_t last_value[EVENT_STREAM_MAX_PAYLOAD]; // Cached last sampled value
} EventStream;

// Interpolation function pointer
// Interpolates between `a` and `b` at factor `t` (0-1), writes to `out`
typedef void (*LerpFunc)(const void *a, const void *b, float t, void *out,
                         size_t size);

// Default linear interpolation for float arrays
void event_lerp_floats(const void *a, const void *b, float t, void *out,
                       size_t size);

// Producer API
void event_stream_init(EventStream *s, EventDomain domain, size_t payload_size);
void event_stream_reset(EventStream *s);
bool event_stream_push(EventStream *s, int64_t time, const void *payload);

// Consumer API
// Sample at time `t`, interpolating if possible. Returns false if no data.
// `lerp` can be NULL to use default float interpolation
bool event_stream_sample(EventStream *s, int64_t t, void *out_payload,
                         LerpFunc lerp);

// Maintenance
void event_stream_prune(EventStream *s, int64_t cutoff);
size_t event_stream_depth(const EventStream *s);
int64_t event_stream_oldest_time(const EventStream *s);
int64_t event_stream_newest_time(const EventStream *s);

// ============================================================================
// Discrete Event Queue (for one-shot triggers)
// ============================================================================

typedef enum {
  EVENT_TYPE_ANIMATION_PLAY, // Play named animation
  EVENT_TYPE_EXPRESSION_SET, // Set expression weight
  EVENT_TYPE_FEATURE_TOGGLE, // Enable/disable feature
  EVENT_TYPE_CUSTOM,         // User-defined
} DiscreteEventType;

#define EVENT_DISCRETE_MAX_PAYLOAD 56

typedef struct {
  int64_t time;      // When to fire
  uint16_t type;     // DiscreteEventType
  uint16_t track_id; // Target entity/channel
  uint16_t payload_size;
  uint16_t _reserved;
  uint8_t payload[EVENT_DISCRETE_MAX_PAYLOAD]; // Inline payload
} DiscreteEvent;

#define EVENT_QUEUE_CAPACITY 64
#define EVENT_QUEUE_MASK (EVENT_QUEUE_CAPACITY - 1)

typedef struct {
  DiscreteEvent buffer[EVENT_QUEUE_CAPACITY];
  _Atomic size_t write_pos;
  size_t read_pos;
  EventDomain domain;
} EventQueue;

// Producer API
void event_queue_init(EventQueue *q, EventDomain domain);
void event_queue_reset(EventQueue *q);
bool event_queue_push(EventQueue *q, const DiscreteEvent *e);

// Consumer API
// Pop all events with time <= `now`. Returns number popped.
// Events are copied to `out` array (caller provides buffer).
size_t event_queue_pop_until(EventQueue *q, int64_t now, DiscreteEvent *out,
                             size_t max_out);

// Peek next event time (for scheduling). Returns INT64_MAX if empty.
int64_t event_queue_peek_time(const EventQueue *q);

// Debug
size_t event_queue_depth(const EventQueue *q);

#endif // EVENT_H
