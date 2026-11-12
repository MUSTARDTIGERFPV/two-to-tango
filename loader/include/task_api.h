// Interface between the Tango 2 monitor (flash, 0x0800C000) and RAM task images.
// A task is a position-dependent binary linked at TASK_AREA_START (see task.ld).
// It starts with task_header_t; the monitor zeroes .bss, then calls entry().
#pragma once
#include <stdint.h>

#define TASK_API_VERSION 1
#define TASK_MAGIC       0x314B5354u  // "TSK1"
#define TASK_AREA_START  0x2000C000u
#define TASK_AREA_END    0x20020000u
#define FAULT_RECORD     0x2000BF00u

typedef struct {
  uint32_t version;
  int (*printf)(const char *fmt, ...);   // to the host, over USB CDC
  void (*write)(const void *buf, uint32_t len);
  int (*getc)(void);                     // next byte from the host, -1 if none
  int (*abort)(void);                    // nonzero once the host sent Ctrl-C (0x03)
  uint32_t (*millis)(void);
  uint32_t (*micros)(void);
  void (*delay_ms)(uint32_t ms);
  void (*delay_us)(uint32_t us);
} task_api_t;

typedef int (*task_entry_t)(const task_api_t *api, const char *args);

typedef struct {
  uint32_t magic;
  task_entry_t entry;
  uint32_t bss_start;
  uint32_t bss_end;
  uint32_t image_end;
} task_header_t;
