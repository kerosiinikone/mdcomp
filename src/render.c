#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "fonts.h"
#include "page.h"
#include "pdf.h"
#include "render.h"

#define IS_TEXT_NODE(type)                                                     \
  ((type) == NODE_HEADING || (type) == NODE_MEDIUM_HEADING ||                  \
   (type) == NODE_SMALL_HEADING || (type) == NODE_PARAGRAPH ||                 \
   (type) == NODE_LIST)

#define IS_PAGE_END(cursor) ((cursor) <= PAGE_MARGIN)

#define IS_PAGE_END_HOR(cursor) ((cursor) >= DRAW_AREA)

struct Render_Context {
  Page_Context **pages;
  size_t capacity;
  size_t length;

  Arena *arena;

  size_t global_cursor;
};

Render_Context *render_create(Arena *arena, size_t page_capacity) {
  Render_Context *render = arena_alloc(arena, sizeof(Render_Context));
  if (render == NULL)
    return NULL;
  render->arena = arena;
  render->global_cursor = RENDER_HEIGHT;
  render->capacity = page_capacity;
  render->pages = (Page_Context **)arena_alloc(arena, sizeof(Page_Context *) *
                                                          page_capacity);
  if (render->pages == NULL)
    return NULL;
  return render;
}

static bool document_append_page(Render_Context *r, Page_Context *page) {
  if (r->length >= r->capacity - 1)
    return false;
  r->pages[r->length++] = page;
  return true;
}

static Page_Context *create_new_page(Render_Context *r) {
  Page_Context *new_ctx = arena_alloc(r->arena, sizeof(Page_Context));
  if (new_ctx == NULL)
    return NULL;
  *new_ctx = page_buf_create(r->arena, PAGE_BUFFER_SIZE);
  if (new_ctx->data == NULL)
    return NULL;
  pdf_stream_write_start(new_ctx);
  if (!document_append_page(r, new_ctx))
    return NULL;
  r->global_cursor = RENDER_HEIGHT;
  return r->pages[r->length - 1];
}

static Page_Context *
create_page_if_space(Render_Context *r, Page_Context *curr_ctx, size_t offset) {
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
    return H1_FONT_SIZE;
  case NODE_MEDIUM_HEADING:
    return H2_FONT_SIZE;
  case NODE_SMALL_HEADING:
    return H3_FONT_SIZE;
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

static size_t get_horizontal_position(Node_Type type, size_t list_depth) {
  return node_cursor_start(type) + LIST_INDENT_STEP * list_depth;
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
  case STRING_BI:
    pdf_stream_change_font(ctx, FONT_BI, font_size);
    break;
  }
}

static bool handle_line_wrap(Render_Context *r, Page_Context **curr_ctx,
                             Text_Span *span, Node_Type type, int list_depth,
                             char *segment_start, size_t emit_length,
                             size_t *cursor, size_t *char_index,
                             size_t *last_space_offset,
                             char **new_segment_start) {
  size_t offset = node_offset_size(type);
  size_t font_size = node_font_size(type);

  if (!pdf_text_span_write(*curr_ctx, segment_start, emit_length))
    return false;

  *curr_ctx = create_page_if_space(r, *curr_ctx, offset);
  if (*curr_ctx == NULL)
    return false;

  size_t x_pos = get_horizontal_position(type, list_depth);
  pdf_stream_set_cursor(*curr_ctx, x_pos, r->global_cursor);

  set_font_for_span(*curr_ctx, span->type, font_size);

  *new_segment_start = segment_start + emit_length;
  if (*last_space_offset > 0)
    (*new_segment_start)++;

  *last_space_offset = 0;
  *char_index = 0;

  *cursor = x_pos;
  return true;
}

static bool render_node_spans(Render_Context *r, Page_Context **curr_ctx,
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
    if (n < 1) {
      return false;
    }

    if (isspace(*span_ptr)) {
      *last_space_offset = span_ptr - segment_start;
    }

    if (IS_PAGE_END_HOR(*cursor)) {
      bool split_by_space = *last_space_offset > 0;
      int emit_length = split_by_space ? *last_space_offset : char_index;

      if (!handle_line_wrap(r, curr_ctx, curr, type, list_depth, segment_start,
                            emit_length, cursor, &char_index, last_space_offset,
                            &segment_start))
        return false;
      span_ptr = segment_start;
    }

    int char_width = get_char_width((unsigned char)*span_ptr, curr->type);
    *cursor += (char_width * font_size) / 1000;

    span_ptr += n;
    char_index += n;
  }

  int rest = span_ptr - segment_start;
  if (rest > 0) {
    if (!pdf_text_span_write(*curr_ctx, segment_start, rest))
      return false;
  }
  *last_space_offset = 0;
  return true;
}

static bool render_text_spans(Render_Context *r, Page_Context **curr_ctx,
                              Node *node, size_t font_size) {
  Text_Span *curr = node->text;
  size_t cursor = get_horizontal_position(node->type, node->list_depth);
  size_t last_space_offset = 0;

  while (curr) {
    set_font_for_span(*curr_ctx, curr->type, font_size);
    if (!render_node_spans(r, curr_ctx, curr, node->type, &last_space_offset,
                           &cursor, node->list_depth))
      return false;
    curr = curr->next;
  }
  return true;
}

static bool render_heading_node(Render_Context *r, Page_Context **curr_ctx,
                                Node *node) {
  r->global_cursor -= BODY_OFFSET;
  pdf_stream_set_cursor(*curr_ctx,
                        PAGE_MARGIN + node->list_depth * LIST_INDENT_STEP,
                        r->global_cursor);

  if (!render_text_spans(r, curr_ctx, node, node_font_size(node->type)))
    return false;

  *curr_ctx = create_page_if_space(r, *curr_ctx, HEADER_OFFSET);
  return true;
}

static bool render_list_node(Render_Context *r, Page_Context **curr_ctx,
                             Node *node) {
  pdf_stream_set_cursor(*curr_ctx,
                        PAGE_MARGIN + node->list_depth * LIST_INDENT_STEP,
                        r->global_cursor);

  pdf_stream_change_font(*curr_ctx, FONT_REGULAR, BODY_FONT_SIZE);
  pdf_stream_write_list(*curr_ctx);

  size_t cursor = get_horizontal_position(node->type, node->list_depth);
  pdf_stream_set_cursor(*curr_ctx, cursor, r->global_cursor);

  if (!render_text_spans(r, curr_ctx, node, BODY_FONT_SIZE))
    return false;

  *curr_ctx = create_page_if_space(r, *curr_ctx, BODY_OFFSET);
  return true;
}

static bool render_paragraph_node(Render_Context *r, Page_Context **curr_ctx,
                                  Node *node) {
  pdf_stream_set_cursor(*curr_ctx,
                        PAGE_MARGIN + node->list_depth * LIST_INDENT_STEP,
                        r->global_cursor);

  if (!render_text_spans(r, curr_ctx, node, BODY_FONT_SIZE))
    return false;

  *curr_ctx = create_page_if_space(r, *curr_ctx, BODY_OFFSET);
  return true;
}

static bool render_node(Render_Context *r, Node *node) {
  if (node == NULL)
    return true;

  while (node) {
    Page_Context *curr_ctx = r->pages[r->length - 1];
    bool success = true;

    switch (node->type) {
    case NODE_HEADING:
    case NODE_MEDIUM_HEADING:
    case NODE_SMALL_HEADING:
      success = render_heading_node(r, &curr_ctx, node);
      break;
    case NODE_LIST:
      success = render_list_node(r, &curr_ctx, node);
      break;
    case NODE_PARAGRAPH:
      success = render_paragraph_node(r, &curr_ctx, node);
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
    if (!success)
      return false;
    if (node->child) {
      render_node(r, node->child);
    }
    node = node->next;
  }
  return true;
}

bool render_document(Render_Context *r, PDF_Context *pdf, Node *root) {
  Page_Context *initial_page = arena_alloc(r->arena, sizeof(Page_Context));
  if (initial_page == NULL)
    return false;
  *initial_page = page_buf_create(r->arena, PAGE_BUFFER_SIZE);
  pdf_stream_write_start(initial_page);
  if (!document_append_page(r, initial_page))
    return false;

  if (!render_node(r, root))
    return false;

  if (r->length == 0)
    return false;

  Page_Context *last_page = r->pages[r->length - 1];
  if (last_page->offset != last_page->capacity)
    pdf_stream_write_end(last_page);

  int pages_id = PDF_ROOT_ID + 1;

  PDF_Object cat = {
      .id = PDF_ROOT_ID, .type = PDF_CATALOG, .catalog = {pages_id}};
  int *tree_kids = (int *)arena_alloc(r->arena, sizeof(int) * r->length);
  if (tree_kids == NULL)
    return false;

  PDF_Object tree = {.id = pages_id, .type = PDF_TREE, .tree = {0, tree_kids}};
  PDF_Object font_reg = {
      .id = PDF_FONT_REGULAR_ID, .type = PDF_FONT, .font = FONT_REGULAR};
  PDF_Object font_bold = {
      .id = PDF_FONT_BOLD_ID, .type = PDF_FONT, .font = FONT_BOLD};
  PDF_Object font_italic = {
      .id = PDF_FONT_ITALIC_ID, .type = PDF_FONT, .font = FONT_ITALIC};
  PDF_Object font_bi = {
      .id = PDF_FONT_BI_ID, .type = PDF_FONT, .font = FONT_BI};

  pdf_tree_init_pages(&tree, r->length);

  pdf_obj_write(pdf, &cat);
  pdf_obj_write(pdf, &tree);
  pdf_obj_write(pdf, &font_reg);
  pdf_obj_write(pdf, &font_bold);
  pdf_obj_write(pdf, &font_italic);
  pdf_obj_write(pdf, &font_bi);

  pdf_write_render_pages(pdf, r->pages, r->length, tree.id, font_reg.id);

  pdf_xref_table_write(pdf);
  pdf_trailer_write(pdf);
  pdf_close(pdf);

  return true;
}
