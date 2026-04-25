#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "format.h"

bool page_buf_write(Page_Context *ctx, const char *fmt, ...) {
  if (ctx->offset >= ctx->capacity - 1)
    return false;

  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(ctx->data + ctx->offset, ctx->capacity - ctx->offset, fmt,
                    args);
  va_end(args);

  if (n > 0) {
    ctx->offset += n;
    if (ctx->offset >= ctx->capacity) {
      ctx->offset = ctx->capacity - 1;
      return false;
    }
    return true;
  }
  return false;
}

Page_Context page_buf_create(Arena *arena, size_t capacity) {
  return (Page_Context){
      .data = arena_alloc(arena, capacity), .capacity = capacity, .offset = 0};
}

void page_buf_reset(Page_Context *ctx) {
  ctx->offset = 0;
  if (ctx->data && ctx->capacity > 0) {
    memset(ctx->data, 0, ctx->capacity);
  }
}
