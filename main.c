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
} BufContext;

void buf_ctx_append(BufContext *ctx, char *fmt, ...) {
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
	// zero excess
	size_t align = (size + 7) & ~7;
	if (arena->offset + align > arena->cap) {
		// realloc
		exit(1);
	};
	// curr
	void *ptr = arena->data + arena->offset;
	arena->offset += align;
	memset(ptr, 0, align);
	return ptr;
}

void arena_destroy(Arena *arena) {
	free(arena->data);
}

void render_node(Node *node, BufContext *ctx, int y);

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
	BufContext ctx = {
		.data = stream_buf,
		.capacity = sizeof(stream_buf),
		.offset = 0
	};

	buf_ctx_append(&ctx, "BT\n");
	render_node(root->child, &ctx, 750);
	buf_ctx_append(&ctx, "ET\n");

	printf("%s", stream_buf);

	arena_destroy(&arena);
	munmap(file_data, filesize);
	close(fd);
	return 0;
}

void render_node(Node *node, BufContext *ctx, int y) {
	if (node == NULL) return;
	while (node)
	{
		switch (node->type) {
			case NODE_HEADING: {
				buf_ctx_append(ctx, "/F1 24 Tf 72 %d Td (%.*s) Tj\n", y, (node->text).length, (node->text).start);
				y -= 30;
				break;
			}
			case NODE_LIST: {
				buf_ctx_append(ctx, "/F1 12 Tf 72 %d Td (- %.*s) Tj\n", y, (node->text).length, (node->text).start);
				y -= 15;
				break;
			}
			case NODE_PARAGRAPH: {
				buf_ctx_append(ctx, "/F1 12 Tf 72 %d Td (%.*s) Tj\n", y, (node->text).length, (node->text).start);
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

int main2()
{
	FILE *fptr = fopen("output.pdf", "wb");
	if (!fptr) return -1;

	char content_string[512];
	char user_content[] = "Hello Note";
	uint64_t bytes_written = 0;
	sprintf(content_string, "BT\n  /F1 18 Tf\n  50 700 Td\n  (%s) Tj\nET", user_content);

	char header[64];
	sprintf(header, "%s", FORMAT_VERSION);
	bytes_written += fwrite(&header, sizeof(char), strlen(header), fptr);

	char root[] = "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n\n";
	bytes_written += fwrite(&root, sizeof(char), strlen(root), fptr);

	char tree[] = "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n\n";
	bytes_written += fwrite(&tree, sizeof(char), strlen(tree), fptr);

	char page[] = "3 0 obj\n<<\n  /Type /Page\n  /Parent 2 0 R\n  /MediaBox [0 0 612 792]\n  /Resources << /Font << /F1 4 0 R >> >>\n  /Contents 5 0 R\n>>\nendobj\n\n";
	bytes_written += fwrite(&page, sizeof(char), strlen(page), fptr);

	char fonts_res[] = "4 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n\n";
	bytes_written += fwrite(&fonts_res, sizeof(char), strlen(fonts_res), fptr);

	char contents[1024];
	sprintf(contents, "5 0 obj\n<< /Length %ld >>\nstream\n%s\nendstream\nendobj\n\n", strlen(content_string), content_string);
	bytes_written += fwrite(contents, sizeof(char), strlen(contents), fptr);

	// TODO: compute the offset automatically while writing
	char xref[] = "xref\n0 6\n0000000000 65535 f\n0000000015 00000 n\n0000000064 00000 n\n0000000122 00000 n\n0000000262 00000 n\n0000000334 00000 n\n";
	fwrite(&xref, sizeof(char), strlen(xref), fptr);

	char trailer[512];
	sprintf(trailer, "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n%llu\n", (unsigned long long)bytes_written);
	fwrite(trailer, sizeof(char), strlen(xref), fptr);
	fputs("%%EOF\n", fptr);
	return 0;
}
