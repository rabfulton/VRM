/*
 * Event System - Generic Time-Based Event Scheduling
 * ===================================================
 *
 * This module provides two primitives:
 *
 * 1. EventStream - For CONTINUOUS, interpolatable data (e.g., viseme weights).
 *    Data is sampled at render time with automatic interpolation between
 *    keyframes. Used by the lip sync system.
 *
 * 2. Event System - For DISCRETE, stateful events (e.g., blinks, winks,
 *    animation triggers, audio cues). Events have a lifecycle:
 *    SCHEDULED → ACTIVE → COMPLETE (with optional WAITING, PAUSED, CANCELED).
 *
 * DESIGN PRINCIPLES:
 * - Scheduler manages ordering/state/looping; subsystems manage execution
 * - Multi-clock: separate updates for wall time (seconds) vs audio samples
 * - Thread-safe: IPC pushes commands, main thread drains and applies
 * - Fixed-size pool for predictable memory usage
 *
 * TYPICAL USAGE:
 *   // IPC thread: schedule a blink
 *   event_cmd_schedule(&blink_event);
 *
 *   // Main loop:
 *   event_drain_commands();
 *   event_update_seconds(now_ns);
 *   event_update_audio(now_samples);
 *   event_foreach_active(EVENT_TYPE_BLINK, process_blink, &app);
 */

#ifndef EVENT_H
#define EVENT_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Configuration
// ============================================================================

#define EVENT_POOL_CAPACITY 512
#define EVENT_MAX_PAYLOAD 48
#define EVENT_CMD_QUEUE_CAPACITY 64

// ============================================================================
// Time Domains
// ============================================================================

typedef enum {
  EVENT_DOMAIN_SECONDS = 0,       // Wall time in nanoseconds (CLOCK_MONOTONIC)
  EVENT_DOMAIN_AUDIO_SAMPLES = 1, // Audio sample index @ 24kHz
} EventDomain;

// Assumed sample rate for AUDIO_SAMPLES timebase (keep in sync with audio system)
#define EVENT_AUDIO_SAMPLE_RATE 24000

// Conversion helpers
static inline int64_t event_ms_to_ns(int64_t ms) { return ms * 1000000LL; }
static inline int64_t event_ns_to_ms(int64_t ns) { return ns / 1000000LL; }
static inline int64_t event_samples_to_ns(int64_t samples, int sample_rate) {
  return (samples * 1000000000LL) / sample_rate;
}
static inline int64_t event_ns_to_samples(int64_t ns, int sample_rate) {
  return (ns * sample_rate) / 1000000000LL;
}
static inline int64_t event_ms_to_samples(int64_t ms) {
  return (ms * EVENT_AUDIO_SAMPLE_RATE) / 1000LL;
}

// ============================================================================
// Event State Machine
// ============================================================================

typedef enum {
  EVENT_STATE_FREE = 0,  // Slot is unused
  EVENT_STATE_SCHEDULED, // Waiting for start_time
  EVENT_STATE_WAITING,   // Blocked on dependency (after_event_id)
  EVENT_STATE_ACTIVE,    // Being processed by subsystem
  EVENT_STATE_PAUSED,    // Temporarily frozen
  EVENT_STATE_COMPLETE,  // Finished (may reschedule if looping)
  EVENT_STATE_CANCELED,  // Canceled; subsystem must cleanup
} EventState;

// ============================================================================
// Event Types (extend as needed)
// ============================================================================

typedef enum {
  EVENT_TYPE_NONE = 0,
  EVENT_TYPE_BLINK,
  EVENT_TYPE_WINK_L,
  EVENT_TYPE_WINK_R,
  EVENT_TYPE_EXPRESSION_SET,
  EVENT_TYPE_ANIM_TRIGGER,
  EVENT_TYPE_AUDIO_CUE,
  EVENT_TYPE_CUSTOM = 100,
} EventType;

// ============================================================================
// Event Flags
// ============================================================================

#define EVENT_FLAG_NONE 0x0000
#define EVENT_FLAG_SUBSYSTEM_OWNS_TIMING 0x0001 // Subsystem decides completion
#define EVENT_FLAG_AUTO_COMPLETE 0x0002    // Scheduler completes when now >= start_time+duration
#define EVENT_FLAG_RECALC_ON_REPEAT 0x0004 // Recalculate start_time on loop
#define EVENT_FLAG_ALLOW_STACKING 0x0008   // Multiple same type/track OK

// ============================================================================
// Event Structure
// ============================================================================

typedef struct {
  // Identity
  uint32_t id;       // Unique event ID (0 = invalid)
  uint16_t type;     // EventType
  uint16_t track_id; // Which entity/channel this targets

  // Time
  EventDomain domain;
  int64_t start_time; // Absolute time in domain units
  int64_t duration;   // 0 = instant, otherwise length in domain units

  // State
  EventState state;

  // Dependency
  uint32_t after_event_id; // 0 = none; wait for this event to complete first

  // Repeat behavior
  int16_t loop_count;    // -1 = infinite, 0 = no repeat, N = repeat N times
  int64_t loop_interval; // Start-to-start interval between repeats (0 = use duration)

  // Behavior
  uint16_t flags;

  // Payload
  uint16_t payload_size;
  uint8_t payload[EVENT_MAX_PAYLOAD];
} Event;

// ============================================================================
// Command Queue (for thread-safe scheduling from IPC)
// ============================================================================

typedef enum {
  EVENT_CMD_SCHEDULE,
  EVENT_CMD_CANCEL,
  EVENT_CMD_MODIFY,
  EVENT_CMD_PAUSE,
  EVENT_CMD_RESUME,
} EventCmdType;

typedef struct {
  EventCmdType cmd_type;
  uint32_t event_id; // For cancel/modify/pause/resume
  Event event;       // For schedule (copied in)
} EventCmd;

// ============================================================================
// EventStream (for continuous data like visemes) - unchanged from before
// ============================================================================

#define EVENT_STREAM_CAPACITY 2048
#define EVENT_STREAM_MASK (EVENT_STREAM_CAPACITY - 1)
#define EVENT_STREAM_MAX_PAYLOAD 64

typedef struct {
  int64_t time;
  uint8_t payload[EVENT_STREAM_MAX_PAYLOAD];
} StreamKeyframe;

typedef struct {
  StreamKeyframe buffer[EVENT_STREAM_CAPACITY];
  _Atomic size_t write_pos;
  size_t read_pos;
  size_t payload_size;
  EventDomain domain;
  int64_t last_sample_time;
  bool has_last_value;
  uint8_t last_value[EVENT_STREAM_MAX_PAYLOAD];
} EventStream;

typedef void (*LerpFunc)(const void *a, const void *b, float t, void *out,
                         size_t size);

// EventStream API (unchanged)
void event_lerp_floats(const void *a, const void *b, float t, void *out,
                       size_t size);
void event_stream_init(EventStream *s, EventDomain domain, size_t payload_size);
void event_stream_reset(EventStream *s);
bool event_stream_push(EventStream *s, int64_t time, const void *payload);
bool event_stream_sample(EventStream *s, int64_t t, void *out_payload,
                         LerpFunc lerp);
void event_stream_prune(EventStream *s, int64_t cutoff);
size_t event_stream_depth(const EventStream *s);
int64_t event_stream_oldest_time(const EventStream *s);
int64_t event_stream_newest_time(const EventStream *s);

// ============================================================================
// Event System API
// ============================================================================

// Initialize the event system (call once at startup)
void event_system_init(void);

// Shutdown and free resources
void event_system_shutdown(void);

// --- Thread-safe command queue (called from IPC thread) ---

// Queue a new event for scheduling (returns immediately; event ID returned via
// callback or can be incremented client-side)
bool event_cmd_schedule(const Event *e);

// Queue a cancel command
bool event_cmd_cancel(uint32_t event_id);

// Queue a pause command
bool event_cmd_pause(uint32_t event_id);

// Queue a resume command
bool event_cmd_resume(uint32_t event_id);

// --- Main thread API (called from main loop) ---

// Drain pending commands from the queue and apply them
void event_drain_commands(void);

// Advance scheduled → active for wall time events
void event_update_seconds(int64_t now_ns);

// Advance scheduled → active for audio sample events
void event_update_audio(int64_t now_samples);

// --- Subsystem API ---

// Iterate all active events of a given type
typedef void (*EventCallback)(Event *e, void *ctx);
void event_foreach_active(uint16_t type, EventCallback cb, void *ctx);

// Iterate all active events (any type)
void event_foreach_active_all(EventCallback cb, void *ctx);

// Mark an event as complete (subsystem signals it's done processing)
bool event_mark_complete(uint32_t event_id);

// --- Direct API (main thread only, for internal use) ---

// Schedule an event directly (returns event ID, 0 on failure)
uint32_t event_schedule(const Event *e);

// Cancel an event
bool event_cancel(uint32_t event_id);

// Get event by ID (returns NULL if not found or freed)
Event *event_get(uint32_t event_id);

// Get next available event ID (for client tracking)
uint32_t event_next_id(void);

#endif // EVENT_H
