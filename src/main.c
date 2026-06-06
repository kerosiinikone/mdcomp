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
#define OBSIDIAN_TAG_LINES 6

int getopt(int ___argc, char *const *___argv, const char *__shortopts);

int main(int argc, char *argv[]) {
  struct stat st;
  int opt;

  const char *input_path = NULL;
  const char *output_path = DEFAULT_OUTPUT_PATH;
  bool skip_obsidian = false;

  while ((opt = getopt(argc, argv, "i:o:s")) != -1) {
    switch (opt) {
    case 'i':
      input_path = optarg;
      break;
    case 'o':
      output_path = optarg;
      break;
    case 's':
      skip_obsidian = true;
      break;
    default:
      return 1;
    }
  }

  if (input_path == NULL) {
    return 1;
  }

  int fd = open(input_path, O_RDONLY);

  if (fd < 0)
    return 1;

  if (fstat(fd, &st) == -1) {
    close(fd);
    return 1;
  }
  size_t filesize = st.st_size;

  char *file_data = mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
  if (file_data == MAP_FAILED) {
    close(fd);
    return 1;
  }

  if (skip_obsidian) {
    int count = 0;
    char *end = file_data + filesize;
    while (file_data < end && count < OBSIDIAN_TAG_LINES) {
      if (*file_data == '\n')
        count++;
      file_data++;
    }
  }

  Arena arena = arena_create(ARENA_SIZE);

  Parser_Context *parser = parser_create(&arena);
  if (parser == NULL) {
    arena_destroy(&arena);
    munmap(file_data, filesize);
    close(fd);
    return 1;
  }

  Node *root = parser_parse(parser, file_data, filesize);
  if (root == NULL) {
    arena_destroy(&arena);
    munmap(file_data, filesize);
    close(fd);
    return 1;
  }

  PDF_Context *pdf = pdf_create(&arena);
  if (pdf == NULL) {
    arena_destroy(&arena);
    munmap(file_data, filesize);
    close(fd);
    return 1;
  }
  if (!pdf_init(pdf, output_path)) {
    arena_destroy(&arena);
    munmap(file_data, filesize);
    close(fd);
    return 1;
  }

  Render_Context *render = render_create(&arena, MAX_PAGES);
  if (render == NULL) {
    arena_destroy(&arena);
    munmap(file_data, filesize);
    close(fd);
    return 1;
  }

  if (!render_document(render, pdf, root)) {
    arena_destroy(&arena);
    munmap(file_data, filesize);
    close(fd);
    return 1;
  }

  arena_destroy(&arena);
  munmap(file_data, filesize);
  close(fd);

  return 0;
}
