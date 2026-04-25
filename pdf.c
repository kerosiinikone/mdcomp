#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "arena.h"
#include "format.h"
#include "parser.h"
#include "pdf.h"

struct PDF_Context {
  FILE *f;
  long offsets[MAX_OBJ_COUNT];
  size_t obj_count;
  size_t root;
};

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

void pdf_text_span_write(Page_Context *ctx, const char *str, size_t length) {
  const char *ptr = str;
  const char *end = str + length;

  page_buf_write(ctx, "<");
  while (ptr < end && ctx->offset < ctx->capacity - 1) {
    uint8_t char_len = utf8_char_length((unsigned char)*ptr);
    if (char_len == 1) {
      switch (*ptr) {
      case '(':
      case ')':
      case '\\': {
        ctx->data[ctx->offset++] = '\\';
      }
      }
      page_buf_write(ctx, "%02X", *ptr);
    } else if (char_len == 2) {
      uint8_t second_byte = (uint8_t)ptr[1];
      if (second_byte == 0xA4)
        page_buf_write(ctx, "E4");
      else if (second_byte == 0xB6)
        page_buf_write(ctx, "F6");
      else if (second_byte == 0xA5)
        page_buf_write(ctx, "E5");
      else if (second_byte == 0x84)
        page_buf_write(ctx, "C4");
      else if (second_byte == 0x96)
        page_buf_write(ctx, "D6");
      else if (second_byte == 0x85)
        page_buf_write(ctx, "C5");
      else
        page_buf_write(ctx, "3F");
    } else {
      page_buf_write(ctx, "3F");
    }
    ptr += char_len;
  }
  page_buf_write(ctx, "> Tj\n");
}

void pdf_stream_write_end(Page_Context *ctx) { page_buf_write(ctx, "ET"); }
void pdf_stream_write_start(Page_Context *ctx) { page_buf_write(ctx, "BT\n"); }

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
  }
}

void pdf_stream_write_list(Page_Context *ctx) {
  page_buf_write(ctx, "(-) Tj\n");
}

void pdf_stream_write_breakline(Page_Context *ctx, size_t y, size_t x_end) {
  page_buf_write(ctx, "q\n1 w\n0 0 0 RG\n72 %lu m\n%lu %lu l\nS\nQ\n", y, x_end,
                 y);
}

void pdf_obj_start(PDF_Context *pctx, PDF_Object *obj) {
  pctx->offsets[obj->id] = ftell(pctx->f);
  pctx->obj_count++;
  fprintf(pctx->f, "%d 0 obj\n", obj->id);
}

void pdf_obj_end(PDF_Context *pctx) { fprintf(pctx->f, "endobj\n\n"); }

void pdf_obj_write(PDF_Context *pctx, PDF_Object *obj) {
  pdf_obj_start(pctx, obj);
  switch (obj->type) {
  case PDF_CATALOG: {
    fprintf(pctx->f, "<< /Pages %d 0 R /Type /Catalog >>\n",
            obj->catalog.pages_id);
  } break;
  case PDF_TREE: {
    fprintf(pctx->f, "<< /Count %d /Kids [\n", obj->tree.count);

    int *kid_id = obj->tree.kids;
    int *last_id = kid_id + obj->tree.count;

    while (kid_id < last_id) {
      fprintf(pctx->f, "%d 0 R\n", *kid_id);
      kid_id++;
    }
    fprintf(pctx->f, "] /Type /Pages >>\n");
  } break;
  case PDF_PAGE: {
    fprintf(pctx->f, "<<\n");
    fprintf(pctx->f, "	/Parent %d 0 R\n", obj->page.parent_id);
    fprintf(pctx->f, "	/Contents %d 0 R\n", obj->page.contents_id);
    fprintf(pctx->f, "	/Mediabox [0 0 %d %d]\n", obj->page.mb_x,
            obj->page.mb_y);
    fprintf(pctx->f,
            "	/Resources << /Font << /F1 %d 0 R /F2 %d 0 R /F3 %d 0 R >> "
            ">>\n",
            obj->page.font_id, obj->page.font_id + 1, obj->page.font_id + 2);
    fprintf(pctx->f, ">>\n");
    fprintf(pctx->f, "/Type /Page\n");
  } break;
  case PDF_FONT: {
    switch (obj->font) {
    case FONT_REGULAR: {
      fprintf(pctx->f, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                       "/Encoding /WinAnsiEncoding >>\n");
    } break;
    case FONT_BOLD: {
      fprintf(pctx->f, "<< /Type /Font /Subtype /Type1 /BaseFont "
                       "/Helvetica-Bold /Encoding /WinAnsiEncoding >>\n");
    } break;
    case FONT_ITALIC: {
      fprintf(pctx->f, "<< /Type /Font /Subtype /Type1 /BaseFont "
                       "/Helvetica-Oblique /Encoding /WinAnsiEncoding >>\n");
    } break;
    }
  } break;
  case PDF_CONTENT: {
    fprintf(pctx->f, "<< /Length %zu >>\n", obj->content.length);
    fprintf(pctx->f, "stream\n");
    fprintf(pctx->f, "%s\n", obj->content.stream);
    fprintf(pctx->f, "endstream\n");
  } break;
  default:
    break;
  }
  pdf_obj_end(pctx);
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
