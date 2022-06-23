#include <stdlib.h>

#ifndef TABLE_HEADER
#define TABLE_HEADER

#define TABLE_MAX_PAGES 100
#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
#define size_of_attribute(Struct, Attribute) sizeof(((Struct *)0)->Attribute)

typedef struct
{
  uint32_t num_rows;
  void *pages[TABLE_MAX_PAGES];
} Table;

typedef struct
{
  uint32_t id;                             /* 32b = 4B */
  char username[COLUMN_USERNAME_SIZE + 1]; /* 32 * 1B + null terminator (1B) */
  char email[COLUMN_EMAIL_SIZE + 1];       /* 255 * 1B + null terminator (1B) */
} Row;                                     /* 32(+1) + 4 + 255(+1) = 293B */

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

Table *new_table();
void free_table(Table *table);

void serialize_row(Row *source, void *destination);
void deserialize_row(void *source, Row *destination);
void *row_slot(Table *table, uint32_t row_num);

#endif