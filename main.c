#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "arena.h"
#include "parser.h"
#include "pdf.h"
#include "render.h"

#define DEFAULT_OUTPUT_PATH "./output.pdf"

int main(int argc, char *argv[]) {
  struct stat st;

  if (argc < 2 || argc > 2)
    return -1;

  int fd = open(argv[1], O_RDONLY);

  if (fd < 0)
    return -1;

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

  Parser_Context *parser = parser_create(&arena);
  Node *root = parser_parse(parser, file_data, filesize);

  PDF_Context pdf;
  if (!pdf_init(&pdf, DEFAULT_OUTPUT_PATH)) {
    return -1;
  }

  Render_Context *render = render_create(&arena, 1024 * 1024);
  render_document(render, &pdf, root);

  arena_destroy(&arena);
  munmap(file_data, filesize);
  close(fd);

  return 0;
}
