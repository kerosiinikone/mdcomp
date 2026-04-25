#ifndef PDF_T
#define PDF_T

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "arena.h"
#include "format.h"

#define FORMAT_VERSION "%PDF-2.0\n%\xE2\xE3\xCF\xD3\n\n"
#define MAX_OBJ_COUNT 256
#define MAX_PAGES 50

typedef enum {
  PDF_CATALOG,
  PDF_TREE,
  PDF_PAGE,
  PDF_CONTENT,
  PDF_FONT
} PDF_Object_Type;

typedef enum { FONT_REGULAR = 1, FONT_BOLD, FONT_ITALIC } PDF_Font_Style;

typedef struct {
  int id;
  PDF_Object_Type type;

  union {
    struct {
      int pages_id;
    } catalog;
    struct {
      int count;
      int *kids;
    } tree;
    struct {
      int contents_id;
      int mb_x;
      int mb_y;
      int parent_id;
      int font_id;
    } page;
    PDF_Font_Style font;
    struct {
      size_t length;
      char *stream;
    } content;
  };
} PDF_Object;

typedef struct PDF_Context PDF_Context;

PDF_Context *pdf_create(Arena *arena);

bool pdf_init(PDF_Context *ctx, const char *fp);
void pdf_obj_write(PDF_Context *pctx, PDF_Object *obj);
void pdf_xref_table_write(PDF_Context *pctx);
void pdf_trailer_write(PDF_Context *pctx);
void pdf_close(PDF_Context *pctx);

void pdf_text_span_write(Page_Context *ctx, const char *str, size_t length);
void pdf_stream_write_end(Page_Context *ctx);
void pdf_stream_write_start(Page_Context *ctx);
void pdf_stream_set_cursor(Page_Context *ctx, size_t x, size_t y);
void pdf_stream_change_font(Page_Context *ctx, PDF_Font_Style style,
                            size_t font_size);
void pdf_stream_write_list(Page_Context *ctx);
void pdf_stream_write_breakline(Page_Context *ctx, size_t y, size_t x_end);

#endif
