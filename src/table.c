#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/errno.h>
#include "table.h"

// 1st version of the database: table as an unsorted list of rows.
// Select * is easy and fast, as well as insertion when it happens in the end of the table
// However searching an element in the middle of the array is linear and deletion is very
// costly because all the elements after the one deleted need to be shifted to the left.
// A possible evolution would have been to sort the array by ids. Then we would have
// been able to reach log n with binary search. But deletion would still have been n.
// Now with the tree structure, every page is a node that contains pointers ot other pages
// and keys (ids ?).
// The node's content or body is stored in a flat way with its metadata because we need
// to make it persist in the disk file.

// Table, Pager and Row constants
const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);

const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

const uint32_t PAGE_SIZE = 4096; /* same as OS virtual memory page size */

// Common Node Header Layout
const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
const uint32_t NODE_TYPE_OFFSET = 0;
const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE + NODE_TYPE_OFFSET;
const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_SIZE + IS_ROOT_OFFSET;
const uint32_t COMMON_NODE_HEADER_SIZE = NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;

// Leaf Node Header Layout
const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE;

// Leaf Node Body Layout
const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_KEY_OFFSET = 0;
const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
const uint32_t LEAF_NODE_VALUE_OFFSET = LEAF_NODE_KEY_SIZE + LEAF_NODE_KEY_OFFSET;
const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
const uint32_t LEAF_NODE_SPACE_FORT_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_MAX_CELLS = LEAF_NODE_SPACE_FORT_CELLS / LEAF_NODE_CELL_SIZE; /* ~13 with some wasted space in the end of the page/node */

// Pointer to the number of cells in the node
uint32_t *leaf_node_num_cells(void *node)
{
    return node + LEAF_NODE_NUM_CELLS_OFFSET;
}
// Pointer to a particular cell (row) in the node
void *leaf_node_cell(void *node, uint32_t cell_num)
{
    return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}
// Pointer to the key of a specific cell
uint32_t *leaf_node_key(void *node, uint32_t cell_num)
{
    return leaf_node_cell(node, cell_num);
}
// Pointer to the value of a specific cell
void *leaf_node_value(void *node, uint32_t cell_num)
{
    return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
}
// anihilates the value the node pointer is pointing to
void initialize_leaf_node(void *node) { *leaf_node_num_cells(node) = 0; }

// Magics of C, placing into a destination memory space, related values next
// to each other, without even caring about their types.
void serialize_row(Row *source, void *destination)
{
    // strncpy instead of memcpy for the char arrays to make sure that bytes are initialized
    // so it's more readable in the hex editor.
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
    memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
}

void deserialize_row(void *source, Row *destination)
{
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

Cursor *table_start(Table *table)
{
    Cursor *cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = table->root_page_num;
    cursor->cell_num = 0;

    void *root_node = get_page(table->pager, table->root_page_num);
    // aren't we making the assumption that root_node is a leaf node ?
    uint32_t num_cells = *leaf_node_num_cells(root_node);
    cursor->end_of_table = (num_cells == 0);

    return cursor;
}

Cursor *table_end(Table *table)
{
    Cursor *cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    void *root_node = get_page(table->pager, table->root_page_num);
    // aren't we making the assumption that root_node is a leaf node ?
    uint32_t num_cells = *leaf_node_num_cells(root_node);
    cursor->cell_num = num_cells;
    cursor->end_of_table = true;

    return cursor;
}

void cursor_advance(Cursor *cursor)
{
    void *node = get_page(cursor->table->pager, cursor->page_num);
    cursor->cell_num += 1;
    if (cursor->cell_num >= (*leaf_node_num_cells(node)))
    {
        cursor->end_of_table = true;
    }
}

// returns a pointer to the position described by the cursor
void *cursor_value(Cursor *cursor)
{
    void *page = get_page(cursor->table->pager, cursor->page_num);
    if (page == NULL)
    {
        return NULL;
    }
    return leaf_node_value(page, cursor->cell_num);
}

void leaf_node_insert(Cursor *cursor, uint32_t key, Row *value)
{
    void *node = get_page(cursor->table->pager, cursor->page_num);

    uint32_t node_num_cells = *leaf_node_num_cells(node);
    if (node_num_cells >= LEAF_NODE_MAX_CELLS)
    {
        // Node full
        printf("Need to implement splitting a leaf node.\n");
        exit(EXIT_FAILURE);
    }

    if (cursor->cell_num < node_num_cells)
    {
        // Make room for new cell
        for (uint32_t i = node_num_cells; i > cursor->cell_num; i--)
        {
            memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1),
                   LEAF_NODE_CELL_SIZE);
        }
    }
    // ex: node_num_cells = 10, cell_num = 7
    // so position in cells ranging from [[0, 9] * LEAF_NODE_CELL_SIZE]
    // In order to insert a cell at position 7, we run the loop from 10 to 8
    // effectively placing cells 10<-9, 9<-8, 8<-7
    // if cursor->cell_num = node_num_cells <=> 8 == 8, the 8 cell slot is empty because [0, 7] are occupied

    *(leaf_node_num_cells(node)) += 1;
    *(leaf_node_key(node, cursor->cell_num)) = key;
    serialize_row(value, leaf_node_value(node, cursor->cell_num));
}

// Attempts to get the page from pager (cache).
// if miss, allocates memory for this page and returns it.
// writing to the disk file does not happen here yet.
void *get_page(Pager *pager, uint32_t page_num)
{
    if (page_num > TABLE_MAX_PAGES)
    {
        printf("Tried to fetch page number out of bounds. %d > %d\n", page_num,
               TABLE_MAX_PAGES);
        return NULL;
    }

    if (pager->pages[page_num] == NULL)
    {
        // Cache miss. Allocate memory and load from file.
        void *page = malloc(PAGE_SIZE);
        pager->pages[page_num] = page;

        // why ?
        if (page_num >= pager->num_pages)
        {
            pager->num_pages = page_num + 1;
        }

        uint32_t file_num_pages = pager->file_length / PAGE_SIZE;

        // We have the data for the given page in the file, so we load the cache page with it
        if (page_num <= file_num_pages)
        {
            // changes the location of the read/write pointer of the file descriptor
            lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
            // attempts to read from it
            ssize_t bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
            if (bytes_read == -1)
            {
                printf("Error reading file: %d\n", errno);
                return NULL;
            }
        }
    }

    return pager->pages[page_num];
}

/* Opening the database file
initializing a pager data structure
initializing a table data structure */
Table *db_open(const char *filename)
{
    Pager *pager = pager_open(filename);

    Table *table = malloc(sizeof(Table));
    table->pager = pager;
    // why would that be 0 ?
    table->root_page_num = 0;

    if (pager->num_pages == 0)
    {
        // New database file. Initialize page 0 as leaf node.
        void *root_node = get_page(pager, 0);
        initialize_leaf_node(root_node);
    }

    return table;
}

// Opens the database file and keeps track of its size. It also initializes the page cache to all NULLs.
Pager *pager_open(const char *filename)
{
    int fd = open(filename,
                  O_RDWR |     // Read/Write mode
                      O_CREAT, // Create file if it does not exist
                  S_IWUSR |    // User write permission
                      S_IRUSR  // User read permission
    );

    if (fd == -1)
    {
        printf("Unable to open file\n");
        exit(EXIT_FAILURE);
    }

    /* SEEK_END: set file offset to EndOfFile plus offset */
    off_t file_length = lseek(fd, 0, SEEK_END);

    Pager *pager = malloc(sizeof(Pager));
    pager->file_descriptor = fd;
    pager->file_length = file_length;
    pager->num_pages = (file_length / PAGE_SIZE);

    if (file_length % PAGE_SIZE != 0)
    {
        printf("Db file is not a whole number of pages. Corrupt file.\n");
        exit(EXIT_FAILURE);
    }

    // cache
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++)
    {
        pager->pages[i] = NULL;
    }

    return pager;
}

/*
For now, we’ll wait to flush the cache to disk until the user closes the connection to the database:
• flushes the page cache to disk
• closes the database file
• frees the memory for the Pager and Table data structures */
void db_close(Table *table)
{
    Pager *pager = table->pager;

    for (uint32_t num_page = 0; num_page < pager->num_pages; num_page++)
    {
        if (pager->pages[num_page] == NULL)
        {
            continue; /* not overriding file pages */
        }
        pager_flush(pager, num_page);
    }

    if ((close(pager->file_descriptor)) == -1)
    {
        printf("Error closing db file.\n");
        exit(EXIT_FAILURE);
    }
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++)
    {
        void *page = pager->pages[i];
        if (page)
        {
            free(page);
            pager->pages[i] = NULL;
        }
    }
    free(pager);
    free(table);
}

void pager_flush(Pager *pager, uint32_t page_num)
{
    if (pager->pages[page_num] == NULL)
    {
        printf("Tried to flush null page\n");
        exit(EXIT_FAILURE);
    }

    // places the file pointer to where we need to write
    off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);

    if (offset == -1)
    {
        printf("Error seeking: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    ssize_t bytes_written =
        write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);

    if (bytes_written == -1)
    {
        printf("Error writing: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}