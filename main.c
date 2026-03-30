#include <assert.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "render.c"

#define DEFAULT_OUTPUT_PATH "./output.pdf"

#define ALIGN_8(size) ((size) + 7) & ~7

#define HEADER_OFFSET 30
#define BODY_OFFSET 15
#define DRAW_AREA 612-72*2

size_t utf8_char_length(unsigned char leading_byte) {
	if ((leading_byte & 0x80) == 0x00) return 1; 
	if ((leading_byte & 0xE0) == 0xC0) return 2;
	if ((leading_byte & 0xF0) == 0xE0) return 3;
	if ((leading_byte & 0xF8) == 0xF0) return 4;
	return -1;
}

typedef enum {
	NODE_ROOT,
	NODE_HEADING,
	NODE_PARAGRAPH,
	NODE_LIST,
} Node_Type;

typedef enum {
	STRING_REGULAR,
	STRING_BOLD,
	STRING_ITALIC
} String_Type;

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
	
	Text_Span *text;

	struct Node *child;
	struct Node *next;
} Node;

typedef struct {
    char *data;
    size_t capacity;
    size_t offset;
} Buf_Context;

// TODO: realloc / return false if fails?
void buf_ctx_append(Buf_Context *ctx, char *fmt, ...) {
	if (ctx->offset >= ctx->capacity - 1) return;

	va_list args;
	va_start(args, fmt);
	int n = vsnprintf(ctx->data + ctx->offset, ctx->capacity - ctx->offset, fmt, args);
	va_end(args);

	if (n > 0) {
		ctx->offset += n;
        	if (ctx->offset >= ctx->capacity) {
        		ctx->offset = ctx->capacity - 1; 
        	}
	}
}

char utf8_to_ascii(const char **ptr) {
	size_t n = utf8_char_length((unsigned char)**ptr);
	unsigned char first_byte = (unsigned char)(**ptr);

	if (n == 1) {
		(*ptr)++;
		return first_byte;
	}
	if (n == 2) {
		unsigned char second_byte = (unsigned char)(*ptr)[1];
		uint16_t codepoint = ((first_byte & 0x1F) << 6) | (second_byte & 0x3F);
		switch (codepoint) {
			case 0xE4: case 0xE5:
				*ptr += 2; return 'a';
			case 0xF6:
				*ptr += 2; return 'o';
			case 0xC4: case 0xC5:
				*ptr += 2; return 'A';
			case 0xD6:
				*ptr += 2; return 'O';
			default:
				*ptr += 2; return '?';
		}
	}
	*ptr += n;
	return '?';
}

void buf_ctx_append_trans(Buf_Context *ctx, const char *str, size_t length) {
	const char *ptr = str;
	const char *end = str + length;
	
	while (ptr < end && ctx->offset < ctx->capacity - 1) {
		char ascii_char = utf8_to_ascii(&ptr);
		ctx->data[ctx->offset++] = ascii_char;
	}
}

typedef struct {
	uint8_t *data;
	size_t cap;
	size_t offset;
} Arena;

Arena arena_create(size_t cap) {
	return (Arena){
		.data = malloc(cap),
		.cap = cap,
		.offset = 0
	};
}

void* arena_alloc(Arena *arena, size_t size) {
	size_t align = ALIGN_8(size);
	if (arena->offset + align > arena->cap) {
		return NULL;
	};
	void *ptr = arena->data + arena->offset;
	arena->offset += align;
	memset(ptr, 0, align);
	return ptr;
}

void arena_destroy(Arena *arena) {
	free(arena->data);
}

void render_node(Node *node, Buf_Context *ctx, int y);

int main(int argc, char *argv[]) 
{
	struct stat st;
	if (argc < 2 || argc > 2) return -1;

	int fd = open(argv[1], O_RDONLY);
	if (fd < 0) return -1;

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
	Arena arena = arena_create(1024*1024);

	// TODO: check for NULL
	Node *root = arena_alloc(&arena, sizeof(Node));
	Node *curr_node = root;
	Node *last_root_child = root->child;

	bool is_newline = true;

	char *ptr = file_data;
	char *end = file_data + st.st_size;

	Text_Span *curr_span = NULL;
	String_Type curr_fmt = STRING_REGULAR;

	while (ptr < end) 
	{
		// get leading byte -> len of char
		size_t n = utf8_char_length((unsigned char)*ptr);
		// strcmp
		// parse and adv the pointer the len of the char
		switch (*ptr)
		{
			case '\n': {
				is_newline = true;
			} break;
			// TODO: smaller headings
			case '#': {
				if (ptr + n >= end) {
					break;
            			}
				if (!is_newline || !isspace((unsigned char)ptr[n])) {
					goto add_char;
				}

				Node *heading = arena_alloc(&arena, sizeof(Node));
				heading->type = NODE_HEADING;

				Text_Span *span = arena_alloc(&arena, sizeof(Text_Span));
				span->type = STRING_REGULAR;
				heading->text = span;
				curr_span = span;
				curr_fmt = STRING_REGULAR;

				if (curr_node->type == NODE_ROOT) {
					curr_node->child = heading;
					last_root_child = heading;
				} else if (curr_node->type == NODE_HEADING) {
					curr_node->next = heading;
					last_root_child = heading;
				} else {
					if (last_root_child == NULL) {
						root->child = heading;
						last_root_child = heading;
					} else {
						last_root_child->next = heading;
						last_root_child = heading;
					}
				}
				ptr += n;
				curr_node = heading;
				is_newline = false;
			} break;
			case '-': {
				if (ptr + n >= end) {
					break;
            			}
				if (!is_newline || !isspace((unsigned char)ptr[n])) {
					goto add_char;
				}

				Node *list = arena_alloc(&arena, sizeof(Node));
				list->type = NODE_LIST;

				Text_Span *span = arena_alloc(&arena, sizeof(Text_Span));
				span->type = STRING_REGULAR;
				list->text = span;
				curr_span = span;
				curr_fmt = STRING_REGULAR;

				if (curr_node->type == NODE_ROOT || curr_node->type == NODE_HEADING) {
					curr_node->child = list;
				} else {
					curr_node->next = list;
				}
				ptr += n;
				curr_node = list;
				is_newline = false;
			} break;
			case '*': {
				if (ptr + n >= end || ptr[n] != '*') {
					goto add_char;
				}
				
				if (curr_fmt == STRING_BOLD) {
					curr_fmt = STRING_REGULAR;
				} else {
					curr_fmt = STRING_BOLD;
				}
				
				Text_Span *new_span = arena_alloc(&arena, sizeof(Text_Span));
				new_span->type = curr_fmt;
				
				if (curr_span) {
					curr_span->next = new_span;
				}
				curr_span = new_span;
				
				ptr += n;
			} break;
			case '_': {
				if (curr_fmt == STRING_ITALIC) {
					curr_fmt = STRING_REGULAR;
				} else {
					curr_fmt = STRING_ITALIC;
				}
				
				Text_Span *new_span = arena_alloc(&arena, sizeof(Text_Span));
				new_span->type = curr_fmt;
				
				if (curr_span) {
					curr_span->next = new_span;
				}
				curr_span = new_span;
			} break;
			default: add_char: {
				if (is_newline) {
					Node *p = arena_alloc(&arena, sizeof(Node));
					p->type = NODE_PARAGRAPH;

					Text_Span *span = arena_alloc(&arena, sizeof(Text_Span));
					span->type = STRING_REGULAR;
					p->text = span;
					curr_span = span;
					curr_fmt = STRING_REGULAR;

					if (curr_node->type == NODE_PARAGRAPH) {
						curr_node->next = p;
					} else {
						curr_node->child = p;
					}
					curr_node = p;
					is_newline = false;
				}
				
				if (curr_span) {
					if (curr_span->view.start == NULL) {
						curr_span->view.start = ptr;
					}
					curr_span->view.length += n;
				}
			} break;
		}
		ptr += n;
	}

	char stream_buf[10*1024] = {0};
	Buf_Context ctx = {
		.data = stream_buf,
		.capacity = sizeof(stream_buf),
		.offset = 0
	};

	buf_ctx_append(&ctx, "BT\n");
	// page height?
	render_node(root->child, &ctx, 750);
	buf_ctx_append(&ctx, "ET");

	int kids[1] = {3};

	PDF_Context pctx = {0};
	if (!pdf_init(&pctx, DEFAULT_OUTPUT_PATH)) {
		return -1;
	}

	PDF_Object cat = {
		.id = 1,
		.type = PDF_CATALOG,
		.catalog = { 2 }
	};
	PDF_Object tree = {
		.id = 2,
		.type = PDF_TREE,
		.tree = { 1, kids },
	};
	PDF_Object page = {
		.id = 3,
		.type = PDF_PAGE,
		.page = { 4, 612, 792, tree.id, 5 },
	};
	PDF_Object contents = {
		.id = 4,
		.type = PDF_CONTENT,
		.content = { ctx.offset, stream_buf }
	};
	PDF_Object font_reg = {
		.id = 5,
		.type = PDF_FONT,
		.font = 1
	};
	PDF_Object font_bold = {
		.id = 6,
		.type = PDF_FONT,
		.font = 2
	};
	PDF_Object font_italic = {
		.id = 7,
		.type = PDF_FONT,
		.font = 3
	};
	 
	pdf_obj_write(&pctx, &cat);
	pdf_obj_write(&pctx, &tree);
	pdf_obj_write(&pctx, &page);
	pdf_obj_write(&pctx, &font_reg);
	pdf_obj_write(&pctx, &font_bold);
	pdf_obj_write(&pctx, &font_italic);
	pdf_obj_write(&pctx, &contents);

	pdf_xref_table_write(&pctx);
	pdf_trailer_write(&pctx, cat.id);
	fclose(pctx.f);

	arena_destroy(&arena);
	munmap(file_data, filesize);
	close(fd);
	return 0;
}

// TODO: preprocess for pages and escape \()
void render_node(Node *node, Buf_Context *ctx, int y) {
	if (node == NULL) return;
	while (node)
	{
		buf_ctx_append(ctx, "1 0 0 1 72 %d Tm\n", y);
		switch (node->type) {
			case NODE_HEADING: {
				Text_Span *curr = node->text;

				int cursor = 72;
				size_t last_space_offset = 0;

				while (curr) 
				{
					switch (curr->type) {
						case STRING_REGULAR:
						buf_ctx_append(ctx, "/F1 24 Tf\n");
						break;
						case STRING_BOLD:
						buf_ctx_append(ctx, "/F2 24 Tf\n");
						break;
						case STRING_ITALIC:
						buf_ctx_append(ctx, "/F3 24 Tf\n");
						break;
					}

					char *span_ptr = (char*)curr->view.start;
					char *end = span_ptr + curr->view.length;
					char *segment_start = span_ptr;
					int char_index = 0;

					while (span_ptr < end) 
					{
						size_t n = utf8_char_length((unsigned char)*span_ptr);

						if (isspace(*span_ptr)) {
							// absolute space offset from last
							last_space_offset = span_ptr - segment_start;
						}
						if (cursor > DRAW_AREA) {
							int emit_length = last_space_offset > 0 ? last_space_offset : char_index;
							buf_ctx_append(ctx, "(");
							buf_ctx_append_trans(ctx, segment_start, emit_length);
							buf_ctx_append(ctx, ") Tj\n");
							y -= HEADER_OFFSET;
							buf_ctx_append(ctx, "1 0 0 1 72 %d Tm\n", y);
							
							switch (curr->type) {
								case STRING_REGULAR:
								buf_ctx_append(ctx, "/F1 24 Tf\n");
								break;
								case STRING_BOLD:
								buf_ctx_append(ctx, "/F2 24 Tf\n");
								break;
								case STRING_ITALIC:
								buf_ctx_append(ctx, "/F3 24 Tf\n");
								break;
							}
							segment_start += emit_length;
							cursor = 72;
							char_index = 0;
							last_space_offset = 0;
						}
						// glyph width, font size
						cursor += 12;
						span_ptr += n;
						char_index += n;
					}
					int rest = span_ptr - segment_start;
					if (rest > 0) {
						buf_ctx_append(ctx, "(");
						buf_ctx_append_trans(ctx, segment_start, rest);
						buf_ctx_append(ctx, ") Tj\n");
					}
					curr = curr->next;
				}
				y -= HEADER_OFFSET;
				break;
			}
			case NODE_LIST: {
				Text_Span *curr = node->text;
				buf_ctx_append(ctx, "/F1 12 Tf\n");
				buf_ctx_append(ctx, "(- ) Tj\n");
				while (curr) {
					switch (curr->type) {
						case STRING_REGULAR:
						buf_ctx_append(ctx, "/F1 12 Tf\n");
						break;
						case STRING_BOLD:
						buf_ctx_append(ctx, "/F2 12 Tf\n");
						break;
						case STRING_ITALIC:
						buf_ctx_append(ctx, "/F3 12 Tf\n");
						break;
					}
					buf_ctx_append(ctx, "(");
					buf_ctx_append_trans(ctx, curr->view.start, curr->view.length);
					buf_ctx_append(ctx, ") Tj\n");
					curr = curr->next;
				}
				y -= BODY_OFFSET;
				break;
			}
			case NODE_PARAGRAPH: {
				Text_Span *curr = node->text;
				while (curr) {
					switch (curr->type) {
						case STRING_REGULAR:
						buf_ctx_append(ctx, "/F1 12 Tf\n");
						break;
						case STRING_BOLD:
						buf_ctx_append(ctx, "/F2 12 Tf\n");
						break;
						case STRING_ITALIC:
						buf_ctx_append(ctx, "/F3 12 Tf\n");
						break;
					}
					buf_ctx_append(ctx, "(");
					buf_ctx_append_trans(ctx, curr->view.start, curr->view.length);
					buf_ctx_append(ctx, ") Tj\n");
					curr = curr->next;
				}
				y -= BODY_OFFSET;
				break;
			}
			default:
				break;
		}
        	if (node->child)
        		render_node(node->child, ctx, y);
        	node = node->next;
	}
}
