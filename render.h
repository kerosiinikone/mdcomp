#ifndef RENDER_H
#define RENDER_H

#include "arena.h"
#include "parser.h"
#include "pdf.h"

#define HEADER_OFFSET 30
#define BODY_FONT_SIZE 12
#define HEADER_FONT_SIZE 24
#define LIST_OFFSET 88
#define LIST_INDENT_STEP 10
#define BODY_OFFSET 15
#define PAGE_MARGIN 72
#define PAGE_WIDTH 612
#define PAGE_HEIGHT 792
#define DRAW_AREA (PAGE_WIDTH - PAGE_MARGIN * 2)
#define RENDER_HEIGHT 750

#define PDF_FONT_REGULAR_ID 3
#define PDF_FONT_BOLD_ID 4
#define PDF_FONT_ITALIC_ID 5
#define PDF_FIRST_PAGE_ID 6

typedef struct Render_Context Render_Context;

Render_Context *render_create(Arena *arena, size_t page_capacity);

bool render_document(Render_Context *r, PDF_Context *pdf, Node *root);

#endif
