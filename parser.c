#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>

#include "parser.h"

#define PARSER_CHECK_BOUNDS(p, n) ((p)->ptr + (n) < (p)->end)

struct Parser_Context {
  Arena *arena;
  Node *root;
  Node *curr_node;
  Node *last_root_child;

  size_t indent_level;
  int stack_top;

  Node *list_stack[MAX_LIST_COUNT];
  size_t list_depths[MAX_LIST_COUNT];

  bool is_newline;

  const char *ptr;
  const char *end;

  Text_Span *curr_span;
  String_Type curr_fmt;
};

int utf8_char_length(unsigned char leading_byte) {
  if ((leading_byte & 0x80) == 0x00)
    return 1;
  if ((leading_byte & 0xE0) == 0xC0)
    return 2;
  if ((leading_byte & 0xF0) == 0xE0)
    return 3;
  if ((leading_byte & 0xF8) == 0xF0)
    return 4;
  return -1;
}

static Text_Span *create_text_span(Parser_Context *p, String_Type type) {
  Text_Span *span = arena_alloc(p->arena, sizeof(Text_Span));
  if (span == NULL)
    return NULL;
  span->type = type;
  return span;
}

static Node *create_node(Parser_Context *p, Node_Type type) {
  Node *node = arena_alloc(p->arena, sizeof(Node));
  if (node == NULL)
    return NULL;
  node->type = type;
  return node;
}

static bool attach_as_child(Parser_Context *p, Node *node) {
  p->curr_node->child = node;
  p->last_root_child = node;
  p->curr_node = node;
  return true;
}

static bool attach_as_sibling(Parser_Context *p, Node *node) {
  p->curr_node->next = node;
  p->last_root_child = node;
  p->curr_node = node;
  return true;
}

static bool attach_as_root_sibling(Parser_Context *p, Node *node) {
  if (p->last_root_child == NULL) {
    p->root->child = node;
    p->last_root_child = node;
  } else {
    p->last_root_child->next = node;
    p->last_root_child = node;
  }
  p->curr_node = node;
  return true;
}

static bool is_breakline(Parser_Context *p) {
  return PARSER_CHECK_BOUNDS(p, 2) && *(p->ptr) == '_' && p->ptr[1] == '_' &&
         p->ptr[2] == '_';
}

static bool handle_newline(Parser_Context *p) {
  if (PARSER_CHECK_BOUNDS(p, 1) && p->ptr[1] == '\n') {
    Node *br_nl = create_node(p, NODE_BREAK_NO_LINE);
    if (br_nl == NULL)
      return false;
    p->curr_node->next = br_nl;
    p->curr_node = br_nl;
    p->ptr++;
  }
  p->is_newline = true;
  while (isspace((unsigned char)*(p->ptr))) {
    p->ptr++;
    p->indent_level++;
  }
  p->ptr--;
  return true;
}

static bool handle_default(Parser_Context *p, size_t n) {
  if (p->is_newline) {
    Node *par = create_node(p, NODE_PARAGRAPH);
    if (par == NULL)
      return false;

    Text_Span *span = create_text_span(p, STRING_REGULAR);
    if (span == NULL)
      return false;
    par->text = span;
    p->curr_span = span;
    p->curr_fmt = STRING_REGULAR;

    if (p->curr_node->type == NODE_PARAGRAPH ||
        p->curr_node->type == NODE_BREAK || p->curr_node->type == NODE_LIST ||
        p->curr_node->type == NODE_BREAK_NO_LINE) {
      p->curr_node->next = par;
    } else {
      p->curr_node->child = par;
    }
    p->curr_node = par;
    p->is_newline = false;
  }

  if (p->curr_span) {
    if (p->curr_span->view.start == NULL) {
      p->curr_span->view.start = p->ptr;
    }
    p->curr_span->view.length += n;
  }
  return true;
}

static bool handle_heading(Parser_Context *p, size_t n) {
  if (!PARSER_CHECK_BOUNDS(p, 1))
    return true;

  if (!p->is_newline)
    return handle_default(p, n);

  size_t hash_count = 1;
  while (hash_count < MAX_HASH_COUNT && PARSER_CHECK_BOUNDS(p, hash_count) &&
         p->ptr[hash_count] == '#') {
    hash_count++;
  }

  if (!PARSER_CHECK_BOUNDS(p, hash_count) ||
      !isspace((unsigned char)p->ptr[hash_count])) {
    return handle_default(p, n);
  }

  Node_Type heading_type = hash_count % MAX_HASH_COUNT == 0 ? 1 : hash_count;
  Node *heading = create_node(p, heading_type);
  if (heading == NULL)
    return false;

  Text_Span *span = create_text_span(p, STRING_REGULAR);
  if (span == NULL)
    return false;
  heading->text = span;
  p->curr_span = span;
  p->curr_fmt = STRING_REGULAR;

  bool should_be_child = false;
  bool should_be_sibling = false;

  switch (p->curr_node->type) {
  case NODE_ROOT:
    should_be_child = true;
    break;
  case NODE_HEADING:
    should_be_child = (heading_type == NODE_MEDIUM_HEADING ||
                       heading_type == NODE_SMALL_HEADING);
    should_be_sibling = (heading_type == NODE_HEADING);
    break;
  case NODE_MEDIUM_HEADING:
    should_be_child = (heading_type == NODE_SMALL_HEADING);
    should_be_sibling =
        (heading_type == NODE_HEADING || heading_type == NODE_MEDIUM_HEADING);
    break;
  case NODE_SMALL_HEADING:
    should_be_sibling = true;
    break;
  case NODE_BREAK:
  case NODE_BREAK_NO_LINE:
    should_be_sibling = true;
    break;
  default:
    break;
  }

  if (should_be_child) {
    attach_as_child(p, heading);
  } else if (should_be_sibling) {
    attach_as_sibling(p, heading);
  } else {
    attach_as_root_sibling(p, heading);
  }

  p->ptr += hash_count;
  p->is_newline = false;
  return true;
}

static bool handle_list(Parser_Context *p, size_t n) {
  if (!PARSER_CHECK_BOUNDS(p, 1))
    return true;

  if (!p->is_newline || !isspace((unsigned char)p->ptr[1])) {
    return handle_default(p, n);
  }

  Node *list = create_node(p, NODE_LIST);
  if (list == NULL)
    return false;

  Text_Span *span = create_text_span(p, STRING_REGULAR);
  if (span == NULL)
    return false;
  p->curr_fmt = STRING_REGULAR;
  list->text = span;
  p->curr_span = span;

  if (p->curr_node->type == NODE_ROOT || p->curr_node->type == NODE_HEADING ||
      p->curr_node->type == NODE_MEDIUM_HEADING ||
      p->curr_node->type == NODE_SMALL_HEADING) {
    p->stack_top = 0;
    p->list_stack[p->stack_top] = list;
    p->list_depths[p->stack_top] = p->indent_level;

    list->list_depth = p->stack_top;
    p->curr_node->child = list;
  } else if (p->curr_node->type == NODE_LIST) {
    size_t prev_indent = p->list_depths[p->stack_top];

    if (p->indent_level > prev_indent) {
      if (p->stack_top >= MAX_LIST_COUNT - 1)
        return false;
      p->list_depths[++p->stack_top] = p->indent_level;
      list->list_depth = p->stack_top;
      p->list_stack[p->stack_top] = list;
      p->curr_node->child = list;
    } else if (p->indent_level < prev_indent) {
      while (p->stack_top > -1 &&
             p->list_depths[p->stack_top] > p->indent_level)
        p->stack_top--;
      if (p->stack_top == -1 ||
          p->list_depths[p->stack_top] > p->indent_level) {
        p->stack_top = 0;
        p->list_depths[p->stack_top] = p->indent_level;
      }
    } else {
      list->list_depth = p->stack_top;
      p->list_stack[p->stack_top]->next = list;
      p->list_stack[p->stack_top] = list;
    }
  } else {
    p->stack_top = 0;
    p->list_stack[p->stack_top] = list;
    p->list_depths[p->stack_top] = p->indent_level;

    list->list_depth = p->stack_top;
    p->curr_node->next = list;
  }
  p->curr_node = list;
  p->is_newline = false;
  p->ptr++;
  return true;
}

static bool handle_underscore(Parser_Context *p, size_t n) {
  if (p->is_newline && is_breakline(p)) {
    Node *br = create_node(p, NODE_BREAK);
    if (br == NULL)
      return false;
    p->curr_node->next = br;
    p->curr_node = br;
    p->is_newline = false;
    p->ptr += 2;
    return true;
  }

  Text_Span *new_span = NULL;

  if (p->curr_fmt == STRING_ITALIC) {
    if (p->ptr && isspace((unsigned char)p->ptr[-1]))
      return handle_default(p, n);

    p->curr_fmt = STRING_REGULAR;
    if (p->curr_span)
      p->curr_span->type = STRING_ITALIC;
  } else {
    if (PARSER_CHECK_BOUNDS(p, 1) && isspace((unsigned char)p->ptr[1]))
      return handle_default(p, n);

    const char italic = *p->ptr;
    const char *line_ptr = p->ptr + 1;

    while (line_ptr < p->end && *line_ptr != '\n' && *line_ptr != italic) {
      line_ptr++;
    }

    if (line_ptr >= p->end)
      return true;

    new_span = create_text_span(p, STRING_REGULAR);
    if (new_span == NULL)
      return false;

    if (*line_ptr == '\n') {
      new_span->view.start = p->ptr;
      new_span->view.length++;
    } else {
      p->curr_fmt = STRING_ITALIC;
    }
  }

  if (new_span == NULL) {
    new_span = create_text_span(p, STRING_REGULAR);
    if (new_span == NULL)
      return false;
  }

  if (p->is_newline) {
    Node *par = create_node(p, NODE_PARAGRAPH);
    if (par == NULL)
      return false;
    par->text = new_span;
    if (p->curr_node->type == NODE_PARAGRAPH) {
      p->curr_node->next = par;
    } else {
      p->curr_node->child = par;
    }
    p->curr_node = par;
    p->is_newline = false;
  } else {
    if (p->curr_span)
      p->curr_span->next = new_span;
  }
  p->curr_span = new_span;
  return true;
}

static bool handle_asterix(Parser_Context *p, size_t n) {
  if (!PARSER_CHECK_BOUNDS(p, 1))
    return handle_default(p, n);

  if (p->ptr[1] != '*') {
    return handle_underscore(p, n);
  }

  if (p->curr_fmt == STRING_BOLD) {
    p->curr_fmt = STRING_REGULAR;
  } else {
    p->curr_fmt = STRING_BOLD;
  }

  Text_Span *new_span = create_text_span(p, p->curr_fmt);
  if (new_span == NULL)
    return false;

  if (p->is_newline) {
    Node *par = create_node(p, NODE_PARAGRAPH);
    if (par == NULL)
      return false;
    par->text = new_span;
    if (p->curr_node->type == NODE_PARAGRAPH ||
        p->curr_node->type == NODE_LIST || p->curr_node->type == NODE_BREAK ||
        p->curr_node->type == NODE_BREAK_NO_LINE) {
      p->curr_node->next = par;
    } else {
      p->curr_node->child = par;
    }
    p->curr_node = par;
    p->is_newline = false;
  } else {
    if (p->curr_span)
      p->curr_span->next = new_span;
  }

  p->curr_span = new_span;
  p->ptr++;
  return true;
}

Parser_Context *parser_create(Arena *arena) {
  Parser_Context *parser = arena_alloc(arena, sizeof(Parser_Context));
  if (parser == NULL)
    return NULL;
  parser->arena = arena;
  return parser;
}

Node *parser_parse(Parser_Context *p, const char *input, size_t len) {
  if (p == NULL || input == NULL)
    return NULL;

  p->root = create_node(p, NODE_ROOT);
  if (p->root == NULL)
    return NULL;
  p->curr_node = p->root;
  p->last_root_child = p->root->child;

  p->indent_level = 0;
  p->stack_top = -1;

  p->is_newline = true;

  p->ptr = input;
  p->end = input + len;

  p->curr_span = NULL;
  p->curr_fmt = STRING_REGULAR;

  while (p->ptr < p->end) {
    int char_len = utf8_char_length((unsigned char)*(p->ptr));
    if (char_len < 1) {
      return NULL;
    }
    size_t n = (size_t)char_len;
    bool success = true;

    switch (*(p->ptr)) {
    case '\n': {
      success = handle_newline(p);
    } break;
    case '#': {
      success = handle_heading(p, n);
    } break;
    case '-': {
      success = handle_list(p, n);
    } break;
    case '*': {
      success = handle_asterix(p, n);
    } break;
    case '_': {
      success = handle_underscore(p, n);
    } break;
    default: {
      success = handle_default(p, n);
    } break;
    }
    if (!success)
      return NULL;
    if (!p->is_newline)
      p->indent_level = 0;
    p->ptr += n;
  }

  return p->root;
}
