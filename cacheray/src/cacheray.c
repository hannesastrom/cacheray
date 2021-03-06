#if __has_feature(thread_sanitizer)
#error "Don't sanitize cacheray"
#endif

#include "cacheray/cacheray.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef DEBUG
#define CACHERAY_DPRINT(...) printf(__VA_ARGS__)
#else
#define CACHERAY_DPRINT(...)                                                   \
  do {                                                                         \
  } while (0)
#endif

#define CACHERAY_ASSERT(...) assert(__VA_ARGS__)

#define buf_write(X, Y) _Generic((X), event_type_t:)

static char *buf; // The actual buf used to log events
static unsigned long buf_idx;
static unsigned long buf_len;
static unsigned int amount_buf;
static int enabled;
static cacheray_buf_cb callback = NULL;

static unsigned char string_copy(char *dest, const char *src) {
  unsigned int i = 0;
  char c;
  while ((c = src[i])) {
    dest[i] = c;
    i++;
  }
  dest[i] = '\0';
  return (unsigned char)i;
}

static void zero_buffer(void) {
  for (unsigned long i = 0; i < buf_len; i++)
    ((char *)buf)[i] = '\0';
}

static void copy_to_buf(const void *ptr, unsigned long size) {
  // safety check
  if (buf_idx + size >= buf_len) {
    // will write outside buf
    // TODO: send signal or something
#ifdef DEBUG
    abort();
#endif
    return;
  }
  for (unsigned long i = 0; i < size; i++) {
    buf[buf_idx + i] = ((char *)ptr)[i];
  }
  CACHERAY_DPRINT("copied type %d to buf: N = %d\n",
                  ((cacheray_log_t *)ptr)->type, amount_buf);
  buf_idx += size;
  amount_buf++;
}

/**
 * Set all data in buf to 0 and reset the buf pointer
 */
static void reset_buffer(void) {
  zero_buffer();
  buf_idx = 0;
  amount_buf = 0;
}

static void check_index(unsigned int next_index) {
  // Buffer is full? Call the supplied function
  if (buf_idx + next_index >= buf_len) {
    enabled = 0;
    callback(buf, buf_idx, amount_buf);
    reset_buffer();
    enabled = 1;
    // Reset buf. For security reasons, of course
  }
}

/**
 * Enable Cacheray logging
 */
static void cacheray_enable_log(void) {
  enabled = 1;
}

/**
 * Disable Cacheray logging
 */
static void cacheray_disable_log(void) {
  enabled = 0;
}

/**
 * Return the state of Cacheray logging
 * @return 1 if logging is enabled, 0 if disabled
 */
static int cacheray_is_enabled(void) {
  return enabled;
}

/**
 * Start Cacheray and enable logging
 * @param my_buffer the buf to use
 * @param my_buffer_size size of the buf
 * @param commit_func function to call when the buf is full
 * @return 0 on successful start
 */
int cacheray_start(void *my_buffer, unsigned long my_buffer_size,
                   cacheray_buf_cb commit_func) {
  callback = commit_func;
  buf = (char *)my_buffer;
  buf_idx = 0;
  buf_len = my_buffer_size;
  amount_buf = 0;

  enabled = 1;

  return 0; // Success
}

/**
 * Stop cacheray and disable logging
 * @return the amount of memory written to buf so far
 */
unsigned long cacheray_stop() {
  unsigned long temp_len = buf_idx;
  callback(buf, buf_idx, amount_buf);
  reset_buffer();
  enabled = 0;
  return temp_len;
}

/* Instrumentation API */
static void cacheray_init(void) {
}

/**
 * Log a single address event
 * @param type event type
 * @param addr address
 */
static void cacheray_log(cacheray_event_t type, void *addr,
                         unsigned char size) {
  // Return instantly if not turned on
  if (!enabled)
    return;

  cacheray_log_t tmp = {
      .type = type, .addr = addr, .size = size, .threadid = 0};
  unsigned int log_size = sizeof(cacheray_log_t);
  check_index(log_size);
  copy_to_buf(&tmp, log_size);
}

void cacheray_rtta_add(void *addr, const char *typename, unsigned elem_size,
                       unsigned elem_count) {
  if (!enabled)
    return;

  cacheray_rtta_add_t tmp = {.type = CACHERAY_EVENT_RTTA_ADD,
                             .threadid = 0,
                             .addr = addr,
                             .elem_size = elem_size,
                             .elem_count = elem_count};
  tmp.typename_len = string_copy(tmp.typename, typename);
  // Full struct - buffer size + typename length
  unsigned int log_size = sizeof(tmp) - sizeof(tmp.typename) + tmp.typename_len;
  check_index(log_size);
  copy_to_buf(&tmp, log_size);
}

void cacheray_rtta_remove(void *addr) {
  if (!enabled)
    return;

  cacheray_rtta_remove_t tmp = {
      .type = CACHERAY_EVENT_RTTA_REMOVE,
      .threadid = 0,
      .addr = addr,
  };
  check_index(sizeof(tmp));
  copy_to_buf(&tmp, sizeof(tmp));
}

#include "cacheray_instrum.inc"
