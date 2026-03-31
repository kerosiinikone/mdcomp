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

#define ARENA_SIZE 1024 * 1024

#define HEADER_OFFSET 30
#define BODY_OFFSET 15
#define DRAW_AREA 612-72*2
#define PAGE_HEIGHT 750

size_t utf8_char_length(unsigned char leading_byte) {
	if ((leading_byte & 0x80) == 0x00) return 1; 
	if ((leading_byte & 0xE0) == 0xC0) return 2;
	if ((leading_byte & 0xF0) == 0xE0) return 3;
	if ((leading_byte & 0xF8) == 0xF0) return 4;
	return -1;
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

void *arena_alloc(Arena *arena, size_t size) {
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

typedef enum {
	NODE_ROOT,

	NODE_HEADING,
	NODE_MEDIUM_HEADING,
	NODE_SMALL_HEADING,

	NODE_PARAGRAPH,
	NODE_LIST,
} Node_Type;

typedef enum {
	STRING_REGULAR,
	STRING_BOLD,
	STRING_ITALIC
} String_Type;

typedef struct {
    char *data;
    size_t capacity;
    size_t offset;
} Buf_Context;

typedef struct {
	Buf_Context **data;
	size_t capacity;
	size_t length;

	int global_cursor;
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
	if (pca->length >= pca->capacity - 1) return;
	pca->data[pca->length++] = buf_ptr;
}

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
	unsigned char first_byte = (unsigned char)(**ptr);
	size_t n = utf8_char_length(first_byte);

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
		switch (ascii_char) {
			case '(': case ')': case '\\': {
				ctx->data[ctx->offset++] = '\\';
			}
		}
		ctx->data[ctx->offset++] = ascii_char;
	}
}

void temp_render_node(Arena *arena, Node *node, Page_Context *ctx);

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
	Arena arena = arena_create(ARENA_SIZE);

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
		size_t n = utf8_char_length((unsigned char)*ptr);

		switch (*ptr)
		{
			case '\n': {
				is_newline = true;
			} break;
			case '#': {
				if (ptr + 1 >= end) break;
				if (!is_newline) goto add_char;

				int hash_count = 1;
				while (hash_count < 3 && ptr + hash_count < end && ptr[hash_count] == '#') {
					hash_count++;
				}

				if (ptr + hash_count >= end || !isspace((unsigned char)ptr[hash_count])) {
					goto add_char;
				}

				Node_Type heading_type = hash_count;
				Node *heading = arena_alloc(&arena, sizeof(Node));
				heading->type = heading_type;

				Text_Span *span = arena_alloc(&arena, sizeof(Text_Span));
				span->type = STRING_REGULAR;
				heading->text = span;
				curr_span = span;
				curr_fmt = STRING_REGULAR;

				bool should_be_child = false;
				bool should_be_sibling = false;

				switch (curr_node->type) {
					case NODE_ROOT:
						should_be_child = true;
						break;
					case NODE_HEADING:
						should_be_child = (heading_type == NODE_MEDIUM_HEADING || heading_type == NODE_SMALL_HEADING);
						should_be_sibling = (heading_type == NODE_HEADING);
						break;
					case NODE_MEDIUM_HEADING:
						should_be_child = (heading_type == NODE_SMALL_HEADING);
						should_be_sibling = (heading_type == NODE_HEADING || heading_type == NODE_MEDIUM_HEADING);
						break;
					case NODE_SMALL_HEADING:
						should_be_sibling = true;
						break;
					default:
						break;
				}
				if (should_be_child) {
					curr_node->child = heading;
					last_root_child = heading;
				} else if (should_be_sibling) {
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
				ptr += hash_count;
				curr_node = heading;
				is_newline = false;
			} break;
			case '-': {
				if (ptr + 1 >= end) break;
				if (!is_newline || !isspace((unsigned char)ptr[1])) {
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
				ptr++;
				curr_node = list;
				is_newline = false;
			} break;
			// TODO: Make into a generic function to take in the format char and function pointer / types
			case '*': {
				if (ptr + 1 >= end) goto add_char;

				if (ptr[1] != '*') {
					goto italic;
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
				
				ptr++;
			} break;
			case '_': italic: {
				// always allocates -> can be opt
				Text_Span *new_span = arena_alloc(&arena, sizeof(Text_Span));

				if (curr_fmt == STRING_ITALIC) {
					curr_fmt = STRING_REGULAR;
					curr_span->type = STRING_ITALIC;
				} else {
					// TODO: if the next char is a space -> treat it as not closed!
					// refactor
					char *line_ptr = ptr + 1;
					while (line_ptr < end && *line_ptr != '\n' && *line_ptr != '_' && *line_ptr != '*') {
						line_ptr++;
					}
					if (line_ptr >= end) break;
					if (*line_ptr == '\n') {
						// will not be closed
						new_span->view.start = ptr;
						new_span->view.length++;
					}
					curr_fmt = STRING_ITALIC;
					new_span->type = STRING_REGULAR;
				}
				
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

	// Heap allocated struct?
	Buf_Context ctx = {
		.data = arena_alloc(&arena, 10*1024),
		.capacity = 10*1024,
		.offset = 0
	};
	buf_ctx_append(&ctx, "BT\n");
	
	// Append first -> the curr pointer is always valid
	Page_Context p_ctx_arr = page_ctx_create(&arena, 50*1024);
	page_ctx_append(&p_ctx_arr, &ctx);

	temp_render_node(&arena, root->child, &p_ctx_arr);
	
	// complete the buffer in case it is still "open"
	Buf_Context *curr = p_ctx_arr.data[p_ctx_arr.length-1];
	if (curr->offset != curr->capacity) {
		buf_ctx_append(curr, "ET"); 
	}

	PDF_Context pctx = {0};
	int kids[50] = {0};

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
		.tree = { 0, kids }
	};
	PDF_Object font_reg = {
		.id = 3,
		.type = PDF_FONT,
		.font = 1
	};
	PDF_Object font_bold = {
		.id = 4,
		.type = PDF_FONT,
		.font = 2
	};
	PDF_Object font_italic = {
		.id = 5,
		.type = PDF_FONT,
		.font = 3
	};

	for (int page_id = 1; page_id < p_ctx_arr.length * 2; page_id += 2) {
		// from 1, not 2
		tree.tree.kids[tree.tree.count++] = font_italic.id + page_id;
	}

	pdf_obj_write(&pctx, &cat);
	pdf_obj_write(&pctx, &tree);
	pdf_obj_write(&pctx, &font_reg);
	pdf_obj_write(&pctx, &font_bold);
	pdf_obj_write(&pctx, &font_italic);

	int page_id = font_italic.id + 1;
	int content_id = font_italic.id + 2;

	for (size_t i = 0; i < p_ctx_arr.length; i++) {
		Buf_Context *curr_buf = p_ctx_arr.data[i];
		
		PDF_Object contents = {
			.id = content_id,
			.type = PDF_CONTENT,
			.content = { curr_buf->offset, curr_buf->data }
		};

		PDF_Object page = {
			.id = page_id,
			.type = PDF_PAGE,
			.page = { content_id, 612, 792, tree.id, 3 },
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

// TODO: more spacing BEFORE headings (lower the cursor further)
void temp_render_node(Arena *arena, Node *node, Page_Context *ctx) {
	if (node == NULL) return;
	while (node)
	{
		Buf_Context *curr_ctx = ctx->data[ctx->length-1];

		buf_ctx_append(curr_ctx, "1 0 0 1 72 %d Tm\n", ctx->global_cursor);
		switch (node->type) {
			case NODE_HEADING: case NODE_MEDIUM_HEADING: case NODE_SMALL_HEADING: {
				Text_Span *curr = node->text;

				int cursor = 72;
				size_t last_space_offset = 0;

				while (curr) 
				{
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

							buf_ctx_append(curr_ctx, "(");
							buf_ctx_append_trans(curr_ctx, segment_start, emit_length);
							buf_ctx_append(curr_ctx, ") Tj\n");

							ctx->global_cursor -= HEADER_OFFSET;

							if (ctx->global_cursor < 72) {
								buf_ctx_append(curr_ctx, "ET");

								Buf_Context *new_ctx = arena_alloc(arena, sizeof(Buf_Context));
								new_ctx->capacity = 10*1024;
								new_ctx->data = arena_alloc(arena, new_ctx->capacity);
								buf_ctx_append(new_ctx, "BT\n");

								page_ctx_append(ctx, new_ctx);
								curr_ctx = new_ctx;
								ctx->global_cursor = PAGE_HEIGHT;
							}
							buf_ctx_append(curr_ctx, "1 0 0 1 72 %d Tm\n", ctx->global_cursor);
							
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
						buf_ctx_append(curr_ctx, "(");
						buf_ctx_append_trans(curr_ctx, segment_start, rest);
						buf_ctx_append(curr_ctx, ") Tj\n");
					}
					last_space_offset = 0;
					curr = curr->next;
				}

				ctx->global_cursor -= HEADER_OFFSET;

				if (ctx->global_cursor < 72) {
					buf_ctx_append(curr_ctx, "ET");

					Buf_Context *new_ctx = arena_alloc(arena, sizeof(Buf_Context));
					new_ctx->capacity = 10*1024;
					new_ctx->data = arena_alloc(arena, new_ctx->capacity);
					page_ctx_append(ctx, new_ctx);

					buf_ctx_append(new_ctx, "BT\n");
					curr_ctx = new_ctx;

					ctx->global_cursor = PAGE_HEIGHT;
				}
				break;
			}
			case NODE_LIST: {
				Text_Span *curr = node->text;
				buf_ctx_append(curr_ctx, "/F1 12 Tf\n");
				// TODO: 'write_list' -> more appropriate list indicators?
				buf_ctx_append(curr_ctx, "(- ) Tj\n");
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
					buf_ctx_append(curr_ctx, "(");
					buf_ctx_append_trans(curr_ctx, curr->view.start, curr->view.length);
					buf_ctx_append(curr_ctx, ") Tj\n");
					curr = curr->next;
				}

				ctx->global_cursor -= BODY_OFFSET;

				if (ctx->global_cursor < 72) {
					buf_ctx_append(curr_ctx, "ET");

					Buf_Context *new_ctx = arena_alloc(arena, sizeof(Buf_Context));
					new_ctx->capacity = 10*1024;
					new_ctx->data = arena_alloc(arena, new_ctx->capacity);
					buf_ctx_append(new_ctx, "BT\n");

					page_ctx_append(ctx, new_ctx);
					curr_ctx = new_ctx;
					ctx->global_cursor = PAGE_HEIGHT;
				}
				break;
			}
			case NODE_PARAGRAPH: {
				Text_Span *curr = node->text;
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
					buf_ctx_append(curr_ctx, "(");
					buf_ctx_append_trans(curr_ctx, curr->view.start, curr->view.length);
					buf_ctx_append(curr_ctx, ") Tj\n");
					curr = curr->next;
				}

				ctx->global_cursor -= BODY_OFFSET;

				if (ctx->global_cursor < 72) {
					buf_ctx_append(curr_ctx, "ET");

					Buf_Context *new_ctx = arena_alloc(arena, sizeof(Buf_Context));
					new_ctx->capacity = 10*1024;
					new_ctx->data = arena_alloc(arena, new_ctx->capacity);
					buf_ctx_append(new_ctx, "BT\n");

					page_ctx_append(ctx, new_ctx);
					curr_ctx = new_ctx;
					ctx->global_cursor = PAGE_HEIGHT;
				}
				break;
			}
			default:
				break;
		}
        	if (node->child)
        		temp_render_node(arena, node->child, ctx);
        	node = node->next;
	}
}
