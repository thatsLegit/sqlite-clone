#include <stdlib.h>
#include <stdbool.h>

#ifndef TABLE_HEADER
#define TABLE_HEADER

#define TABLE_MAX_PAGES 100
#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
#define size_of_attribute(Struct, Attribute) sizeof(((Struct *)0)->Attribute)

// Structure used by a table to access the file of the table (file descriptor).
// Or to load the data from memory (pager)
typedef struct
{
  int file_descriptor;
  uint32_t file_length;
  void *pages[TABLE_MAX_PAGES];
} Pager;
// num_rows always indicates the total number of rows that comprises the table
typedef struct
{
  uint32_t num_rows;
  Pager *pager;
} Table;

typedef struct
{
  uint32_t id;                             /* 32b = 4B */
  char username[COLUMN_USERNAME_SIZE + 1]; /* 32 * 1B + null terminator (1B) = 32B */
  char email[COLUMN_EMAIL_SIZE + 1];       /* 255 * 1B + null terminator (1B) = 255B */
} Row;                                     /* 32(+1) + 4 + 255(+1) = 293B */

/*
    Abstraction. Represents a location in the table. Things you might want to do with cursors:

• Create a cursor at the beginning of the table
• Create a cursor at the end of the table
• Access the row the cursor is pointing to
• Advance the cursor to the next row

    Those are the behaviors we’re going to implement now. Later, we will also want to:

• Delete the row pointed to by a cursor
• Modify the row pointed to by a cursor
• Search a table for a given ID, and create a cursor pointing to the row with that ID */
typedef struct
{
  Table *table;
  uint32_t row_num;
  bool end_of_table; // Indicates a position one past the last element
} Cursor;

Cursor *table_start(Table *table);
Cursor *table_end(Table *table);
void cursor_advance(Cursor *cursor);

extern const uint32_t ID_SIZE;
extern const uint32_t USERNAME_SIZE;
extern const uint32_t EMAIL_SIZE;

extern const uint32_t ID_OFFSET;
extern const uint32_t USERNAME_OFFSET;
extern const uint32_t EMAIL_OFFSET;
extern const uint32_t ROW_SIZE;

extern const uint32_t PAGE_SIZE;
extern const uint32_t ROWS_PER_PAGE;
extern const uint32_t TABLE_MAX_ROWS;

Table *db_open(const char *filename);
Pager *pager_open(const char *filename);

void *cursor_value(Cursor *cursor);
void *get_page(Pager *pager, uint32_t page_num);

void serialize_row(Row *source, void *destination);
void deserialize_row(void *source, Row *destination);

void db_close(Table *table);
void pager_flush(Pager *pager, uint32_t page_num, uint32_t size);

#endif