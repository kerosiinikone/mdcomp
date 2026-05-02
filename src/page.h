#ifndef FORMAT_T
#define FORMAT_T

#include <stdbool.h>
#include <stddef.h>

#include "arena.h"

#define PAGE_BUFFER_SIZE (50 * 1024)
#define PAGE_WIDTH 612
#define PAGE_HEIGHT 792

typedef struct {
  char *data;
  size_t capacity;
  size_t offset;
} Page_Context;

Page_Context page_buf_create(Arena *arena, size_t capacity);

bool page_buf_write(Page_Context *ctx, const char *fmt, ...);

void page_buf_reset(Page_Context *ctx);

#endif
