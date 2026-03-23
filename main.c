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

typedef enum {
	NODE_ROOT,
	NODE_HEADING,
	NODE_PARAGRAPH,
	NODE_LIST,
} Node_Type;

typedef struct {
	const char *start;
	int length;
} String_View;

typedef struct Node {
	Node_Type type;
	// idea: stringview linked list / array with regular text, bolded and italic as separate items?
	String_View text;

	struct Node *child;
	struct Node *next;
} Node;

typedef struct {
    char *data;
    size_t capacity;
    size_t offset;
} Buf_Context;

void buf_ctx_append(Buf_Context *ctx, char *fmt, ...) {
	if (ctx->offset >= ctx->capacity - 1) return;

	va_list args;
	va_start(args, fmt);
	int written = vsnprintf(ctx->data + ctx->offset, ctx->capacity - ctx->offset, fmt, args);
	va_end(args);

	if (written > 0) {
		ctx->offset += written;
        	if (ctx->offset >= ctx->capacity) {
        		ctx->offset = ctx->capacity - 1; 
        	}
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
	size_t align = (size + 7) & ~7;
	if (arena->offset + align > arena->cap) {
		exit(1);
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

	Node *root = arena_alloc(&arena, sizeof(Node));
	Node *curr_node = root;
	Node *last_root_child = root->child;

	bool is_newline = true;

	char *ptr = file_data;
	char *end = file_data + st.st_size;

	while (ptr < end) 
	{
		switch (*ptr)
		{
			case '\n': {
				is_newline = true;
				break;
			}
			case '#': {
				if (!is_newline) {
					goto paragraph;
				}
				if (ptr + 1 >= end) {
					break;
            			}
				if (!isspace((unsigned char)ptr[1])) {
					goto paragraph;
				}
				Node *heading = arena_alloc(&arena, sizeof(Node));
				heading->type = NODE_HEADING;
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
				ptr++;
				curr_node = heading;
				is_newline = false;
				break;
			}
			case '-': {
				if (!is_newline) {
					goto paragraph;
				}
				if (ptr + 1 >= end) {
					break;
            			}
				if (!isspace((unsigned char)ptr[1])) {
					goto paragraph;
				}
				Node *list = arena_alloc(&arena, sizeof(Node));
				list->type = NODE_LIST;
				if (curr_node->type == NODE_ROOT || curr_node->type == NODE_HEADING) {
					curr_node->child = list;
				} else {
					curr_node->next = list;
				}
				ptr++;

				curr_node = list;
				is_newline = false;
				break;
			}
			default: paragraph: {
				if (!is_newline) {
					if (curr_node->text.start == NULL) {
						(curr_node->text).start = ptr;
					}
					curr_node->text.length++;
				} else {
					Node *p = arena_alloc(&arena, sizeof(Node));
					p->type = NODE_PARAGRAPH;
					p->text.start = ptr;
					p->text.length++;
					if (curr_node->type == NODE_PARAGRAPH) {
						curr_node->next = p;
					} else {
						curr_node->child = p;
					}
					curr_node = p;
					is_newline = false;
				}
				break;
			}
		}
		ptr++;
	}

	char stream_buf[1024] = {0};
	Buf_Context ctx = {
		.data = stream_buf,
		.capacity = sizeof(stream_buf),
		.offset = 0
	};

	buf_ctx_append(&ctx, "BT\n");
	render_node(root->child, &ctx, 750);
	buf_ctx_append(&ctx, "ET");
	//
	// DEBUG
	int kids[] = {0};
	kids[0] = 3;

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
		.page = { 5, 612, 792, tree.id, 4 },
	};
	PDF_Object font = {
		.id = 4,
		.type = PDF_FONT,
	};
	PDF_Object contents = {
		.id = 5,
		.type = PDF_CONTENT,
		.content = { ctx.offset, stream_buf }
	};
	 
	pdf_obj_write(&pctx, &cat);
	pdf_obj_write(&pctx, &tree);
	pdf_obj_write(&pctx, &page);
	pdf_obj_write(&pctx, &font);
	pdf_obj_write(&pctx, &contents);

	pdf_xref_table_write(&pctx);
	pdf_trailer_write(&pctx, cat.id);
	fclose(pctx.f);

	arena_destroy(&arena);
	munmap(file_data, filesize);
	close(fd);
	return 0;
}

void render_node(Node *node, Buf_Context *ctx, int y) {
	if (node == NULL) return;
	while (node)
	{
		switch (node->type) {
			case NODE_HEADING: {
				buf_ctx_append(ctx, "/F1 24 Tf 1 0 0 1 72 %d Tm (%.*s) Tj\n", y, node->text.length, node->text.start);
				// TODO: define
				y -= 30;
				break;
			}
			case NODE_LIST: {
				buf_ctx_append(ctx, "/F1 12 Tf 1 0 0 1 72 %d Tm (- %.*s) Tj\n", y, node->text.length, node->text.start);
				// TODO: define
				y -= 15;
				break;
			}
			case NODE_PARAGRAPH: {
				buf_ctx_append(ctx, "/F1 12 Tf 1 0 0 1 72 %d Tm (%.*s) Tj\n", y, node->text.length, node->text.start);
				// TODO: define
				y -= 15;
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
