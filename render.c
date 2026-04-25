#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "format.h"
#include "pdf.h"
#include "render.h"

static const int HELVETICA_WIDTHS[256] = {
    [32] = 278,   [33] = 278,   [34] = 355,   [35] = 556,   [36] = 556,
    [37] = 889,   [38] = 667,   [39] = 191,   [40] = 333,   [41] = 333,
    [42] = 389,   [43] = 584,   [44] = 278,   [45] = 333,   [46] = 278,
    [47] = 278,   [48] = 556,   [49] = 556,   [50] = 556,   [51] = 556,
    [52] = 556,   [53] = 556,   [54] = 556,   [55] = 556,   [56] = 556,
    [57] = 556,   [58] = 278,   [59] = 278,   [60] = 584,   [61] = 584,
    [62] = 584,   [63] = 556,   [64] = 1015,  [65] = 667,   [66] = 667,
    [67] = 722,   [68] = 722,   [69] = 667,   [70] = 611,   [71] = 778,
    [72] = 722,   [73] = 278,   [74] = 500,   [75] = 667,   [76] = 556,
    [77] = 833,   [78] = 722,   [79] = 778,   [80] = 667,   [81] = 778,
    [82] = 722,   [83] = 667,   [84] = 611,   [85] = 722,   [86] = 667,
    [87] = 944,   [88] = 667,   [89] = 667,   [90] = 611,   [91] = 278,
    [92] = 278,   [93] = 278,   [94] = 469,   [95] = 556,   [96] = 333,
    [97] = 556,   [98] = 556,   [99] = 500,   [100] = 556,  [101] = 556,
    [102] = 278,  [103] = 556,  [104] = 556,  [105] = 222,  [106] = 222,
    [107] = 500,  [108] = 222,  [109] = 833,  [110] = 556,  [111] = 556,
    [112] = 556,  [113] = 556,  [114] = 333,  [115] = 500,  [116] = 278,
    [117] = 556,  [118] = 500,  [119] = 722,  [120] = 500,  [121] = 500,
    [122] = 500,  [123] = 334,  [124] = 260,  [125] = 334,  [126] = 584,
    [0xC4] = 667, [0xC5] = 667, [0xD6] = 778, [0xE4] = 556, [0xE5] = 556,
    [0xF6] = 556,
};

struct Render_Context {
  Page_Context **pages;
  size_t capacity;
  size_t length;

  Arena *arena;

  size_t global_cursor;
};

Render_Context *render_create(Arena *arena, size_t page_capacity) {
  Render_Context *render = arena_alloc(arena, sizeof(Render_Context));
  render->arena = arena;
  render->global_cursor = PAGE_HEIGHT;
  render->pages = (Page_Context **)arena_alloc(arena, page_capacity);
  return render;
}

static void document_append_page(Render_Context *r, Page_Context *page) {
  if (r->length >= r->capacity - 1)
    return;
  r->pages[r->length++] = page;
}

static size_t node_font_size(Node_Type type) {
  switch (type) {
  case NODE_HEADING:
  case NODE_MEDIUM_HEADING:
  case NODE_SMALL_HEADING:
    return 24;
  default:
    return 12;
  }
}

static size_t node_offset_size(Node_Type type) {
  switch (type) {
  case NODE_HEADING:
  case NODE_MEDIUM_HEADING:
  case NODE_SMALL_HEADING:
    return HEADER_OFFSET;
  default:
    return BODY_OFFSET;
  }
}

static size_t node_cursor_start(Node_Type type) {
  switch (type) {
  case NODE_LIST:
    return 88;
  default:
    return 72;
  }
}

static void render_node_spans(Render_Context *r, Page_Context **curr_ctx,
                              Text_Span *curr, Node_Type type,
                              size_t *last_space_offset, size_t *cursor,
                              int list_depth) {
  char *span_ptr = (char *)curr->view.start;
  char *end = span_ptr + curr->view.length;
  char *segment_start = span_ptr;
  size_t char_index = 0;

  size_t offset = node_offset_size(type);
  size_t font_size = node_font_size(type);

  while (span_ptr < end) {
    size_t n = utf8_char_length((unsigned char)*span_ptr);

    if (isspace(*span_ptr)) {
      *last_space_offset = span_ptr - segment_start;
    }
    if (*cursor > DRAW_AREA) {
      int emit_length =
          *last_space_offset > 0 ? *last_space_offset : char_index;
      pdf_text_span_write(*curr_ctx, segment_start, emit_length);

      r->global_cursor -= offset;
      if (r->global_cursor < 72) {
        pdf_stream_write_end(*curr_ctx);

        Page_Context new_ctx = page_buf_create(r->arena, 50 * 1024);
        pdf_stream_write_start(&new_ctx);

        document_append_page(r, &new_ctx);
        *curr_ctx = &new_ctx;
        r->global_cursor = PAGE_HEIGHT;
      }
      size_t x_pos = node_cursor_start(type) + 10 * list_depth;
      pdf_stream_set_cursor(*curr_ctx, x_pos, r->global_cursor);

      switch (curr->type) {
      case STRING_REGULAR:
        pdf_stream_change_font(*curr_ctx, FONT_REGULAR, font_size);
        break;
      case STRING_BOLD:
        pdf_stream_change_font(*curr_ctx, FONT_BOLD, font_size);
        break;
      case STRING_ITALIC:
        pdf_stream_change_font(*curr_ctx, FONT_ITALIC, font_size);
        break;
      }
      segment_start += emit_length;
      if (*last_space_offset > 0)
        segment_start++;

      *cursor = x_pos;
      char_index = 0;
      *last_space_offset = 0;
    }

    int char_width = 556;
    if (n == 1 && *span_ptr >= 32 && *span_ptr <= 126) {
      char_width = HELVETICA_WIDTHS[(unsigned char)*span_ptr];
    } else {
      char_width = HELVETICA_WIDTHS[(unsigned char)*span_ptr];
      if (char_width == 0)
        char_width = 556;
    }
    *cursor += (char_width * font_size) / 1000;

    span_ptr += n;
    char_index += n;
  }
  int rest = span_ptr - segment_start;
  if (rest > 0) {
    pdf_text_span_write(*curr_ctx, segment_start, rest);
  }
  *last_space_offset = 0;
}

static void render_node(Render_Context *r, Node *node) {
  if (node == NULL)
    return;

  while (node) {
    Page_Context *curr_ctx = r->pages[r->length - 1];

    if (node->type == NODE_HEADING || node->type == NODE_MEDIUM_HEADING ||
        node->type == NODE_SMALL_HEADING) {
      r->global_cursor -= BODY_OFFSET;
    }
    pdf_stream_set_cursor(curr_ctx, 72 + node->list_depth * 10,
                          r->global_cursor);

    switch (node->type) {
    case NODE_HEADING:
    case NODE_MEDIUM_HEADING:
    case NODE_SMALL_HEADING: {
      Text_Span *curr = node->text;

      size_t cursor = node_cursor_start(node->type);
      size_t last_space_offset = 0;

      while (curr) {
        switch (curr->type) {
        case STRING_REGULAR:
          pdf_stream_change_font(curr_ctx, FONT_REGULAR, 24);
          break;
        case STRING_BOLD:
          pdf_stream_change_font(curr_ctx, FONT_BOLD, 24);
          break;
        case STRING_ITALIC:
          pdf_stream_change_font(curr_ctx, FONT_ITALIC, 24);
          break;
        }
        render_node_spans(r, &curr_ctx, curr, node->type, &last_space_offset,
                          &cursor, 0);
        curr = curr->next;
      }

      r->global_cursor -= HEADER_OFFSET;

      if (r->global_cursor < 72) {
        pdf_stream_write_end(curr_ctx);

        Page_Context new_ctx = page_buf_create(r->arena, 50 * 1024);
        pdf_stream_write_start(&new_ctx);

        document_append_page(r, &new_ctx);
        curr_ctx = &new_ctx;

        r->global_cursor = PAGE_HEIGHT;
      }
    } break;
    case NODE_LIST: {
      Text_Span *curr = node->text;

      size_t cursor = node_cursor_start(node->type) + node->list_depth * 10;
      size_t last_space_offset = 0;

      pdf_stream_change_font(curr_ctx, FONT_REGULAR, 12);
      pdf_stream_write_list(curr_ctx);
      pdf_stream_set_cursor(curr_ctx, cursor, r->global_cursor);

      while (curr) {
        switch (curr->type) {
        case STRING_REGULAR:
          pdf_stream_change_font(curr_ctx, FONT_REGULAR, 12);
          break;
        case STRING_BOLD:
          pdf_stream_change_font(curr_ctx, FONT_BOLD, 12);
          break;
        case STRING_ITALIC:
          pdf_stream_change_font(curr_ctx, FONT_ITALIC, 12);
          break;
        }

        render_node_spans(r, &curr_ctx, curr, node->type, &last_space_offset,
                          &cursor, node->list_depth);
        curr = curr->next;
      }
      r->global_cursor -= BODY_OFFSET;

      if (r->global_cursor < 72) {
        pdf_stream_write_end(curr_ctx);

        Page_Context new_ctx = page_buf_create(r->arena, 50 * 1024);
        pdf_stream_write_start(&new_ctx);

        document_append_page(r, &new_ctx);
        curr_ctx = &new_ctx;
        r->global_cursor = PAGE_HEIGHT;
      }
    } break;
    case NODE_PARAGRAPH: {
      Text_Span *curr = node->text;

      size_t cursor = node_cursor_start(node->type);
      size_t last_space_offset = 0;

      while (curr) {
        switch (curr->type) {
        case STRING_REGULAR:
          pdf_stream_change_font(curr_ctx, FONT_REGULAR, 12);
          break;
        case STRING_BOLD:
          pdf_stream_change_font(curr_ctx, FONT_BOLD, 12);
          break;
        case STRING_ITALIC:
          pdf_stream_change_font(curr_ctx, FONT_ITALIC, 12);
          break;
        }
        render_node_spans(r, &curr_ctx, curr, node->type, &last_space_offset,
                          &cursor, 0);
        curr = curr->next;
      }
      r->global_cursor -= BODY_OFFSET;

      if (r->global_cursor < 72) {
        pdf_stream_write_end(curr_ctx);

        Page_Context new_ctx = page_buf_create(r->arena, 50 * 1024);
        pdf_stream_write_start(&new_ctx);

        document_append_page(r, &new_ctx);
        curr_ctx = &new_ctx;
        r->global_cursor = PAGE_HEIGHT;
      }
    } break;
    case NODE_BREAK: {
      pdf_stream_write_breakline(curr_ctx, r->global_cursor, DRAW_AREA + 72);
      r->global_cursor -= BODY_OFFSET;
    } break;
    case NODE_BREAK_NO_LINE: {
      r->global_cursor -= BODY_OFFSET;
    } break;
    default:
      break;
    }
    if (node->child) {
      render_node(r, node->child);
    }
    node = node->next;
  }
}

void render_document(Render_Context *r, PDF_Context *pdf, Node *root) {
  Page_Context initial_page = page_buf_create(r->arena, 1024 * 1024);
  pdf_stream_write_start(&initial_page);
  document_append_page(r, &initial_page);

  render_node(r, root);

  Page_Context *last_page = r->pages[r->length - 1];
  if (last_page->offset != last_page->capacity)
    pdf_stream_write_end(last_page);

  int kids[MAX_PAGES] = {0};

  PDF_Object cat = {.id = 1, .type = PDF_CATALOG, .catalog = {2}};
  PDF_Object tree = {.id = 2, .type = PDF_TREE, .tree = {0, kids}};
  PDF_Object font_reg = {.id = 3, .type = PDF_FONT, .font = 1};
  PDF_Object font_bold = {.id = 4, .type = PDF_FONT, .font = 2};
  PDF_Object font_italic = {.id = 5, .type = PDF_FONT, .font = 3};

  for (size_t page_id = 1; page_id < r->length * 2; page_id += 2)
    tree.tree.kids[tree.tree.count++] = font_italic.id + page_id;

  pdf_obj_write(pdf, &cat);
  pdf_obj_write(pdf, &tree);
  pdf_obj_write(pdf, &font_reg);
  pdf_obj_write(pdf, &font_bold);
  pdf_obj_write(pdf, &font_italic);

  size_t page_id = font_italic.id + 1;
  size_t content_id = font_italic.id + 2;

  for (size_t i = 0; i < r->length; i++) {
    Page_Context *curr_buf = r->pages[i];

    PDF_Object contents = {.id = content_id,
                           .type = PDF_CONTENT,
                           .content = {curr_buf->offset, curr_buf->data}};
    PDF_Object page = {
        .id = page_id,
        .type = PDF_PAGE,
        .page = {content_id, 612, 792, tree.id, font_reg.id},
    };

    pdf_obj_write(pdf, &page);
    pdf_obj_write(pdf, &contents);

    page_id += 2;
    content_id += 2;
  }

  pdf_xref_table_write(pdf);
  pdf_trailer_write(pdf);
  pdf_close(pdf);
}
