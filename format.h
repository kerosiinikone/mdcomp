#ifndef FORMAT_T
#define FORMAT_T

#include <stddef.h>

#include "arena.h"

typedef struct {
  char *data;
  size_t capacity;
  size_t offset;
} Page_Context;

Page_Context page_buf_create(Arena *arena, size_t page_capacity);

void page_buf_write(Page_Context *ctx, char *fmt, ...);

#endif
