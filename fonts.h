#ifndef FONTS_H
#define FONTS_H

#include <stddef.h>

size_t get_char_width(unsigned char c);

size_t calculate_text_width(const char *text, size_t length, size_t font_size);

#endif
