#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>

#include "parser.h"

struct Parser {
  Arena *arena;
  Node *root;
  Node *curr_node;
  Node *last_root_child;

  int indent_level;
  int stack_top;

  Node *list_stack[LIST_STACK_MAX];
  int list_depths[LIST_STACK_MAX];

  bool is_newline;

  char *ptr;
  char *end;

  Text_Span *curr_span;
  String_Type curr_fmt;
};

size_t utf8_char_length(unsigned char leading_byte) {
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

static bool is_breakline(char *ptr, char *end) {
  return ptr + 2 < end && *ptr == '_' && ptr[1] == '_' && ptr[2] == '_';
}

static void handle_newline(Parser *p) {
  if (p->ptr + 1 < p->end && p->ptr[1] == '\n') {
    Node *br_nl = arena_alloc(p->arena, sizeof(Node));
    br_nl->type = NODE_BREAK_NO_LINE;
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
}

static void handle_default(Parser *p, size_t n) {
  if (p->is_newline) {
    Node *par = arena_alloc(p->arena, sizeof(Node));
    par->type = NODE_PARAGRAPH;

    Text_Span *span = arena_alloc(p->arena, sizeof(Text_Span));
    span->type = STRING_REGULAR;
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
}

static void handle_heading(Parser *p, size_t n) {
  if (p->ptr + 1 >= p->end)
    return;

  if (!p->is_newline)
    return handle_default(p, n);

  int hash_count = 1;
  while (hash_count < 4 && p->ptr + hash_count < p->end &&
         p->ptr[hash_count] == '#') {
    hash_count++;
  }

  if (p->ptr + hash_count >= p->end ||
      !isspace((unsigned char)p->ptr[hash_count])) {
    return handle_default(p, n);
  }

  Node_Type heading_type = hash_count % 4 == 0 ? 1 : hash_count;
  Node *heading = arena_alloc(p->arena, sizeof(Node));
  heading->type = heading_type;

  Text_Span *span = arena_alloc(p->arena, sizeof(Text_Span));
  span->type = STRING_REGULAR;
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
    p->curr_node->child = heading;
    p->last_root_child = heading;
  } else if (should_be_sibling) {
    p->curr_node->next = heading;
    p->last_root_child = heading;
  } else {
    if (p->last_root_child == NULL) {
      p->root->child = heading;
      p->last_root_child = heading;
    } else {
      p->last_root_child->next = heading;
      p->last_root_child = heading;
    }
  }
  p->ptr += hash_count;
  p->curr_node = heading;
  p->is_newline = false;
}

static void handle_list(Parser *p, size_t n) {
  if (p->ptr + 1 >= p->end)
    return;

  if (!p->is_newline || !isspace((unsigned char)p->ptr[1])) {
    return handle_default(p, n);
  }

  Node *list = arena_alloc(p->arena, sizeof(Node));
  list->type = NODE_LIST;

  Text_Span *span = arena_alloc(p->arena, sizeof(Text_Span));
  span->type = STRING_REGULAR;
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
    int prev_indent = p->list_depths[p->stack_top];

    if (p->indent_level > prev_indent) {
      // child
      p->list_depths[++p->stack_top] = p->indent_level;
      list->list_depth = p->stack_top;
      p->list_stack[p->stack_top] = list;
      p->curr_node->child = list;
    } else if (p->indent_level < prev_indent) {
      // parent
      while (p->stack_top > -1 &&
             p->list_depths[p->stack_top] > p->indent_level)
        p->stack_top--;
      if (p->stack_top == -1 ||
          p->list_depths[p->stack_top] > p->indent_level) {
        p->stack_top = 0;
        p->list_depths[p->stack_top] = p->indent_level;
        list->list_depth = p->stack_top;
        p->list_stack[p->stack_top]->next = list;
        p->list_stack[p->stack_top] = list;
      } else {
        list->list_depth = p->stack_top;
        p->list_stack[p->stack_top]->next = list;
        p->list_stack[p->stack_top] = list;
      }
    } else {
      // equal
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
}

static void handle_underscore(Parser *p, size_t n) {
  if (p->is_newline && is_breakline(p->ptr, p->end)) {
    Node *br = arena_alloc(p->arena, sizeof(Node));
    br->type = NODE_BREAK;
    p->curr_node->next = br;
    p->curr_node = br;
    p->is_newline = false;
    p->ptr += 2;
    return;
  }

  Text_Span *new_span = arena_alloc(p->arena, sizeof(Text_Span));

  if (p->curr_fmt == STRING_ITALIC) {
    if (isspace((unsigned char)p->ptr[-1]))
      return handle_default(p, n);

    p->curr_fmt = STRING_REGULAR;
    p->curr_span->type = STRING_ITALIC;
  } else {
    if (isspace((unsigned char)p->ptr[1]))
      return handle_default(p, n);

    char italic = *p->ptr;
    char *line_ptr = p->ptr + 1;

    while (line_ptr < p->end && *line_ptr != '\n' && *line_ptr != italic) {
      line_ptr++;
    }

    // Check for preceding spaces in the loop?
    if (line_ptr >= p->end)
      return;
    else if (*line_ptr == '\n') {
      new_span->view.start = p->ptr;
      new_span->view.length++;
    } else {
      p->curr_fmt = STRING_ITALIC;
    }
    new_span->type = STRING_REGULAR;
  }

  if (p->is_newline) {
    Node *par = arena_alloc(p->arena, sizeof(Node));
    par->type = NODE_PARAGRAPH;
    par->text = new_span;
    if (p->curr_node->type == NODE_PARAGRAPH) {
      p->curr_node->next = par;
    } else {
      p->curr_node->child = par;
    }
    p->curr_node = par;
    p->is_newline = false;
  } else {
    p->curr_span->next = new_span;
  }
  p->curr_span = new_span;
}

static void handle_asterix(Parser *p, size_t n) {
  if (p->ptr + 1 >= p->end)
    return handle_default(p, n);

  if (p->ptr[1] != '*') {
    return handle_underscore(p, n);
  }

  if (p->curr_fmt == STRING_BOLD) {
    p->curr_fmt = STRING_REGULAR;
  } else {
    p->curr_fmt = STRING_BOLD;
  }

  Text_Span *new_span = arena_alloc(p->arena, sizeof(Text_Span));
  new_span->type = p->curr_fmt;

  if (p->is_newline) {
    Node *par = arena_alloc(p->arena, sizeof(Node));
    par->type = NODE_PARAGRAPH;
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
    p->curr_span->next = new_span;
  }

  p->curr_span = new_span;
  p->ptr++;
}

Parser *parser_create(Arena *arena) {
  Parser *parser = arena_alloc(arena, sizeof(Parser));
  parser->arena = arena;
  return parser;
}

Node *parser_parse(Parser *p, char *input, size_t len) {
  p->root = arena_alloc(p->arena, sizeof(Node));
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
    size_t n = utf8_char_length((unsigned char)*(p->ptr));

    switch (*(p->ptr)) {
    case '\n': {
      handle_newline(p);
    } break;
    case '#': {
      handle_heading(p, n);
    } break;
    case '-': {
      handle_list(p, n);
    } break;
    case '*': {
      handle_asterix(p, n);
    } break;
    case '_': {
      handle_underscore(p, n);
    } break;
    default: {
      handle_default(p, n);
    } break;
    }
    if (!p->is_newline)
      p->indent_level = 0;
    p->ptr += n;
  }

  return p->root;
}
