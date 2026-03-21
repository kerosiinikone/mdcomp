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

#define FORMAT_VERSION "%PDF-2.0\n%\xE2\xE3\xCF\xD3\n\n"
#define DEFAULT_OUTPUT_PATH "./output.pdf"

typedef enum {
	NODE_ROOT,
	NODE_HEADING,
	NODE_PARAGRAPH,
	NODE_LIST,
} NodeType;

typedef struct {
	const char *start;
	int length;
} StringView;

typedef struct Node {
	NodeType type;
	// idea: stringview linked list / array with regular text, bolded and italic as separate items?
	StringView text;

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

typedef struct {
	FILE *f;
	long offsets[256];
	size_t obj_count;
} PDF_Context;

// bool
int pdf_init(PDF_Context *ctx, const char *fp) {
	FILE *f = fopen(fp, "wb");
	if (f == NULL) return -1;

	ctx->f = f;
	ctx->offsets[0] = 0;
	ctx->obj_count = 1;

	fprintf(f, "%s", FORMAT_VERSION);
	return 0;
}

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
					if ((curr_node->text).start == NULL) {
						(curr_node->text).start = ptr;
					}
					(curr_node->text).length++;
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

	PDF_Context pctx = {0};
	if (pdf_init(&pctx, DEFAULT_OUTPUT_PATH) != 0) {
		return -1;
	}

	// doc catalog
	// 1 0 obj 
	// << 
	// /Pages 2 0 R
	// /Type /Catalog
	// >> 
	// endobj
	//

	int obj_id = pctx.obj_count++;
	pctx.offsets[obj_id] = ftell(pctx.f);
	fprintf(pctx.f, "%d 0 obj\n", obj_id);
	fprintf(pctx.f, "<< /Pages %d 0 R /Type /Catalog >>\n", obj_id + 1);
	fprintf(pctx.f, "endobj\n\n");

	// page tree
	// 2 0 obj
	// << 
	// /Count (page_count)
	// /Kids [pointer_to_pages] (3 0 R)
	// /Type /Pages
	// <<
	// endobj
	
	int tree = pctx.obj_count++;
	pctx.offsets[tree] = ftell(pctx.f);
	fprintf(pctx.f, "%d 0 obj\n", tree);
	fprintf(pctx.f, "<< /Count 1 /Kids [%d 0 R] /Type /Pages >>\n", tree + 1);
	fprintf(pctx.f, "endobj\n\n");

	//
	// individual pages
	// 3 0 obj
	// <<
	// /Contents (pointer to contents) 4 0 R
	// /Mediabox (page size)
	// /Parent (pointer to tree)
	// /Resources <<
	// 	/Font << ... >>
	// >>
	// /Type /Page
	// ...
	
	int page = pctx.obj_count++;
	pctx.offsets[page] = ftell(pctx.f);
	fprintf(pctx.f, "%d 0 obj\n", page);
	fprintf(pctx.f, "<<\n");
	fprintf(pctx.f, "	/Parent %d 0 R\n", tree);
	fprintf(pctx.f, "	/Contents %d 0 R\n", page + 2);
	fprintf(pctx.f, "	/Mediabox [0 0 612 792]\n");
	fprintf(pctx.f, "	/Resources << /Font << /F1 %d 0 R >> >>\n", page + 1);
	fprintf(pctx.f, ">>\n");
	fprintf(pctx.f, "/Type /Page\n");
	fprintf(pctx.f, "endobj\n\n");

	//
	// font
	// 5 0 obj
	// <<
	// 	/BaseFont /Helvetica
	// 	/Encoding ...
	// 	/Subtype ...
	// 	/Type /Font
	// >>
	// endobj
	//
	
	int font = pctx.obj_count++;
	pctx.offsets[font] = ftell(pctx.f);
	fprintf(pctx.f, "%d 0 obj\n", font);
	fprintf(pctx.f, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\n");
	fprintf(pctx.f, "endobj\n\n");

 
	// page content objects (as pointed to in the pages objects)
	// 4 0 obj
	// <<
	// 	/Length (stream len)
	// >>
	// stream
	// [contents produced!]
	// endstream
	// endobj
	//

	int content = pctx.obj_count++;
	pctx.offsets[content] = ftell(pctx.f);
	fprintf(pctx.f, "%d 0 obj\n", content);
	fprintf(pctx.f, "<< /Length %zu >>\n", ctx.offset);
	fprintf(pctx.f, "stream\n");
	fprintf(pctx.f, "%s\n", stream_buf);
	fprintf(pctx.f, "endstream\n");
	fprintf(pctx.f, "endobj\n\n");

	//
	// XREF table
	// xref
	// 0 6
	// 0000000000 65535 f
	// ...
	//

	size_t startxref = ftell(pctx.f);

	fprintf(pctx.f, "xref\n");
	fprintf(pctx.f, "0 %zu\n", pctx.obj_count);
	fprintf(pctx.f, "0000000000 65535 f \n");

	for (int i = 1; i < pctx.obj_count; i++) {
		fprintf(pctx.f, "%010ld 00000 n \n", pctx.offsets[i]);
	}

	fprintf(pctx.f, "trailer\n");
	fprintf(pctx.f, "<< /Size %zu /Root %d 0 R >>\n", pctx.obj_count, obj_id);
	fprintf(pctx.f, "startxref\n%ld\n%%%%EOF\n", startxref);
    
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
				buf_ctx_append(ctx, "/F1 24 Tf 1 0 0 1 72 %d Tm (%.*s) Tj\n", y, (node->text).length, (node->text).start);
				y -= 30;
				break;
			}
			case NODE_LIST: {
				buf_ctx_append(ctx, "/F1 12 Tf 1 0 0 1 72 %d Tm (- %.*s) Tj\n", y, (node->text).length, (node->text).start);
				y -= 15;
				break;
			}
			case NODE_PARAGRAPH: {
				buf_ctx_append(ctx, "/F1 12 Tf 1 0 0 1 72 %d Tm (%.*s) Tj\n", y, (node->text).length, (node->text).start);
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
