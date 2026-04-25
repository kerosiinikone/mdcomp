#ifndef RENDER_H
#define RENDER_H

#include "arena.h"
#include "format.h"
#include "parser.h"
#include "pdf.h"

#define HEADER_OFFSET 30
#define BODY_OFFSET 15
#define DRAW_AREA 612 - 72 * 2
#define PAGE_HEIGHT 750

typedef struct Render_Context Render_Context;

Render_Context *render_create(Arena *arena, size_t page_capacity);

void render_document(Render_Context *r, PDF_Context *pdf, Node *root);

#endif
