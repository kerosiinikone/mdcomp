#ifndef RENDER_H
#define RENDER_H

#include "arena.h"
#include "parser.h"
#include "pdf.h"

#define BODY_FONT_SIZE 12
#define H3_FONT_SIZE 16
#define H2_FONT_SIZE 20
#define H1_FONT_SIZE 26

#define HEADER_OFFSET 30
#define LIST_OFFSET 88
#define BODY_OFFSET 15

#define LIST_INDENT_STEP 10

#define PAGE_MARGIN 72
#define DRAW_AREA (PAGE_WIDTH - PAGE_MARGIN * 2)
#define RENDER_HEIGHT 750

typedef struct Render_Context Render_Context;

Render_Context *render_create(Arena *arena, size_t page_capacity);

bool render_document(Render_Context *r, PDF_Context *pdf, Node *root);

#endif
