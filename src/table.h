#include <stdlib.h>
#include <stdbool.h>

#ifndef TABLE_HEADER
#define TABLE_HEADER

// general purpose macros
#define size_of_attribute(Struct, Attribute) sizeof(((Struct *)0)->Attribute)

// table attributes / rows
#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255

extern const uint32_t ID_SIZE;
extern const uint32_t USERNAME_SIZE;
extern const uint32_t EMAIL_SIZE;

extern const uint32_t ID_OFFSET;
extern const uint32_t USERNAME_OFFSET;
extern const uint32_t EMAIL_OFFSET;
extern const uint32_t ROW_SIZE;

typedef struct
{
  uint32_t id;                             /* 32b = 4B */
  char username[COLUMN_USERNAME_SIZE + 1]; /* 32 * 1B + null terminator (1B) = 32B */
  char email[COLUMN_EMAIL_SIZE + 1];       /* 255 * 1B + null terminator (1B) = 255B */
} Row;                                     /* 32(+1) + 4 + 255(+1) = 293B */

// Table and Pages
#define TABLE_MAX_PAGES 100
extern const uint32_t PAGE_SIZE;

// Structure used by a table to access the file of the table (file descriptor).
// Or to load the data from memory (pager)
typedef struct
{
  int file_descriptor;
  uint32_t file_length;
  uint32_t num_pages;
  void *pages[TABLE_MAX_PAGES];
} Pager;

typedef struct
{
  // A btree is identified by its root node page number, so the table object needs to keep track of that
  uint32_t root_page_num;
  Pager *pager;
} Table;

// Used for search, insertion and every other operation on the table
typedef struct
{
  Table *table;
  uint32_t page_num;
  uint32_t cell_num;
  bool end_of_table; // Indicates a position one past the last element
} Cursor;

// Nodes
typedef enum
{
  NODE_INTERNAL,
  NODE_LEAF
} NodeType;

// Common Node Header Layout
extern const uint32_t NODE_TYPE_SIZE;
extern const uint32_t NODE_TYPE_OFFSET;
extern const uint32_t IS_ROOT_SIZE;
extern const uint32_t IS_ROOT_OFFSET;
extern const uint32_t PARENT_POINTER_SIZE;
extern const uint32_t PARENT_POINTER_OFFSET;
extern const uint32_t COMMON_NODE_HEADER_SIZE;

// Leaf Node Header Layout
extern const uint32_t LEAF_NODE_NUM_CELLS_SIZE;
extern const uint32_t LEAF_NODE_NUM_CELLS_OFFSET;
extern const uint32_t LEAF_NODE_HEADER_SIZE;

// Leaf Node Body Layout
extern const uint32_t LEAF_NODE_KEY_SIZE;
extern const uint32_t LEAF_NODE_KEY_OFFSET;
extern const uint32_t LEAF_NODE_VALUE_SIZE;
extern const uint32_t LEAF_NODE_VALUE_OFFSET;
extern const uint32_t LEAF_NODE_CELL_SIZE;
extern const uint32_t LEAF_NODE_SPACE_FOR_CELLS;
extern const uint32_t LEAF_NODE_MAX_CELLS;
extern const uint32_t LEAF_NODE_RIGHT_SPLIT_COUNT;
extern const uint32_t LEAF_NODE_LEFT_SPLIT_COUNT;

/*
  Abstraction. Represents a location in the table. Things you might want to do with cursors :
  • Create a cursor at the beginning of the table
  • Create a cursor at the end of the table
  • Access the row the cursor is pointing to
  • Advance the cursor to the next row

  Those are the behaviors we’re going to implement now. Later, we will also want to:
  • Delete the row pointed to by a cursor
  • Modify the row pointed to by a cursor
  • Search a table for a given ID, and create a cursor pointing to the row with that ID
*/

void print_constants();
void print_leaf_node(void *node);

Cursor *table_start(Table *table);
void *cursor_value(Cursor *cursor);
void cursor_advance(Cursor *cursor);
Cursor *table_find(Table *table, u_int32_t key_to_insert);

uint32_t *leaf_node_num_cells(void *node);
void *leaf_node_cell(void *node, uint32_t cell_num);
uint32_t *leaf_node_key(void *node, uint32_t cell_num);
void *leaf_node_value(void *node, uint32_t cell_num);
void initialize_leaf_node(void *node);
void leaf_node_insert(Cursor *cursor, uint32_t key, Row *value);
void leaf_node_split_and_insert(Cursor *cursor, uint32_t key, Row *value);
NodeType get_node_type(void *node);
void set_node_type(void *node, NodeType type);
Cursor *leaf_node_find(Table *table, u_int32_t page_num, u_int32_t key_to_insert);

Pager *pager_open(const char *filename);
void *get_page(Pager *pager, uint32_t page_num);
uint32_t get_unused_page_num(Pager *pager);

Table *db_open(const char *filename);
void db_close(Table *table);
void pager_flush(Pager *pager, uint32_t page_num);

void serialize_row(Row *source, void *destination);
void deserialize_row(void *source, Row *destination);

#endif