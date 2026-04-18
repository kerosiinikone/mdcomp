#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>
#include "arena.h"

typedef struct Parser Parser;

typedef enum {
  NODE_ROOT,

  NODE_HEADING,
  NODE_MEDIUM_HEADING,
  NODE_SMALL_HEADING,

  NODE_PARAGRAPH,
  NODE_LIST,

  NODE_BREAK,
  NODE_BREAK_NO_LINE,
} Node_Type;

typedef enum { STRING_REGULAR, STRING_BOLD, STRING_ITALIC } String_Type;

typedef struct {
  const char *start;
  size_t length;
} String_View;

typedef struct Text_Span {
  struct Text_Span *next;
  String_Type type;
  String_View view;
} Text_Span;

typedef struct Node {
  Node_Type type;
  size_t list_depth;

  Text_Span *text;
  struct Node *child;
  struct Node *next;
} Node;

Parser *parser_create(Arena *arena);
Node *parser_parse(Parser *p, const char *input, size_t len);

size_t utf8_char_length(unsigned char leading_byte);

#endif


