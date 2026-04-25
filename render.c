#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "fonts.h"
#include "format.h"
#include "pdf.h"
#include "render.h"

#define IS_TEXT_NODE(type)                                                     \
  ((type) == NODE_HEADING || (type) == NODE_MEDIUM_HEADING ||                  \
   (type) == NODE_SMALL_HEADING || (type) == NODE_PARAGRAPH ||                 \
   (type) == NODE_LIST)

#define IS_PAGE_END(cursor) ((cursor) < PAGE_MARGIN)

#define IS_PAGE_END_HOR(cursor) ((cursor) > DRAW_AREA)

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
  render->global_cursor = RENDER_HEIGHT;
  render->pages = (Page_Context **)arena_alloc(arena, page_capacity);
  return render;
}

static void document_append_page(Render_Context *r, Page_Context *page) {
  if (r->length >= r->capacity - 1)
    return;
  r->pages[r->length++] = page;
}

static Page_Context *create_new_page(Render_Context *r) {
  Page_Context new_ctx = page_buf_create(r->arena, PAGE_BUFFER_SIZE);
  pdf_stream_write_start(&new_ctx);
  document_append_page(r, &new_ctx);
  r->global_cursor = RENDER_HEIGHT;
  return r->pages[r->length - 1];
}

static Page_Context *create_page_with_space(Render_Context *r,
                                            Page_Context *curr_ctx,
                                            size_t offset) {
  r->global_cursor -= offset;
  if (IS_PAGE_END(r->global_cursor)) {
    pdf_stream_write_end(curr_ctx);
    return create_new_page(r);
  }
  return curr_ctx;
}

static size_t node_font_size(Node_Type type) {
  switch (type) {
  case NODE_HEADING:
  case NODE_MEDIUM_HEADING:
  case NODE_SMALL_HEADING:
    return HEADER_FONT_SIZE;
  default:
    return BODY_FONT_SIZE;
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
    return LIST_OFFSET;
  default:
    return PAGE_MARGIN;
  }
}

static size_t get_x_position(Node_Type type, size_t list_depth) {
  return node_cursor_start(type) + 10 * list_depth;
}

static void set_font_for_span(Page_Context *ctx, String_Type span_type,
                              size_t font_size) {
  switch (span_type) {
  case STRING_REGULAR:
    pdf_stream_change_font(ctx, FONT_REGULAR, font_size);
    break;
  case STRING_BOLD:
    pdf_stream_change_font(ctx, FONT_BOLD, font_size);
    break;
  case STRING_ITALIC:
    pdf_stream_change_font(ctx, FONT_ITALIC, font_size);
    break;
  }
}

static void handle_line_wrap(Render_Context *r, Page_Context **curr_ctx,
                             Text_Span *span, Node_Type type, int list_depth,
                             char *segment_start, size_t emit_length,
                             size_t *cursor, size_t *char_index,
                             size_t *last_space_offset,
                             char **new_segment_start) {
  size_t offset = node_offset_size(type);
  size_t font_size = node_font_size(type);

  pdf_text_span_write(*curr_ctx, segment_start, emit_length);

  *curr_ctx = create_page_with_space(r, *curr_ctx, offset);

  size_t x_pos = get_x_position(type, list_depth);
  pdf_stream_set_cursor(*curr_ctx, x_pos, r->global_cursor);

  set_font_for_span(*curr_ctx, span->type, font_size);

  *new_segment_start = segment_start + emit_length;
  if (*last_space_offset > 0)
    (*new_segment_start)++;

  *cursor = x_pos;
  *char_index = 0;
  *last_space_offset = 0;
}

static void render_node_spans(Render_Context *r, Page_Context **curr_ctx,
                              Text_Span *curr, Node_Type type,
                              size_t *last_space_offset, size_t *cursor,
                              size_t list_depth) {
  char *span_ptr = (char *)curr->view.start;
  char *end = span_ptr + curr->view.length;
  char *segment_start = span_ptr;
  size_t char_index = 0;

  size_t font_size = node_font_size(type);

  while (span_ptr < end) {
    size_t n = utf8_char_length((unsigned char)*span_ptr);

    if (isspace(*span_ptr)) {
      *last_space_offset = span_ptr - segment_start;
    }

    if (IS_PAGE_END_HOR(*cursor)) {
      int emit_length =
          *last_space_offset > 0 ? *last_space_offset : char_index;
      handle_line_wrap(r, curr_ctx, curr, type, list_depth, segment_start,
                       emit_length, cursor, &char_index, last_space_offset,
                       &segment_start);
    }

    int char_width = get_char_width((unsigned char)*span_ptr);
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

static void render_text_spans(Render_Context *r, Page_Context **curr_ctx,
                              Node *node, size_t font_size) {
  Text_Span *curr = node->text;
  size_t cursor = get_x_position(node->type, node->list_depth);
  size_t last_space_offset = 0;

  while (curr) {
    set_font_for_span(*curr_ctx, curr->type, font_size);
    render_node_spans(r, curr_ctx, curr, node->type, &last_space_offset,
                      &cursor, node->list_depth);
    curr = curr->next;
  }
}

static void render_heading_node(Render_Context *r, Page_Context **curr_ctx,
                                Node *node) {
  r->global_cursor -= BODY_OFFSET;
  pdf_stream_set_cursor(*curr_ctx, PAGE_MARGIN + node->list_depth * 10,
                        r->global_cursor);

  render_text_spans(r, curr_ctx, node, HEADER_FONT_SIZE);

  *curr_ctx = create_page_with_space(r, *curr_ctx, HEADER_OFFSET);
}

static void render_list_node(Render_Context *r, Page_Context **curr_ctx,
                             Node *node) {
  pdf_stream_set_cursor(*curr_ctx, PAGE_MARGIN + node->list_depth * 10,
                        r->global_cursor);

  pdf_stream_change_font(*curr_ctx, FONT_REGULAR, BODY_FONT_SIZE);
  pdf_stream_write_list(*curr_ctx);

  size_t cursor = get_x_position(node->type, node->list_depth);
  pdf_stream_set_cursor(*curr_ctx, cursor, r->global_cursor);

  render_text_spans(r, curr_ctx, node, BODY_FONT_SIZE);

  *curr_ctx = create_page_with_space(r, *curr_ctx, BODY_OFFSET);
}

static void render_paragraph_node(Render_Context *r, Page_Context **curr_ctx,
                                  Node *node) {
  pdf_stream_set_cursor(*curr_ctx, PAGE_MARGIN + node->list_depth * 10,
                        r->global_cursor);

  render_text_spans(r, curr_ctx, node, BODY_FONT_SIZE);

  *curr_ctx = create_page_with_space(r, *curr_ctx, BODY_OFFSET);
}

static void render_node(Render_Context *r, Node *node) {
  if (node == NULL)
    return;

  while (node) {
    Page_Context *curr_ctx = r->pages[r->length - 1];

    switch (node->type) {
    case NODE_HEADING:
    case NODE_MEDIUM_HEADING:
    case NODE_SMALL_HEADING:
      render_heading_node(r, &curr_ctx, node);
      break;
    case NODE_LIST:
      render_list_node(r, &curr_ctx, node);
      break;
    case NODE_PARAGRAPH:
      render_paragraph_node(r, &curr_ctx, node);
      break;
    case NODE_BREAK:
      pdf_stream_write_breakline(curr_ctx, r->global_cursor,
                                 DRAW_AREA + PAGE_MARGIN);
      r->global_cursor -= BODY_OFFSET;
      break;
    case NODE_BREAK_NO_LINE:
      r->global_cursor -= BODY_OFFSET;
      break;
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
  int page_ids[MAX_PAGES] = {0};

  Page_Context initial_page = page_buf_create(r->arena, PAGE_BUFFER_SIZE);
  pdf_stream_write_start(&initial_page);
  document_append_page(r, &initial_page);

  render_node(r, root);

  Page_Context *last_page = r->pages[r->length - 1];
  if (last_page->offset != last_page->capacity)
    pdf_stream_write_end(last_page);

  PDF_Object cat = {.id = 1, .type = PDF_CATALOG, .catalog = {2}};
  PDF_Object tree = {.id = 2, .type = PDF_TREE, .tree = {0, page_ids}};
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
        .page = {content_id, PAGE_WIDTH, PAGE_HEIGHT, tree.id, font_reg.id},
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
