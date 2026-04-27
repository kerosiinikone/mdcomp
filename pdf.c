#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "arena.h"
#include "format.h"
#include "parser.h"
#include "pdf.h"

static const char *FONT_NAMES[] = {[FONT_REGULAR] = "Helvetica",
                                   [FONT_BOLD] = "Helvetica-Bold",
                                   [FONT_ITALIC] = "Helvetica-Oblique",
                                   [FONT_BI] = "Helvetica-BoldOblique"};

struct PDF_Context {
  FILE *f;
  long offsets[MAX_OBJ_COUNT];
  size_t obj_count;
  size_t root;
};

static bool encode_utf8_char(Page_Context *ctx, const char *ptr,
                             size_t char_len) {
  if (char_len == 2) {
    uint8_t second_byte = (uint8_t)ptr[1];
    const char *hex_code = NULL;

    switch (second_byte) {
    case 0xA4:
      hex_code = "E4";
      break;
    case 0xB6:
      hex_code = "F6";
      break;
    case 0xA5:
      hex_code = "E5";
      break;
    case 0x84:
      hex_code = "C4";
      break;
    case 0x96:
      hex_code = "D6";
      break;
    case 0x85:
      hex_code = "C5";
      break;
    default:
      hex_code = "3F";
      break;
    }

    return page_buf_write(ctx, hex_code);
  } else if (char_len > 2) {
    return page_buf_write(ctx, "3F");
  }
  return false;
}

PDF_Context *pdf_create(Arena *arena) {
  return (PDF_Context *)arena_alloc(arena, sizeof(PDF_Context));
}

bool pdf_init(PDF_Context *ctx, const char *fp) {
  FILE *f = fopen(fp, "wb");
  if (f == NULL)
    return false;

  ctx->f = f;
  ctx->offsets[0] = 0;
  ctx->obj_count = 1;
  ctx->root = 1;

  fprintf(f, "%s", FORMAT_VERSION);
  return true;
}

bool pdf_text_span_write(Page_Context *ctx, const char *str, size_t length) {
  const char *ptr = str;
  const char *end = str + length;

  if (!page_buf_write(ctx, PDF_CMD_BEGIN_SHOW_TEXT))
    return false;

  while (ptr < end && ctx->offset < ctx->capacity - 1) {
    uint8_t char_len = utf8_char_length((unsigned char)*ptr);
    if (char_len < 1)
      return false;

    if (char_len == 1) {
      switch (*ptr) {
      case '(':
      case ')':
      case '\\':
        if (!page_buf_write(ctx, "\\"))
          return false;
        break;
      }
      if (!page_buf_write(ctx, "%02X", *ptr))
        return false;
    } else {
      if (!encode_utf8_char(ctx, ptr, char_len))
        return false;
    }
    ptr += char_len;
  }

  return page_buf_write(ctx, PDF_CMD_END_SHOW_TEXT);
}

void pdf_stream_write_end(Page_Context *ctx) {
  page_buf_write(ctx, PDF_CMD_END_TEXT);
}

void pdf_stream_write_start(Page_Context *ctx) {
  page_buf_write(ctx, PDF_CMD_BEGIN_TEXT);
}

void pdf_stream_set_cursor(Page_Context *ctx, size_t x, size_t y) {
  page_buf_write(ctx, "1 0 0 1 %lu %lu Tm\n", x, y);
}

void pdf_stream_change_font(Page_Context *ctx, PDF_Font_Style style,
                            size_t font_size) {
  switch (style) {
  case FONT_REGULAR: {
    page_buf_write(ctx, "/F1 %lu Tf\n", font_size);
    break;
  }
  case FONT_BOLD: {
    page_buf_write(ctx, "/F2 %lu Tf\n", font_size);
    break;
  }
  case FONT_ITALIC: {
    page_buf_write(ctx, "/F3 %lu Tf\n", font_size);
    break;
  }
  case FONT_BI: {
    page_buf_write(ctx, "/F4 %lu Tf\n", font_size);
    break;
  }
  }
}

void pdf_stream_write_list(Page_Context *ctx) {
  page_buf_write(ctx, PDF_CMD_LIST_BULLET);
}

void pdf_stream_write_breakline(Page_Context *ctx, size_t y, size_t x_end) {
  page_buf_write(ctx, "q\n1 w\n0 0 0 RG\n72 %lu m\n%lu %lu l\nS\nQ\n", y, x_end,
                 y);
}

void pdf_obj_start(PDF_Context *pctx, const PDF_Object *obj) {
  pctx->offsets[obj->id] = ftell(pctx->f);
  pctx->obj_count++;
  fprintf(pctx->f, "%d 0 obj\n", obj->id);
}

void pdf_obj_end(PDF_Context *pctx) { fprintf(pctx->f, "endobj\n\n"); }

void pdf_tree_init_pages(PDF_Object *tree, size_t first_page_id,
                         size_t pages_length) {
  for (size_t page_id = 1; page_id < pages_length * 2; page_id += 2) {
    if (tree->tree.count >= MAX_PAGES)
      break;
    tree->tree.kids[tree->tree.count++] = first_page_id + page_id - 1;
  }
}

static void write_catalog_obj(PDF_Context *pctx, const PDF_Object *obj) {
  fprintf(pctx->f, "<< /Pages %d 0 R /Type /Catalog >>\n",
          obj->catalog.pages_id);
}

static void write_tree_obj(PDF_Context *pctx, const PDF_Object *obj) {
  fprintf(pctx->f, "<< /Count %d /Kids [\n", obj->tree.count);

  const int *kid_id = obj->tree.kids;
  const int *last_id = kid_id + obj->tree.count;

  while (kid_id < last_id) {
    fprintf(pctx->f, "%d 0 R\n", *kid_id);
    kid_id++;
  }
  fprintf(pctx->f, "] /Type /Pages >>\n");
}

static void write_page_obj(PDF_Context *pctx, const PDF_Object *obj) {
  fprintf(pctx->f, "<<\n");
  fprintf(pctx->f, "	/Parent %d 0 R\n", obj->page.parent_id);
  fprintf(pctx->f, "	/Contents %d 0 R\n", obj->page.contents_id);
  fprintf(pctx->f, "	/Mediabox [0 0 %d %d]\n", obj->page.mb_x,
          obj->page.mb_y);
  // TODO: make dynamic
  fprintf(
      pctx->f,
      "	/Resources << /Font << /F1 %d 0 R /F2 %d 0 R /F3 %d 0 R /F4 %d 0 R >>"
      ">>\n",
      obj->page.font_id, obj->page.font_id + 1, obj->page.font_id + 2,
      obj->page.font_id + 3);
  fprintf(pctx->f, ">>\n");
  fprintf(pctx->f, "/Type /Page\n");
}

static void write_font_obj(PDF_Context *pctx, const PDF_Object *obj) {
  if (obj->font < FONT_REGULAR || obj->font > FONT_BI)
    return;

  fprintf(pctx->f,
          "<< /Type /Font /Subtype /Type1 /BaseFont /%s "
          "/Encoding /WinAnsiEncoding >>\n",
          FONT_NAMES[obj->font]);
}

static void write_content_obj(PDF_Context *pctx, const PDF_Object *obj) {
  fprintf(pctx->f, "<< /Length %zu >>\n", obj->content.length);
  fprintf(pctx->f, "stream\n");
  fprintf(pctx->f, "%s\n", obj->content.stream);
  fprintf(pctx->f, "endstream\n");
}

bool pdf_obj_write(PDF_Context *pctx, const PDF_Object *obj) {
  if (obj->id >= MAX_OBJ_COUNT)
    return false;

  pdf_obj_start(pctx, obj);

  switch (obj->type) {
  case PDF_CATALOG:
    write_catalog_obj(pctx, obj);
    break;
  case PDF_TREE:
    write_tree_obj(pctx, obj);
    break;
  case PDF_PAGE:
    write_page_obj(pctx, obj);
    break;
  case PDF_FONT:
    write_font_obj(pctx, obj);
    break;
  case PDF_CONTENT:
    write_content_obj(pctx, obj);
    break;
  default:
    pdf_obj_end(pctx);
    return false;
  }

  pdf_obj_end(pctx);
  return true;
}

void pdf_xref_table_write(PDF_Context *pctx) {
  fprintf(pctx->f, "xref\n");
  fprintf(pctx->f, "0 %zu\n", pctx->obj_count);
  fprintf(pctx->f, "0000000000 65535 f \n");
  for (size_t i = 1; i < pctx->obj_count; i++) {
    fprintf(pctx->f, "%010ld 00000 n \n", pctx->offsets[i]);
  }
}

void pdf_trailer_write(PDF_Context *pctx) {
  size_t startxref = ftell(pctx->f);
  fprintf(pctx->f, "trailer\n");
  fprintf(pctx->f, "<< /Size %zu /Root %zu 0 R >>\n", pctx->obj_count,
          pctx->root);
  fprintf(pctx->f, "startxref\n%ld\n%%%%EOF\n", startxref);
}

void pdf_close(PDF_Context *pctx) { fclose(pctx->f); }
