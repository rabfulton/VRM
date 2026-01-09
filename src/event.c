#include "event.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// Default Interpolation
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

// ============================================================================
// EventStream Implementation
// ============================================================================

void event_stream_init(EventStream *s, EventDomain domain,
                       size_t payload_size) {
  if (!s)
    return;
  memset(s, 0, sizeof(EventStream));
  s->domain = domain;
  s->payload_size = payload_size;
  if (payload_size > EVENT_STREAM_MAX_PAYLOAD) {
    fprintf(stderr, "EventStream: payload_size %zu exceeds max %d, clamping\n",
            payload_size, EVENT_STREAM_MAX_PAYLOAD);
    s->payload_size = EVENT_STREAM_MAX_PAYLOAD;
  }
  atomic_store(&s->write_pos, 0);
  s->read_pos = 0;
  s->has_last_value = false;
  s->last_sample_time = 0;
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

  // Check if buffer is full (would overwrite unread data)
  // We allow overwriting old data since this is a sliding window
  // The read_pos is not atomic from producer side, so we just push

  StreamKeyframe *kf = &s->buffer[w];
  kf->time = time;
  memcpy(kf->payload, payload, s->payload_size);

  // Release fence ensures payload is visible before write_pos update
  atomic_store_explicit(&s->write_pos, next_w, memory_order_release);

  return true;
}

// Find keyframes bracketing time `t` for interpolation
// Returns true if found, sets out_idx_a and out_idx_b
static bool find_bracket(EventStream *s, int64_t t, size_t *out_idx_a,
                         size_t *out_idx_b, size_t read_start, size_t count) {
  if (count == 0)
    return false;

  // Single keyframe case
  if (count == 1) {
    *out_idx_a = read_start;
    *out_idx_b = read_start;
    return true;
  }

  // Find the two keyframes that bracket time t
  // Keyframes should be in chronological order
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

  // t is past all keyframes - use last two for extrapolation/hold
  size_t last_idx = (read_start + count - 1) & EVENT_STREAM_MASK;
  *out_idx_a = last_idx;
  *out_idx_b = last_idx;
  return true;
}

bool event_stream_sample(EventStream *s, int64_t t, void *out_payload,
                         LerpFunc lerp) {
  if (!s || !out_payload)
    return false;

  // Acquire fence ensures we see all writes before write_pos
  size_t w = atomic_load_explicit(&s->write_pos, memory_order_acquire);
  size_t r = s->read_pos;

  // Calculate available count
  size_t count;
  if (w >= r) {
    count = w - r;
  } else {
    count = EVENT_STREAM_CAPACITY - r + w;
  }

  if (count == 0) {
    // No data - return last value if available
    if (s->has_last_value) {
      memcpy(out_payload, s->last_value, s->payload_size);
      return true;
    }
    return false;
  }

  // Find bracketing keyframes
  size_t idx_a, idx_b;
  if (!find_bracket(s, t, &idx_a, &idx_b, r, count)) {
    return false;
  }

  StreamKeyframe *kf_a = &s->buffer[idx_a];
  StreamKeyframe *kf_b = &s->buffer[idx_b];

  if (idx_a == idx_b || kf_a->time == kf_b->time) {
    // Single point or same time - no interpolation
    memcpy(out_payload, kf_a->payload, s->payload_size);
  } else {
    // Interpolate
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

  // Cache last value
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

  // Advance read_pos past old keyframes
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

// ============================================================================
// EventQueue Implementation
// ============================================================================

void event_queue_init(EventQueue *q, EventDomain domain) {
  if (!q)
    return;
  memset(q, 0, sizeof(EventQueue));
  q->domain = domain;
  atomic_store(&q->write_pos, 0);
  q->read_pos = 0;
}

void event_queue_reset(EventQueue *q) {
  if (!q)
    return;
  atomic_store(&q->write_pos, 0);
  q->read_pos = 0;
}

bool event_queue_push(EventQueue *q, const DiscreteEvent *e) {
  if (!q || !e)
    return false;

  size_t w = atomic_load_explicit(&q->write_pos, memory_order_relaxed);
  size_t next_w = (w + 1) & EVENT_QUEUE_MASK;

  // Check if full (would overwrite unread)
  if (next_w == q->read_pos) {
    fprintf(stderr, "EventQueue: buffer full, dropping event\n");
    return false;
  }

  q->buffer[w] = *e;
  atomic_store_explicit(&q->write_pos, next_w, memory_order_release);

  return true;
}

size_t event_queue_pop_until(EventQueue *q, int64_t now, DiscreteEvent *out,
                             size_t max_out) {
  if (!q || !out || max_out == 0)
    return 0;

  size_t w = atomic_load_explicit(&q->write_pos, memory_order_acquire);
  size_t r = q->read_pos;
  size_t popped = 0;

  while (r != w && popped < max_out) {
    DiscreteEvent *e = &q->buffer[r];
    if (e->time > now) {
      break; // Events are assumed to be in time order
    }
    out[popped++] = *e;
    r = (r + 1) & EVENT_QUEUE_MASK;
  }

  q->read_pos = r;
  return popped;
}

int64_t event_queue_peek_time(const EventQueue *q) {
  if (!q)
    return INT64_MAX;

  size_t w = atomic_load_explicit(&q->write_pos, memory_order_acquire);
  size_t r = q->read_pos;

  if (r == w)
    return INT64_MAX;

  return q->buffer[r].time;
}

size_t event_queue_depth(const EventQueue *q) {
  if (!q)
    return 0;

  size_t w = atomic_load_explicit(&q->write_pos, memory_order_acquire);
  size_t r = q->read_pos;

  if (w >= r) {
    return w - r;
  }
  return EVENT_QUEUE_CAPACITY - r + w;
}
