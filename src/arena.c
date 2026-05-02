#include "arena.h"

#include <stdlib.h>
#include <string.h>

Arena arena_create(size_t cap) {
  return (Arena){.data = malloc(cap), .cap = cap, .offset = 0};
}

void *arena_alloc(Arena *arena, size_t size) {
  size_t align = ALIGN_8(size);

  if (arena->offset + align > arena->cap) {
    return NULL;
  };

  void *ptr = arena->data + arena->offset;
  arena->offset += align;
  memset(ptr, 0, align);
  return ptr;
}

void arena_destroy(Arena *arena) { free(arena->data); }
