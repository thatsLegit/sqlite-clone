#include <stdlib.h>
#include <string.h>
#include "table.h"

#ifndef USER_INPUT_HEADER
#define USER_INPUT_HEADER

typedef struct
{
  char *buffer;
  size_t buffer_length;
  ssize_t input_length;
} InputBuffer;

InputBuffer *new_input_buffer();
void print_prompt();
void print_row(Row *row);
void read_input(InputBuffer *input_buffer);
void close_input_buffer(InputBuffer *input_buffer);

#endif