/*
 * Event System Implementation
 *
 * Storage:
 * - Fixed pool of EVENT_POOL_CAPACITY events
 * - Min-heaps per domain for scheduled events (sorted by start_time)
 * - Separate list of active events for fast iteration
 * - SPSC command queue for thread-safe scheduling from IPC
 *
 * Threading:
 * - IPC thread pushes commands via event_cmd_*()
 * - Main thread drains commands in event_drain_commands()
 * - All state mutations happen on main thread
 */

#include "event.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Internal State
// ============================================================================

// Event pool
static Event g_pool[EVENT_POOL_CAPACITY];
static uint32_t g_next_id = 1;

// Active event tracking (indices into pool)
#define MAX_ACTIVE EVENT_POOL_CAPACITY
static uint32_t g_active_ids[MAX_ACTIVE];
static size_t g_active_count = 0;

// Waiting event tracking (indices into pool)
static uint32_t g_waiting_ids[EVENT_POOL_CAPACITY];
static size_t g_waiting_count = 0;

// Min-heaps for scheduled events per domain
// Heap stores pool indices, sorted by start_time
#define HEAP_CAPACITY EVENT_POOL_CAPACITY
static uint32_t g_heap_seconds[HEAP_CAPACITY];
static size_t g_heap_seconds_size = 0;
static uint32_t g_heap_audio[HEAP_CAPACITY];
static size_t g_heap_audio_size = 0;

// Command queue (SPSC ring buffer)
static EventCmd g_cmd_queue[EVENT_CMD_QUEUE_CAPACITY];
static _Atomic size_t g_cmd_write = 0;
static _Atomic size_t g_cmd_read = 0;

// Remember last "now" in each domain (used for dependency unblocking/resume)
static int64_t g_last_now_ns = 0;
static int64_t g_last_now_samples = 0;

// ============================================================================
// Heap Operations (min-heap by start_time)
// ============================================================================

static inline int64_t get_start_time(uint32_t pool_idx) {
  return g_pool[pool_idx].start_time;
}

static void heap_push(uint32_t *heap, size_t *size, uint32_t pool_idx) {
  if (*size >= HEAP_CAPACITY) {
    fprintf(stderr, "Event heap full, dropping event\n");
    return;
  }

  size_t i = (*size)++;
  heap[i] = pool_idx;

  // Bubble up
  while (i > 0) {
    size_t parent = (i - 1) / 2;
    if (get_start_time(heap[i]) < get_start_time(heap[parent])) {
      uint32_t tmp = heap[i];
      heap[i] = heap[parent];
      heap[parent] = tmp;
      i = parent;
    } else {
      break;
    }
  }
}

static uint32_t heap_peek(uint32_t *heap, size_t size) {
  if (size == 0)
    return UINT32_MAX;
  return heap[0];
}

static uint32_t heap_pop(uint32_t *heap, size_t *size) {
  if (*size == 0)
    return UINT32_MAX;

  uint32_t result = heap[0];
  heap[0] = heap[--(*size)];

  // Bubble down
  size_t i = 0;
  while (true) {
    size_t left = 2 * i + 1;
    size_t right = 2 * i + 2;
    size_t smallest = i;

    if (left < *size &&
        get_start_time(heap[left]) < get_start_time(heap[smallest])) {
      smallest = left;
    }
    if (right < *size &&
        get_start_time(heap[right]) < get_start_time(heap[smallest])) {
      smallest = right;
    }

    if (smallest != i) {
      uint32_t tmp = heap[i];
      heap[i] = heap[smallest];
      heap[smallest] = tmp;
      i = smallest;
    } else {
      break;
    }
  }

  return result;
}

static void heap_remove_id(uint32_t *heap, size_t *size, uint32_t pool_idx) {
  // Linear scan to find and remove (not optimal but rare operation)
  for (size_t i = 0; i < *size; i++) {
    if (heap[i] == pool_idx) {
      heap[i] = heap[--(*size)];
      // Re-heapify (simple approach: rebuild from that position)
      // For correctness, we should bubble up or down as needed
      // Simplified: just rebuild the heap
      for (size_t j = *size / 2; j > 0; j--) {
        size_t k = j - 1;
        while (true) {
          size_t left = 2 * k + 1;
          size_t right = 2 * k + 2;
          size_t smallest = k;
          if (left < *size &&
              get_start_time(heap[left]) < get_start_time(heap[smallest]))
            smallest = left;
          if (right < *size &&
              get_start_time(heap[right]) < get_start_time(heap[smallest]))
            smallest = right;
          if (smallest != k) {
            uint32_t tmp = heap[k];
            heap[k] = heap[smallest];
            heap[smallest] = tmp;
            k = smallest;
          } else {
            break;
          }
        }
      }
      return;
    }
  }
}

// ============================================================================
// Active List Operations
// ============================================================================

static void active_add(uint32_t pool_idx) {
  if (g_active_count >= MAX_ACTIVE) {
    fprintf(stderr, "Event active list full\n");
    return;
  }
  g_active_ids[g_active_count++] = pool_idx;
}

static void active_remove(uint32_t pool_idx) {
  for (size_t i = 0; i < g_active_count; i++) {
    if (g_active_ids[i] == pool_idx) {
      g_active_ids[i] = g_active_ids[--g_active_count];
      return;
    }
  }
}

static void waiting_add(uint32_t pool_idx) {
  if (g_waiting_count >= EVENT_POOL_CAPACITY) {
    fprintf(stderr, "Event waiting list full\n");
    return;
  }
  g_waiting_ids[g_waiting_count++] = pool_idx;
}

static void waiting_remove(uint32_t pool_idx) {
  for (size_t i = 0; i < g_waiting_count; i++) {
    if (g_waiting_ids[i] == pool_idx) {
      g_waiting_ids[i] = g_waiting_ids[--g_waiting_count];
      return;
    }
  }
}

// ============================================================================
// Pool Management
// ============================================================================

static uint32_t pool_find_free_slot(void) {
  for (uint32_t i = 0; i < EVENT_POOL_CAPACITY; i++) {
    if (g_pool[i].state == EVENT_STATE_FREE) {
      return i;
    }
  }
  return UINT32_MAX;
}

static uint32_t pool_find_by_id(uint32_t id) {
  if (id == 0)
    return UINT32_MAX;
  for (uint32_t i = 0; i < EVENT_POOL_CAPACITY; i++) {
    if (g_pool[i].id == id && g_pool[i].state != EVENT_STATE_FREE) {
      return i;
    }
  }
  return UINT32_MAX;
}

// ============================================================================
// Event System Init/Shutdown
// ============================================================================

void event_system_init(void) {
  memset(g_pool, 0, sizeof(g_pool));
  g_next_id = 1;
  g_active_count = 0;
  g_waiting_count = 0;
  g_heap_seconds_size = 0;
  g_heap_audio_size = 0;
  atomic_store(&g_cmd_write, 0);
  atomic_store(&g_cmd_read, 0);
  g_last_now_ns = 0;
  g_last_now_samples = 0;
  printf("Event system initialized (pool=%d, heaps ready)\n",
         EVENT_POOL_CAPACITY);
}

void event_system_shutdown(void) {
  memset(g_pool, 0, sizeof(g_pool));
  g_active_count = 0;
  g_waiting_count = 0;
  g_heap_seconds_size = 0;
  g_heap_audio_size = 0;
}

// ============================================================================
// Command Queue (Thread-Safe API)
// ============================================================================

static bool cmd_push(const EventCmd *cmd) {
  size_t w = atomic_load_explicit(&g_cmd_write, memory_order_relaxed);
  size_t next_w = (w + 1) % EVENT_CMD_QUEUE_CAPACITY;

  // Check if full (SPSC ring; safe because g_cmd_read is atomic)
  size_t r = atomic_load_explicit(&g_cmd_read, memory_order_acquire);
  if (next_w == r) {
    fprintf(stderr, "Event command queue full\n");
    return false;
  }

  g_cmd_queue[w] = *cmd;
  atomic_store_explicit(&g_cmd_write, next_w, memory_order_release);
  return true;
}

bool event_cmd_schedule(const Event *e) {
  EventCmd cmd = {.cmd_type = EVENT_CMD_SCHEDULE, .event = *e};
  return cmd_push(&cmd);
}

bool event_cmd_cancel(uint32_t event_id) {
  EventCmd cmd = {.cmd_type = EVENT_CMD_CANCEL, .event_id = event_id};
  return cmd_push(&cmd);
}

bool event_cmd_pause(uint32_t event_id) {
  EventCmd cmd = {.cmd_type = EVENT_CMD_PAUSE, .event_id = event_id};
  return cmd_push(&cmd);
}

bool event_cmd_resume(uint32_t event_id) {
  EventCmd cmd = {.cmd_type = EVENT_CMD_RESUME, .event_id = event_id};
  return cmd_push(&cmd);
}

void event_drain_commands(void) {
  size_t w = atomic_load_explicit(&g_cmd_write, memory_order_acquire);
  size_t r = atomic_load_explicit(&g_cmd_read, memory_order_relaxed);

  while (r != w) {
    EventCmd *cmd = &g_cmd_queue[r];

    switch (cmd->cmd_type) {
    case EVENT_CMD_SCHEDULE:
      event_schedule(&cmd->event);
      break;
    case EVENT_CMD_CANCEL:
      event_cancel(cmd->event_id);
      break;
    case EVENT_CMD_PAUSE: {
      uint32_t idx = pool_find_by_id(cmd->event_id);
      if (idx != UINT32_MAX && g_pool[idx].state == EVENT_STATE_ACTIVE) {
        g_pool[idx].state = EVENT_STATE_PAUSED;
      }
    } break;
    case EVENT_CMD_RESUME: {
      uint32_t idx = pool_find_by_id(cmd->event_id);
      if (idx != UINT32_MAX && g_pool[idx].state == EVENT_STATE_PAUSED) {
        g_pool[idx].state = EVENT_STATE_ACTIVE;
      }
    } break;
    default:
      break;
    }

    r = (r + 1) % EVENT_CMD_QUEUE_CAPACITY;
  }

  atomic_store_explicit(&g_cmd_read, r, memory_order_release);
}

// ============================================================================
// Direct API (Main Thread Only)
// ============================================================================

uint32_t event_next_id(void) { return g_next_id; }

uint32_t event_schedule(const Event *e) {
  uint32_t slot = pool_find_free_slot();
  if (slot == UINT32_MAX) {
    fprintf(stderr, "Event pool full, cannot schedule\n");
    return 0;
  }

  // Copy event into pool
  g_pool[slot] = *e;
  g_pool[slot].id = g_next_id++;
  g_pool[slot].state = EVENT_STATE_SCHEDULED;

  // Check for dependency
  if (e->after_event_id != 0) {
    uint32_t dep_idx = pool_find_by_id(e->after_event_id);
    if (dep_idx != UINT32_MAX && g_pool[dep_idx].state != EVENT_STATE_FREE &&
        g_pool[dep_idx].state != EVENT_STATE_COMPLETE) {
      // Dependency not yet complete - go to WAITING
      g_pool[slot].state = EVENT_STATE_WAITING;
      waiting_add(slot);
      return g_pool[slot].id;
    }
  }

  // Add to appropriate heap
  if (e->domain == EVENT_DOMAIN_SECONDS) {
    heap_push(g_heap_seconds, &g_heap_seconds_size, slot);
  } else {
    heap_push(g_heap_audio, &g_heap_audio_size, slot);
  }

  return g_pool[slot].id;
}

bool event_cancel(uint32_t event_id) {
  uint32_t idx = pool_find_by_id(event_id);
  if (idx == UINT32_MAX)
    return false;

  Event *e = &g_pool[idx];

  switch (e->state) {
  case EVENT_STATE_SCHEDULED:
    // Remove from heap
    if (e->domain == EVENT_DOMAIN_SECONDS) {
      heap_remove_id(g_heap_seconds, &g_heap_seconds_size, idx);
    } else {
      heap_remove_id(g_heap_audio, &g_heap_audio_size, idx);
    }
    e->state = EVENT_STATE_FREE;
    e->id = 0;
    break;

  case EVENT_STATE_WAITING:
    waiting_remove(idx);
    e->state = EVENT_STATE_FREE;
    e->id = 0;
    break;

  case EVENT_STATE_ACTIVE:
  case EVENT_STATE_PAUSED:
    // Let subsystem cleanup on next iteration
    e->state = EVENT_STATE_CANCELED;
    break;

  default:
    break;
  }

  return true;
}

Event *event_get(uint32_t event_id) {
  uint32_t idx = pool_find_by_id(event_id);
  if (idx == UINT32_MAX)
    return NULL;
  return &g_pool[idx];
}

// ============================================================================
// Update Functions (Main Loop)
// ============================================================================

static void check_waiting_events(int64_t now_ns, int64_t now_samples) {
  // Check if any WAITING events can now be scheduled
  for (size_t i = 0; i < g_waiting_count;) {
    uint32_t idx = g_waiting_ids[i];
    if (idx >= EVENT_POOL_CAPACITY || g_pool[idx].state != EVENT_STATE_WAITING) {
      // Stale entry; remove
      g_waiting_ids[i] = g_waiting_ids[--g_waiting_count];
      continue;
    }

    uint32_t dep_idx = pool_find_by_id(g_pool[idx].after_event_id);
      bool dep_done = (dep_idx == UINT32_MAX) ||
                      (g_pool[dep_idx].state == EVENT_STATE_COMPLETE) ||
                      (g_pool[dep_idx].state == EVENT_STATE_FREE);
    if (!dep_done) {
      i++;
      continue;
    }

    // Unblock: transition to SCHEDULED and push into appropriate heap.
    Event *e = &g_pool[idx];
    e->state = EVENT_STATE_SCHEDULED;
    // Common convention: if start_time==0, start immediately on unblock.
    if (e->start_time == 0) {
      if (e->domain == EVENT_DOMAIN_SECONDS) {
        if (now_ns != 0)
          e->start_time = now_ns;
      } else {
        if (now_samples != 0)
          e->start_time = now_samples;
      }
    }

    if (e->domain == EVENT_DOMAIN_SECONDS) {
      heap_push(g_heap_seconds, &g_heap_seconds_size, idx);
    } else {
      heap_push(g_heap_audio, &g_heap_audio_size, idx);
    }

    // Remove from waiting list (swap-remove)
    g_waiting_ids[i] = g_waiting_ids[--g_waiting_count];
  }
}

static int64_t default_repeat_interval(EventDomain domain) {
  // Only used if both loop_interval and duration are 0.
  if (domain == EVENT_DOMAIN_SECONDS) {
    return event_ms_to_ns(100);
  }
  return event_ms_to_samples(100);
}

static void handle_complete_events(int64_t now_ns, int64_t now_samples) {
  // Auto-complete duration-based events if requested.
  for (size_t i = 0; i < g_active_count; i++) {
    uint32_t idx = g_active_ids[i];
    Event *e = &g_pool[idx];

    if (e->state != EVENT_STATE_ACTIVE)
      continue;
    if ((e->flags & EVENT_FLAG_SUBSYSTEM_OWNS_TIMING) != 0)
      continue;
    if ((e->flags & EVENT_FLAG_AUTO_COMPLETE) == 0)
      continue;
    if (e->duration <= 0)
      continue;

    bool done = false;
    if (e->domain == EVENT_DOMAIN_SECONDS) {
      if (now_ns >= e->start_time + e->duration)
        done = true;
    } else {
      if (now_samples >= e->start_time + e->duration)
        done = true;
    }

    if (done) {
      e->state = EVENT_STATE_COMPLETE;
    }
  }

  // Process COMPLETE events: reschedule if looping, otherwise free
  for (size_t i = 0; i < g_active_count;) {
    uint32_t idx = g_active_ids[i];
    Event *e = &g_pool[idx];

    if (e->state == EVENT_STATE_COMPLETE) {
      active_remove(idx);

      if (e->loop_count != 0) {
        // Reschedule
        if (e->loop_count > 0)
          e->loop_count--;

        // loop_interval is interpreted as start-to-start interval.
        int64_t interval = e->loop_interval;
        if (interval == 0)
          interval = e->duration;
        if (interval == 0)
          interval = default_repeat_interval(e->domain);

        e->start_time = e->start_time + interval;
        e->state = EVENT_STATE_SCHEDULED;

        if (e->domain == EVENT_DOMAIN_SECONDS) {
          heap_push(g_heap_seconds, &g_heap_seconds_size, idx);
        } else {
          heap_push(g_heap_audio, &g_heap_audio_size, idx);
        }
      } else {
        // Free the slot
        e->state = EVENT_STATE_FREE;
        e->id = 0;
      }
      // Don't increment i, we removed an element
    } else if (e->state == EVENT_STATE_CANCELED ||
               e->state == EVENT_STATE_FREE) {
      active_remove(idx);
      if (e->state != EVENT_STATE_FREE) {
        e->state = EVENT_STATE_FREE;
        e->id = 0;
      }
    } else {
      i++;
    }
  }
}

void event_update_seconds(int64_t now_ns) {
  g_last_now_ns = now_ns;
  check_waiting_events(now_ns, g_last_now_samples);

  // Pop events from heap that should activate
  while (g_heap_seconds_size > 0) {
    uint32_t idx = heap_peek(g_heap_seconds, g_heap_seconds_size);
    Event *e = &g_pool[idx];

    if (e->start_time <= now_ns) {
      heap_pop(g_heap_seconds, &g_heap_seconds_size);
      e->state = EVENT_STATE_ACTIVE;
      active_add(idx);

      // Auto-complete for instant events without subsystem ownership
      if (e->duration == 0 && !(e->flags & EVENT_FLAG_SUBSYSTEM_OWNS_TIMING)) {
        e->state = EVENT_STATE_COMPLETE;
      }
    } else {
      break;
    }
  }

  handle_complete_events(now_ns, g_last_now_samples);
}

void event_update_audio(int64_t now_samples) {
  g_last_now_samples = now_samples;
  check_waiting_events(g_last_now_ns, now_samples);

  // Pop events from audio heap
  while (g_heap_audio_size > 0) {
    uint32_t idx = heap_peek(g_heap_audio, g_heap_audio_size);
    Event *e = &g_pool[idx];

    if (e->start_time <= now_samples) {
      heap_pop(g_heap_audio, &g_heap_audio_size);
      e->state = EVENT_STATE_ACTIVE;
      active_add(idx);

      if (e->duration == 0 && !(e->flags & EVENT_FLAG_SUBSYSTEM_OWNS_TIMING)) {
        e->state = EVENT_STATE_COMPLETE;
      }
    } else {
      break;
    }
  }

  handle_complete_events(g_last_now_ns, now_samples);
}

// ============================================================================
// Subsystem API
// ============================================================================

void event_foreach_active(uint16_t type, EventCallback cb, void *ctx) {
  for (size_t i = 0; i < g_active_count; i++) {
    Event *e = &g_pool[g_active_ids[i]];
    if (e->type == type &&
        (e->state == EVENT_STATE_ACTIVE || e->state == EVENT_STATE_CANCELED ||
         e->state == EVENT_STATE_PAUSED)) {
      cb(e, ctx);
    }
  }
}

void event_foreach_active_all(EventCallback cb, void *ctx) {
  for (size_t i = 0; i < g_active_count; i++) {
    Event *e = &g_pool[g_active_ids[i]];
    if (e->state == EVENT_STATE_ACTIVE || e->state == EVENT_STATE_CANCELED ||
        e->state == EVENT_STATE_PAUSED) {
      cb(e, ctx);
    }
  }
}

bool event_mark_complete(uint32_t event_id) {
  uint32_t idx = pool_find_by_id(event_id);
  if (idx == UINT32_MAX)
    return false;

  Event *e = &g_pool[idx];
  if (e->state == EVENT_STATE_ACTIVE || e->state == EVENT_STATE_PAUSED) {
    e->state = EVENT_STATE_COMPLETE;
    return true;
  }
  return false;
}

// ============================================================================
// EventStream Implementation (unchanged from before)
// ============================================================================

void event_lerp_floats(const void *a, const void *b, float t, void *out,
                       size_t size) {
  const float *fa = (const float *)a;
  const float *fb = (const float *)b;
  float *fo = (float *)out;
  size_t count = size / sizeof(float);

  for (size_t i = 0; i < count; i++) {
    fo[i] = fa[i] + t * (fb[i] - fa[i]);
  }
}

void event_stream_init(EventStream *s, EventDomain domain,
                       size_t payload_size) {
  if (!s)
    return;
  memset(s, 0, sizeof(EventStream));
  s->domain = domain;
  s->payload_size = payload_size;
  if (payload_size > EVENT_STREAM_MAX_PAYLOAD) {
    s->payload_size = EVENT_STREAM_MAX_PAYLOAD;
  }
  atomic_store(&s->write_pos, 0);
  s->read_pos = 0;
  s->has_last_value = false;
}

void event_stream_reset(EventStream *s) {
  if (!s)
    return;
  atomic_store(&s->write_pos, 0);
  s->read_pos = 0;
  s->has_last_value = false;
  s->last_sample_time = 0;
  memset(s->last_value, 0, sizeof(s->last_value));
}

bool event_stream_push(EventStream *s, int64_t time, const void *payload) {
  if (!s || !payload)
    return false;

  size_t w = atomic_load_explicit(&s->write_pos, memory_order_relaxed);
  size_t next_w = (w + 1) & EVENT_STREAM_MASK;

  StreamKeyframe *kf = &s->buffer[w];
  kf->time = time;
  memcpy(kf->payload, payload, s->payload_size);

  atomic_store_explicit(&s->write_pos, next_w, memory_order_release);
  return true;
}

static bool find_bracket(EventStream *s, int64_t t, size_t *out_idx_a,
                         size_t *out_idx_b, size_t read_start, size_t count) {
  if (count == 0)
    return false;

  if (count == 1) {
    *out_idx_a = read_start;
    *out_idx_b = read_start;
    return true;
  }

  size_t prev_idx = read_start;
  for (size_t i = 1; i < count; i++) {
    size_t idx = (read_start + i) & EVENT_STREAM_MASK;
    if (s->buffer[idx].time >= t) {
      *out_idx_a = prev_idx;
      *out_idx_b = idx;
      return true;
    }
    prev_idx = idx;
  }

  size_t last_idx = (read_start + count - 1) & EVENT_STREAM_MASK;
  *out_idx_a = last_idx;
  *out_idx_b = last_idx;
  return true;
}

bool event_stream_sample(EventStream *s, int64_t t, void *out_payload,
                         LerpFunc lerp) {
  if (!s || !out_payload)
    return false;

  size_t w = atomic_load_explicit(&s->write_pos, memory_order_acquire);
  size_t r = s->read_pos;

  size_t count;
  if (w >= r) {
    count = w - r;
  } else {
    count = EVENT_STREAM_CAPACITY - r + w;
  }

  if (count == 0) {
    if (s->has_last_value) {
      memcpy(out_payload, s->last_value, s->payload_size);
      return true;
    }
    return false;
  }

  size_t idx_a, idx_b;
  if (!find_bracket(s, t, &idx_a, &idx_b, r, count)) {
    return false;
  }

  StreamKeyframe *kf_a = &s->buffer[idx_a];
  StreamKeyframe *kf_b = &s->buffer[idx_b];

  if (idx_a == idx_b || kf_a->time == kf_b->time) {
    memcpy(out_payload, kf_a->payload, s->payload_size);
  } else {
    float factor = (float)(t - kf_a->time) / (float)(kf_b->time - kf_a->time);
    if (factor < 0.0f)
      factor = 0.0f;
    if (factor > 1.0f)
      factor = 1.0f;

    if (lerp) {
      lerp(kf_a->payload, kf_b->payload, factor, out_payload, s->payload_size);
    } else {
      event_lerp_floats(kf_a->payload, kf_b->payload, factor, out_payload,
                        s->payload_size);
    }
  }

  memcpy(s->last_value, out_payload, s->payload_size);
  s->has_last_value = true;
  s->last_sample_time = t;

  return true;
}

void event_stream_prune(EventStream *s, int64_t cutoff) {
  if (!s)
    return;

  size_t w = atomic_load_explicit(&s->write_pos, memory_order_acquire);
  size_t r = s->read_pos;

  while (r != w) {
    if (s->buffer[r].time >= cutoff) {
      break;
    }
    r = (r + 1) & EVENT_STREAM_MASK;
  }

  s->read_pos = r;
}

size_t event_stream_depth(const EventStream *s) {
  if (!s)
    return 0;

  size_t w = atomic_load_explicit(&s->write_pos, memory_order_acquire);
  size_t r = s->read_pos;

  if (w >= r) {
    return w - r;
  }
  return EVENT_STREAM_CAPACITY - r + w;
}

int64_t event_stream_oldest_time(const EventStream *s) {
  if (!s || event_stream_depth(s) == 0)
    return INT64_MAX;
  return s->buffer[s->read_pos].time;
}

int64_t event_stream_newest_time(const EventStream *s) {
  if (!s || event_stream_depth(s) == 0)
    return INT64_MIN;
  size_t w = atomic_load_explicit(&s->write_pos, memory_order_acquire);
  size_t last = (w - 1) & EVENT_STREAM_MASK;
  return s->buffer[last].time;
}
