#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// fix
#include "render.c"

#include "parser.h"
#include "arena.h"

#define DEFAULT_OUTPUT_PATH "./output.pdf"

#define HEADER_OFFSET 30
#define BODY_OFFSET 15
#define DRAW_AREA 612 - 72 * 2
#define PAGE_HEIGHT 750

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

typedef struct {
  char *data;
  size_t capacity;
  size_t offset;
} Buf_Context;

typedef struct {
  Buf_Context **data;
  size_t capacity;
  size_t length;

  size_t global_cursor;
} Page_Context;

Page_Context page_ctx_create(Arena *arena, size_t capacity) {
  return (Page_Context){
      .capacity = capacity,
      .length = 0,
      .global_cursor = PAGE_HEIGHT,
      .data = (Buf_Context **)arena_alloc(arena, capacity),
  };
}

// TODO: realloc / return false if fails?
void page_ctx_append(Page_Context *pca, Buf_Context *buf_ptr) {
  if (pca->length >= pca->capacity - 1)
    return;
  pca->data[pca->length++] = buf_ptr;
}

// TODO: realloc / return false if fails?
void buf_ctx_append(Buf_Context *ctx, char *fmt, ...) {
  if (ctx->offset >= ctx->capacity - 1)
    return;

  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(ctx->data + ctx->offset, ctx->capacity - ctx->offset, fmt,
                    args);
  va_end(args);

  if (n > 0) {
    ctx->offset += n;
    if (ctx->offset >= ctx->capacity) {
      ctx->offset = ctx->capacity - 1;
    }
  }
}

void buf_ctx_append_winansi(Buf_Context *ctx, const char *str, size_t length) {
  const char *ptr = str;
  const char *end = str + length;

  buf_ctx_append(ctx, "<");
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
      buf_ctx_append(ctx, "%02X", *ptr);
    } else if (char_len == 2) {
      uint8_t second_byte = (uint8_t)ptr[1];
      if (second_byte == 0xA4)
        buf_ctx_append(ctx, "E4");
      else if (second_byte == 0xB6)
        buf_ctx_append(ctx, "F6");
      else if (second_byte == 0xA5)
        buf_ctx_append(ctx, "E5");
      else if (second_byte == 0x84)
        buf_ctx_append(ctx, "C4");
      else if (second_byte == 0x96)
        buf_ctx_append(ctx, "D6");
      else if (second_byte == 0x85)
        buf_ctx_append(ctx, "C5");
      else
        buf_ctx_append(ctx, "3F");
    } else {
      buf_ctx_append(ctx, "3F");
    }
    ptr += char_len;
  }
  buf_ctx_append(ctx, "> Tj\n");
}

void temp_render_node(Arena *arena, Node *node, Page_Context *ctx);

void node_draw_spans(Arena *arena, Page_Context *ctx, Buf_Context **curr_ctx,
                     Text_Span *curr, Node_Type type, size_t *last_space_offset,
                     size_t *cursor, int list_depth);

size_t node_font_size(Node_Type type);
size_t node_offset_size(Node_Type type);
size_t node_cursor_start(Node_Type type);

int main(int argc, char *argv[]) {
  struct stat st;
  if (argc < 2 || argc > 2)
    return -1;

  int fd = open(argv[1], O_RDONLY);

  if (fd < 0)
    return -1;

  if (fstat(fd, &st) == -1) {
    close(fd);
    return -1;
  }
  size_t filesize = st.st_size;

  char *file_data = mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
  if (file_data == MAP_FAILED) {
    close(fd);
    return -1;
  }

  Arena arena = arena_create(ARENA_SIZE);

  Parser *parser = parser_create(&arena);
  Node *root = parser_parse(parser, file_data, filesize);

  Buf_Context ctx = {.data = arena_alloc(&arena, 50 * 1024),
                     .capacity = 50 * 1024,
                     .offset = 0};
  buf_ctx_append(&ctx, "BT\n");

  Page_Context p_ctx_arr = page_ctx_create(&arena, 1024 * 1024);
  page_ctx_append(&p_ctx_arr, &ctx);

  temp_render_node(&arena, root->child, &p_ctx_arr);

  Buf_Context *curr = p_ctx_arr.data[p_ctx_arr.length - 1];
  if (curr->offset != curr->capacity) {
    buf_ctx_append(curr, "ET");
  }

  // TODO: temp solution -> MAX_PAGES, check for overflowing
  int kids[50] = {0};

  PDF_Context pctx = {0};
  if (!pdf_init(&pctx, DEFAULT_OUTPUT_PATH)) {
    return -1;
  }

  PDF_Object cat = {.id = 1, .type = PDF_CATALOG, .catalog = {2}};
  PDF_Object tree = {.id = 2, .type = PDF_TREE, .tree = {0, kids}};
  PDF_Object font_reg = {.id = 3, .type = PDF_FONT, .font = 1};
  PDF_Object font_bold = {.id = 4, .type = PDF_FONT, .font = 2};
  PDF_Object font_italic = {.id = 5, .type = PDF_FONT, .font = 3};

  for (size_t page_id = 1; page_id < p_ctx_arr.length * 2; page_id += 2)
    tree.tree.kids[tree.tree.count++] = font_italic.id + page_id;

  pdf_obj_write(&pctx, &cat);
  pdf_obj_write(&pctx, &tree);
  pdf_obj_write(&pctx, &font_reg);
  pdf_obj_write(&pctx, &font_bold);
  pdf_obj_write(&pctx, &font_italic);

  size_t page_id = font_italic.id + 1;
  size_t content_id = font_italic.id + 2;

  for (size_t i = 0; i < p_ctx_arr.length; i++) {
    Buf_Context *curr_buf = p_ctx_arr.data[i];

    PDF_Object contents = {.id = content_id,
                           .type = PDF_CONTENT,
                           .content = {curr_buf->offset, curr_buf->data}};

    PDF_Object page = {
        .id = page_id,
        .type = PDF_PAGE,
        .page = {content_id, 612, 792, tree.id, font_reg.id},
    };

    pdf_obj_write(&pctx, &page);
    pdf_obj_write(&pctx, &contents);

    page_id += 2;
    content_id += 2;
  }

  pdf_xref_table_write(&pctx);
  pdf_trailer_write(&pctx, cat.id);
  fclose(pctx.f);

  arena_destroy(&arena);
  munmap(file_data, filesize);
  close(fd);
  return 0;
}

void temp_render_node(Arena *arena, Node *node, Page_Context *ctx) {
  if (node == NULL)
    return;

  while (node) {
    Buf_Context *curr_ctx = ctx->data[ctx->length - 1];

    if (node->type == NODE_HEADING || node->type == NODE_MEDIUM_HEADING ||
        node->type == NODE_SMALL_HEADING) {
      ctx->global_cursor -= BODY_OFFSET;
    }
    buf_ctx_append(curr_ctx, "1 0 0 1 %d %d Tm\n", 72 + node->list_depth * 10,
                   ctx->global_cursor);

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
          buf_ctx_append(curr_ctx, "/F1 24 Tf\n");
          break;
        case STRING_BOLD:
          buf_ctx_append(curr_ctx, "/F2 24 Tf\n");
          break;
        case STRING_ITALIC:
          buf_ctx_append(curr_ctx, "/F3 24 Tf\n");
          break;
        }
        node_draw_spans(arena, ctx, &curr_ctx, curr, node->type,
                        &last_space_offset, &cursor, 0);
        curr = curr->next;
      }

      ctx->global_cursor -= HEADER_OFFSET;

      if (ctx->global_cursor < 72) {
        buf_ctx_append(curr_ctx, "ET");

        Buf_Context *new_ctx = arena_alloc(arena, sizeof(Buf_Context));
        new_ctx->capacity = 50 * 1024;
        new_ctx->data = arena_alloc(arena, new_ctx->capacity);
        page_ctx_append(ctx, new_ctx);

        buf_ctx_append(new_ctx, "BT\n");
        curr_ctx = new_ctx;

        ctx->global_cursor = PAGE_HEIGHT;
      }
    } break;
    case NODE_LIST: {
      Text_Span *curr = node->text;

      size_t cursor = node_cursor_start(node->type) + node->list_depth * 10;
      size_t last_space_offset = 0;

      buf_ctx_append(curr_ctx, "/F1 12 Tf\n");
      buf_ctx_append(curr_ctx, "(-) Tj\n");
      buf_ctx_append(curr_ctx, "1 0 0 1 %lu %d Tm\n", cursor,
                     ctx->global_cursor);

      while (curr) {
        switch (curr->type) {
        case STRING_REGULAR:
          buf_ctx_append(curr_ctx, "/F1 12 Tf\n");
          break;
        case STRING_BOLD:
          buf_ctx_append(curr_ctx, "/F2 12 Tf\n");
          break;
        case STRING_ITALIC:
          buf_ctx_append(curr_ctx, "/F3 12 Tf\n");
          break;
        }

        node_draw_spans(arena, ctx, &curr_ctx, curr, node->type,
                        &last_space_offset, &cursor, node->list_depth);
        curr = curr->next;
      }
      ctx->global_cursor -= BODY_OFFSET;

      if (ctx->global_cursor < 72) {
        buf_ctx_append(curr_ctx, "ET");

        Buf_Context *new_ctx = arena_alloc(arena, sizeof(Buf_Context));
        new_ctx->capacity = 50 * 1024;
        new_ctx->data = arena_alloc(arena, new_ctx->capacity);
        buf_ctx_append(new_ctx, "BT\n");

        page_ctx_append(ctx, new_ctx);
        curr_ctx = new_ctx;
        ctx->global_cursor = PAGE_HEIGHT;
      }
    } break;
    case NODE_PARAGRAPH: {
      Text_Span *curr = node->text;

      size_t cursor = node_cursor_start(node->type);
      size_t last_space_offset = 0;

      while (curr) {
        switch (curr->type) {
        case STRING_REGULAR:
          buf_ctx_append(curr_ctx, "/F1 12 Tf\n");
          break;
        case STRING_BOLD:
          buf_ctx_append(curr_ctx, "/F2 12 Tf\n");
          break;
        case STRING_ITALIC:
          buf_ctx_append(curr_ctx, "/F3 12 Tf\n");
          break;
        }
        node_draw_spans(arena, ctx, &curr_ctx, curr, node->type,
                        &last_space_offset, &cursor, 0);
        curr = curr->next;
      }
      ctx->global_cursor -= BODY_OFFSET;

      if (ctx->global_cursor < 72) {
        buf_ctx_append(curr_ctx, "ET");

        Buf_Context *new_ctx = arena_alloc(arena, sizeof(Buf_Context));
        new_ctx->capacity = 50 * 1024;
        new_ctx->data = arena_alloc(arena, new_ctx->capacity);
        buf_ctx_append(new_ctx, "BT\n");

        page_ctx_append(ctx, new_ctx);
        curr_ctx = new_ctx;
        ctx->global_cursor = PAGE_HEIGHT;
      }
    } break;
    case NODE_BREAK: {
      buf_ctx_append(curr_ctx, "q\n1 w\n0 0 0 RG\n72 %lu m\n%lu %lu l\nS\nQ\n",
                     ctx->global_cursor, DRAW_AREA + 72, ctx->global_cursor);
      ctx->global_cursor -= BODY_OFFSET;
    } break;
    case NODE_BREAK_NO_LINE: {
      ctx->global_cursor -= BODY_OFFSET;
    } break;
    default:
      break;
    }
    if (node->child) {
      temp_render_node(arena, node->child, ctx);
    }
    node = node->next;
  }
}

void node_draw_spans(Arena *arena, Page_Context *ctx, Buf_Context **curr_ctx,
                     Text_Span *curr, Node_Type type, size_t *last_space_offset,
                     size_t *cursor, int list_depth) {
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
      buf_ctx_append_winansi(*curr_ctx, segment_start, emit_length);

      ctx->global_cursor -= offset;
      if (ctx->global_cursor < 72) {
        buf_ctx_append(*curr_ctx, "ET");

        Buf_Context *new_ctx = arena_alloc(arena, sizeof(Buf_Context));
        new_ctx->capacity = 50 * 1024;
        new_ctx->data = arena_alloc(arena, new_ctx->capacity);
        buf_ctx_append(new_ctx, "BT\n");

        page_ctx_append(ctx, new_ctx);
        *curr_ctx = new_ctx;
        ctx->global_cursor = PAGE_HEIGHT;
      }
      size_t x_pos = node_cursor_start(type) + 10 * list_depth;
      buf_ctx_append(*curr_ctx, "1 0 0 1 %lu %d Tm\n", x_pos,
                     ctx->global_cursor);

      switch (curr->type) {
      case STRING_REGULAR:
        buf_ctx_append(*curr_ctx, "/F1 %lu Tf\n", font_size);
        break;
      case STRING_BOLD:
        buf_ctx_append(*curr_ctx, "/F2 %lu Tf\n", font_size);
        break;
      case STRING_ITALIC:
        buf_ctx_append(*curr_ctx, "/F3 %lu Tf\n", font_size);
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
    buf_ctx_append_winansi(*curr_ctx, segment_start, rest);
  }
  *last_space_offset = 0;
}

size_t node_font_size(Node_Type type) {
  switch (type) {
  case NODE_HEADING:
  case NODE_MEDIUM_HEADING:
  case NODE_SMALL_HEADING:
    return 24;
  case NODE_LIST:
  case NODE_PARAGRAPH:
    return 12;
  default:
    return -1;
  }
}

size_t node_offset_size(Node_Type type) {
  switch (type) {
  case NODE_HEADING:
  case NODE_MEDIUM_HEADING:
  case NODE_SMALL_HEADING:
    return HEADER_OFFSET;
  case NODE_LIST:
  case NODE_PARAGRAPH:
    return BODY_OFFSET;
  default:
    return -1;
  }
}

size_t node_cursor_start(Node_Type type) {
  switch (type) {
  case NODE_LIST:
    return 88;
  default:
    return 72;
  }
}
