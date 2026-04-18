#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>
#include <stdint.h>

#define ALIGN_8(size) ((size) + 7) & ~7
#define ARENA_SIZE 10 * 1024 * 1024

typedef struct Arena {
  uint8_t *data;
  size_t cap;
  size_t offset;
} Arena;

Arena arena_create(size_t cap);
void *arena_alloc(Arena *arena, size_t size);
void arena_destroy(Arena *arena);

#endif
