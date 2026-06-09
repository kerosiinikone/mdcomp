#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "parser.h"

#define PARSER_CHECK_BOUNDS(p, n) ((p)->ptr + (n) < (p)->end)

#define IS_BODY_NODE(type)                                                     \
  ((type) == NODE_PARAGRAPH || (type) == NODE_LIST || (type) == NODE_BREAK ||  \
   (type) == NODE_BREAK_NO_LINE)

#define IS_TOP_LEVEL_NODE(type)                                                \
  (type) == NODE_ROOT || (type) == NODE_HEADING ||                             \
      (type) == NODE_MEDIUM_HEADING || (type) == NODE_SMALL_HEADING

#define IS_NON_LINK_CHAR(c)                                                    \
  (c) < 33 || (c) > 126 || (c) == '"' || (c) == '<' || (c) == '>' ||           \
      (c) == '\\' || (c) == '^' || (c) == '`' || (c) == '{' || (c) == '|' ||   \
      (c) == '}' || (c) == ')' || (c) == '('

typedef bool (*Format_Parser)(Parser_Context *p);

typedef struct {
  size_t indent_level;
  int stack_top;

  Node *list_stack[MAX_LIST_COUNT];
  size_t list_depths[MAX_LIST_COUNT];
} List_Stack;

struct Parser_Context {
  Arena *arena;
  Node *root;
  Node *curr_node;
  Node *last_root_child;

  bool is_newline;

  List_Stack *list;

  const char *ptr;
  const char *end;

  Text_Span *curr_span;
  bool has_outer_span;

  String_Type curr_fmt;
  String_Type outer_fmt;
};

typedef enum {
  ASTERIX_ITALIC,
  ASTERIX_BOLD,
  ASTERIX_BI,
} Asterix_Type;

typedef enum {
  UNDERLINE_ITALIC,
  UNDERLINE_BOLD,
  UNDERLINE_BREAK,
} Underscore_Type;

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

static List_Stack *create_list_stack(Parser_Context *p) {
  List_Stack *stack = arena_alloc(p->arena, sizeof(List_Stack));
  if (stack == NULL)
    return NULL;
  stack->stack_top = -1;
  return stack;
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
    p->list->indent_level++;
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

    if (IS_BODY_NODE(p->curr_node->type)) {
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

static void pop_stack_until(List_Stack *s) {
  while (s->stack_top > -1 && s->list_depths[s->stack_top] > s->indent_level)
    s->stack_top--;
}

static void reset_stack(List_Stack *s, Node *list) {
  s->stack_top = 0;
  s->list_stack[s->stack_top] = list;
  s->list_depths[s->stack_top] = s->indent_level;
}

static void attach_stack_top(List_Stack *s, Node *list) {
  list->list_depth = s->stack_top;
  s->list_stack[s->stack_top]->next = list;
  s->list_stack[s->stack_top] = list;
}

static void push_stack(List_Stack *s, Node *list) {
  s->list_depths[++s->stack_top] = s->indent_level;
  list->list_depth = s->stack_top;
  s->list_stack[s->stack_top] = list;
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

  if (IS_TOP_LEVEL_NODE(p->curr_node->type)) {
    reset_stack(p->list, list);
    list->list_depth = p->list->stack_top;

    p->curr_node->child = list;
  } else if (p->curr_node->type == NODE_LIST) {
    size_t prev_indent = p->list->list_depths[p->list->stack_top];

    if (p->list->indent_level > prev_indent) {
      if (p->list->stack_top < MAX_LIST_COUNT - 1)
        push_stack(p->list, list);
      else
        attach_stack_top(p->list, list);
      p->curr_node->child = list;
    } else if (p->list->indent_level < prev_indent) {
      pop_stack_until(p->list);

      if (p->list->stack_top == -1 ||
          p->list->list_depths[p->list->stack_top] > p->list->indent_level) {
        p->list->stack_top = 0;
        p->list->list_depths[p->list->stack_top] = p->list->indent_level;
      }
      attach_stack_top(p->list, list);
    } else {
      attach_stack_top(p->list, list);
    }
  } else {
    reset_stack(p->list, list);
    list->list_depth = p->list->stack_top;

    p->curr_node->next = list;
  }
  p->curr_node = list;
  p->is_newline = false;
  p->ptr++;
  return true;
}

static bool find_closing_marker(Parser_Context *p, const char *marker,
                                size_t marker_len, bool is_underscore) {
  const char *search = p->ptr + marker_len;

  while (search < p->end) {
    if (*search == '\n')
      return false;

    bool matches = true;
    for (size_t i = 0; i < marker_len && search + i < p->end; i++) {
      if (search[i] != marker[i]) {
        matches = false;
        break;
      }
    }
    bool has_prev_space =
        search > p->ptr + marker_len && !isspace((unsigned char)search[-1]);
    bool has_next_break_char = isspace((unsigned char)search[marker_len]) ||
                IS_NON_LINK_CHAR(search[marker_len]);

    if (is_underscore) {
      if (matches && has_prev_space && has_next_break_char)
        return true;
    } else {
      if (matches && has_prev_space) {
        return true;
      }
    }
    search++;
  }
  return false;
}

static bool validate_format_marker(Parser_Context *p, const char *marker,
                                   size_t marker_len, bool is_closing,
                                   bool is_underscore) {
  if (is_closing) {
    if (isspace((unsigned char)p->ptr[-1]))
      return false;
  } else {
    if (!PARSER_CHECK_BOUNDS(p, marker_len) ||
        isspace((unsigned char)p->ptr[marker_len]))
      return false;
    if (!find_closing_marker(p, marker, marker_len, is_underscore))
      return false;
  }
  return true;
}

static bool parse_bold(Parser_Context *p) {
  switch (p->curr_fmt) {
  case STRING_BOLD: {
    if (!validate_format_marker(p, "**", 2, true, false))
      return false;
    p->curr_fmt = STRING_REGULAR;
  } break;
  case STRING_ITALIC: {
    if (!validate_format_marker(p, "**", 2, false, false))
      return false;
    p->has_outer_span = true;
    p->outer_fmt = STRING_ITALIC;
    p->curr_fmt = STRING_BI;
  } break;
  case STRING_REGULAR: {
    if (!validate_format_marker(p, "**", 2, false, false))
      return false;
    p->curr_fmt = STRING_BOLD;
  } break;
  case STRING_BI: {
    if (!validate_format_marker(p, "**", 2, true, false))
      return false;
    if (p->has_outer_span) {
      p->curr_fmt = p->outer_fmt;
      p->has_outer_span = false;
    }
  } break;
  }
  return true;
}

static bool parse_bold_italic(Parser_Context *p) {
  switch (p->curr_fmt) {
  case STRING_REGULAR: {
    if (!validate_format_marker(p, "***", 3, false, false))
      return false;
    p->curr_fmt = STRING_BI;
  } break;
  case STRING_BI: {
    if (!validate_format_marker(p, "***", 3, true, false))
      return false;
    p->curr_fmt = STRING_REGULAR;
  } break;
  case STRING_ITALIC:
  case STRING_BOLD:
    break;
  }
  return true;
}

static bool parse_italic(Parser_Context *p) {
  switch (p->curr_fmt) {
  case STRING_ITALIC: {
    if (!validate_format_marker(p, "*", 1, true, false))
      return false;
    p->curr_fmt = STRING_REGULAR;
  } break;
  case STRING_REGULAR: {
    if (!validate_format_marker(p, "*", 1, false, false))
      return false;
    p->curr_fmt = STRING_ITALIC;
  } break;
  case STRING_BOLD: {
    if (!validate_format_marker(p, "*", 1, false, false))
      return false;
    p->has_outer_span = true;
    p->outer_fmt = STRING_BOLD;
    p->curr_fmt = STRING_BI;
  } break;
  case STRING_BI: {
    if (!validate_format_marker(p, "*", 1, true, false))
      return false;
    if (p->has_outer_span) {
      p->curr_fmt = p->outer_fmt;
      p->has_outer_span = false;
    }
  } break;
  }
  return true;
}

static Underscore_Type get_underscore_type(Parser_Context *p) {
  if (PARSER_CHECK_BOUNDS(p, 2) && p->ptr[2] == '_' && p->ptr[1] == '_')
    return UNDERLINE_BREAK;
  if (PARSER_CHECK_BOUNDS(p, 1) && p->ptr[1] == '_')
    return UNDERLINE_BOLD;
  return UNDERLINE_ITALIC;
}

static bool parse_underscore_italic(Parser_Context *p) {
  switch (p->curr_fmt) {
  case STRING_ITALIC: {
    if (!validate_format_marker(p, "_", 1, true, true))
      return false;
    p->curr_fmt = STRING_REGULAR;
  } break;
  case STRING_REGULAR: {
    if (!validate_format_marker(p, "_", 1, false, true))
      return false;
    p->curr_fmt = STRING_ITALIC;
  } break;
  case STRING_BOLD: {
    if (!validate_format_marker(p, "_", 1, false, true))
      return false;
    p->has_outer_span = true;
    p->outer_fmt = STRING_BOLD;
    p->curr_fmt = STRING_BI;
  } break;
  case STRING_BI: {
    if (!validate_format_marker(p, "_", 1, true, true))
      return false;
    if (p->has_outer_span) {
      p->curr_fmt = p->outer_fmt;
      p->has_outer_span = false;
    }
  } break;
  }
  return true;
}

static bool parse_underscore_bold(Parser_Context *p) {
  switch (p->curr_fmt) {
  case STRING_BOLD: {
    if (!validate_format_marker(p, "__", 2, true, true))
      return false;
    p->curr_fmt = STRING_REGULAR;
  } break;
  case STRING_ITALIC: {
    if (!validate_format_marker(p, "__", 2, false, true))
      return false;
    p->has_outer_span = true;
    p->outer_fmt = STRING_ITALIC;
    p->curr_fmt = STRING_BI;
  } break;
  case STRING_REGULAR: {
    if (!validate_format_marker(p, "__", 2, false, true))
      return false;
    p->curr_fmt = STRING_BOLD;
  } break;
  case STRING_BI: {
    if (!validate_format_marker(p, "__", 2, true, true))
      return false;
    if (p->has_outer_span) {
      p->curr_fmt = p->outer_fmt;
      p->has_outer_span = false;
    }
  } break;
  }
  return true;
}

static bool handle_underscore(Parser_Context *p, size_t n) {
  Underscore_Type us_count = get_underscore_type(p);

  if (p->is_newline && us_count == UNDERLINE_BREAK) {
    Node *br = create_node(p, NODE_BREAK);
    if (br == NULL)
      return false;
    p->curr_node->next = br;
    p->curr_node = br;
    p->is_newline = false;
    p->ptr += us_count;
    return true;
  }

  Format_Parser us_parser[] = {parse_underscore_italic, parse_underscore_bold};
  if (us_count != UNDERLINE_BREAK && !us_parser[us_count](p))
    return handle_default(p, n);

  Text_Span *new_span = create_text_span(p, p->curr_fmt);
  if (new_span == NULL)
    return false;

  if (p->is_newline) {
    Node *par = create_node(p, NODE_PARAGRAPH);
    if (par == NULL)
      return false;
    par->text = new_span;
    if (IS_BODY_NODE(p->curr_node->type)) {
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
  p->ptr += us_count;
  return true;
}

static Asterix_Type get_asterix_type(Parser_Context *p) {
  if (PARSER_CHECK_BOUNDS(p, 2) && p->ptr[1] == '*' && p->ptr[2] == '*')
    return ASTERIX_BI;
  if (PARSER_CHECK_BOUNDS(p, 1) && p->ptr[1] == '*')
    return ASTERIX_BOLD;
  return ASTERIX_ITALIC;
}

static bool handle_asterix(Parser_Context *p, size_t n) {
  Asterix_Type ast_count = get_asterix_type(p);

  Format_Parser ast_parser[] = {parse_italic, parse_bold, parse_bold_italic};
  if (!ast_parser[ast_count](p))
    return handle_default(p, n);

  Text_Span *new_span = create_text_span(p, p->curr_fmt);
  if (new_span == NULL)
    return false;

  if (p->is_newline) {
    Node *par = create_node(p, NODE_PARAGRAPH);
    if (par == NULL)
      return false;
    par->text = new_span;
    if (IS_BODY_NODE(p->curr_node->type)) {
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
  p->ptr += ast_count;
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

  p->list = create_list_stack(p);
  p->root = create_node(p, NODE_ROOT);
  if (p->root == NULL)
    return NULL;
  p->curr_node = p->root;
  p->last_root_child = p->root->child;

  p->is_newline = true;
  p->ptr = input;
  p->end = input + len;

  p->curr_span = NULL;
  p->curr_fmt = STRING_REGULAR;
  p->outer_fmt = STRING_REGULAR;

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
      p->list->indent_level = 0;
    p->ptr += n;
  }

  return p->root;
}
