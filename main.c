#include <stdio.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#define FORMAT_VERSION "%PDF-2.0\n%µ¶\n\n"

// Root, heading 1 -> text, etc as a tree -> allocate the root at thee beginning (arean)
typedef enum {
	NODE_ROOT,
	NODE_HEADING,
	NODE_PARAGRAPH,
	NODE_TEXT /* for heading and paragraph */
} NodeType;

typedef struct {
	const char *start;
	int length;
} StringView;

typedef struct Node {
	NodeType type;

	// union for either a placeholder (root) or a text field
	// for now -> assume all nodes have text
	StringView text;

	// child
	struct Node *first_child;
	// next
	struct Node *next_sibling;
} Node;

void _traverse(Node *root, int depth)
{
    if (root == NULL) return;
    while (root)
    {
	for (int i = 0; i < depth; i++) { printf("  "); }
	printf("%d\n", root->type);
        if (root->first_child)
            _traverse(root->first_child, depth+1);
        root = root->next_sibling;
    }
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

	printf("File size: %zu\n", filesize);

	Node *root = malloc(sizeof(Node));
	Node *curr_node = root;

	bool is_newline = true;

	char *ptr = file_data;
	char *end = file_data + st.st_size;
	while (ptr < end) 
	{
		switch (*ptr)
		{
			// Skip whitespace?
			case ' ':
			break;
			case '\n':
			is_newline = true;
			break;
			// For now -> only H1s
			case '#': {
				if (!is_newline) {
					break;
				}
				Node *heading = malloc(sizeof(Node));
				heading->type = NODE_HEADING;
				// init text field!
				if (curr_node->type == NODE_ROOT) {
					curr_node->first_child = heading;
					curr_node = heading;
				} else if (curr_node->type == NODE_HEADING) {
					curr_node->next_sibling = heading;
					curr_node = heading;
				} else {}
				// If current node is neither (p) -> root->first_child (if exists) ... ?
				// putchar(*ptr);
				is_newline = false;
				break;
			}
			default: {
				if (!is_newline) {
					// current node's (except root) text
				} else {
					if (curr_node->type == NODE_PARAGRAPH) {
						// next_sibling
					} else {
						// new paragraph node -> onto curr_node->first_child
						// set StringView to start at the new pos - first char (ptr)
					}
				}
				break;
			}
		}
		ptr++;
	}

	// Debug
	_traverse(root, 0);

	// Debug
	free(root->first_child);
	free(root);

	munmap(file_data, filesize);
	close(fd);
	return 0;
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
}
