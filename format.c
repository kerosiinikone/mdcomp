#include <stdarg.h>
#include <stdio.h>

#include "format.h"

void page_buf_write(Page_Context *ctx, char *fmt, ...) {
  if (ctx->offset >= ctx->capacity - 1)
    return;

  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(ctx->data + ctx->offset, ctx->capacity - ctx->offset, fmt,
                    args);
  va_end(args);

  if (n > 0) {
    ctx->offset += n;
    if (ctx->offset >= ctx->capacity) {
      ctx->offset = ctx->capacity - 1;
    }
  }
}

Page_Context page_buf_create(Arena *arena, size_t capacity) {
  return (Page_Context){
      .data = arena_alloc(arena, 50 * 1024), .capacity = capacity, .offset = 0};
}
