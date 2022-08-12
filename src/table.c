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

// Internal Node Header Layout
const uint32_t INTERNAL_NODE_NUM_KEYS_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_NUM_KEYS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t INTERNAL_NODE_RIGHTMOST_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_RIGHTMOST_CHILD_OFFSET =
    INTERNAL_NODE_NUM_KEYS_OFFSET + INTERNAL_NODE_NUM_KEYS_SIZE;
const uint32_t INTERNAL_NODE_HEADER_SIZE =
    COMMON_NODE_HEADER_SIZE + INTERNAL_NODE_NUM_KEYS_SIZE + INTERNAL_NODE_RIGHTMOST_CHILD_SIZE;

// Internal Node Body Layout
/*
    The body is an array of cells where each cell contains a child pointer and a key.
    Every key should be the maximum key contained in the child to its left.
*/
const uint32_t INTERNAL_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CELL_SIZE =
    INTERNAL_NODE_CHILD_SIZE + INTERNAL_NODE_KEY_SIZE;

// Leaf Node Header Layout
const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_NEXT_LEAF_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NEXT_LEAF_OFFSET = LEAF_NODE_NUM_CELLS_OFFSET + LEAF_NODE_NUM_CELLS_SIZE;
const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE + LEAF_NODE_NEXT_LEAF_SIZE;

// Leaf Node Body Layout
const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_KEY_OFFSET = 0;
const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
const uint32_t LEAF_NODE_VALUE_OFFSET = LEAF_NODE_KEY_SIZE + LEAF_NODE_KEY_OFFSET;
const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_MAX_CELLS = LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE; /* ~13 with some wasted space in the end of the page/node */

// Used for splitting the leaf node
const uint32_t LEAF_NODE_RIGHT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) / 2;
const uint32_t LEAF_NODE_LEFT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) - LEAF_NODE_RIGHT_SPLIT_COUNT;

// Pointer to the number of cells in the node
uint32_t *leaf_node_num_cells(void *node)
{
    return node + LEAF_NODE_NUM_CELLS_OFFSET;
}
// Pointer to the number of the immediate right sibling of the given node
uint32_t *leaf_node_next_leaf(void *node)
{
    return node + LEAF_NODE_NEXT_LEAF_OFFSET;
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
void initialize_leaf_node(void *node)
{
    set_node_type(node, NODE_LEAF);
    set_node_root(node, false);
    *leaf_node_num_cells(node) = 0;
    *leaf_node_next_leaf(node) = 0; // 0 represents no sibling
}

NodeType get_node_type(void *node)
{
    // We have to cast to uint8_t first to ensure it’s serialized as a single byte.
    uint8_t value = *((uint8_t *)(node + NODE_TYPE_OFFSET));
    return (NodeType)value;
}

void set_node_type(void *node, NodeType type)
{
    u_int8_t value = (uint8_t)type;
    uint8_t *type_slot_ptr = (uint8_t *)(node + NODE_TYPE_OFFSET);
    *type_slot_ptr = value;
}

bool is_node_root(void *node)
{
    uint8_t value = *(uint8_t *)(node + IS_ROOT_OFFSET);
    return (bool)value;
}

void set_node_root(void *node, bool is_root)
{
    uint8_t value = (uint8_t)is_root;
    uint8_t *is_root_slot = (uint8_t *)(node + IS_ROOT_OFFSET);
    *is_root_slot = value;
}

// anihilates the value the node pointer is pointing to
void initialize_internal_node(void *node)
{
    set_node_type(node, NODE_INTERNAL);
    set_node_root(node, false);
    *internal_node_num_keys(node) = 0;
}
uint32_t *internal_node_num_keys(void *node)
{
    return node + INTERNAL_NODE_NUM_KEYS_OFFSET;
}
// returns the page number of the right most child (the one that has the highest key)
uint32_t *internal_node_rightmost_child(void *node)
{
    return node + INTERNAL_NODE_RIGHTMOST_CHILD_OFFSET;
}
// returns the whole cell (id/page number) at index key_num
uint32_t *internal_node_cell(void *node, uint32_t cell_num)
{
    return node + INTERNAL_NODE_HEADER_SIZE + cell_num * INTERNAL_NODE_CELL_SIZE;
}
// returns the key (id) of the cell at index key_num
uint32_t *internal_node_key(void *node, uint32_t key_num)
{
    return internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
}
// returns the page number (or key) of the child at a given index in the node
uint32_t *internal_node_child(void *node, uint32_t child_num)
{
    uint32_t num_keys = *internal_node_num_keys(node);
    if (child_num > num_keys)
    {
        printf("Tried to access child_num %d > num_keys %d\n", child_num, num_keys);
        exit(EXIT_FAILURE);
    }
    else if (child_num == num_keys)
        return internal_node_rightmost_child(node);
    else
        return internal_node_cell(node, child_num);
}

// returns the highest key in a given node
uint32_t get_node_max_key(void *node)
{
    if (get_node_type(node) == NODE_INTERNAL)
    {
        uint32_t last_cell_index = *internal_node_num_keys(node) - 1;
        return *internal_node_key(node, last_cell_index);
    }
    else
    {
        uint32_t last_cell_index = *leaf_node_num_cells(node) - 1;
        return *leaf_node_key(node, last_cell_index);
    }
}

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

void print_constants()
{
    printf("ROW_SIZE: %d\n", ROW_SIZE);
    printf("COMMON_NODE_HEADER_SIZE: %d\n", COMMON_NODE_HEADER_SIZE);
    printf("LEAF_NODE_HEADER_SIZE: %d\n", LEAF_NODE_HEADER_SIZE);
    printf("LEAF_NODE_CELL_SIZE: %d\n", LEAF_NODE_CELL_SIZE);
    printf("LEAF_NODE_SPACE_FOR_CELLS: %d\n", LEAF_NODE_SPACE_FOR_CELLS);
    printf("LEAF_NODE_MAX_CELLS: %d\n", LEAF_NODE_MAX_CELLS);
}

void indent(uint32_t level)
{
    for (uint32_t i = 0; i < level; i++)
    {
        printf("  ");
    }
}

void print_tree(Pager *pager, uint32_t page_num, uint32_t indentation_level)
{
    void *node = get_page(pager, page_num);
    uint32_t num_keys, child;

    switch (get_node_type(node))
    {
    case (NODE_LEAF):
        num_keys = *leaf_node_num_cells(node);
        indent(indentation_level);
        printf("- leaf (size %d)\n", num_keys);
        for (uint32_t i = 0; i < num_keys; i++)
        {
            indent(indentation_level + 1);
            printf("- %d\n", *leaf_node_key(node, i));
        }
        break;
    case (NODE_INTERNAL):
        num_keys = *internal_node_num_keys(node);
        indent(indentation_level);
        printf("- internal (size %d)\n", num_keys);
        for (uint32_t i = 0; i < num_keys; i++)
        {
            child = *internal_node_child(node, i);
            print_tree(pager, child, indentation_level + 1);

            indent(indentation_level + 1);
            printf("- key %d\n", *internal_node_key(node, i));
        }
        // because we don't have any key for the last child so code above doesn't reach it
        child = *internal_node_rightmost_child(node);
        print_tree(pager, child, indentation_level + 1);
        break;
    }
}

// should really return cell 0 of the leftmost leaf node
// previous implementation was based on the assumption that the root node is a leaf
// Even if key 0 does not exist, it will return the leftmost node
Cursor *table_start(Table *table)
{
    Cursor *cursor = table_find(table, 0);

    void *node = get_page(table->pager, cursor->page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);
    cursor->end_of_table = (num_cells == 0);

    return cursor;
}

void cursor_advance(Cursor *cursor)
{
    void *node = get_page(cursor->table->pager, cursor->page_num);
    cursor->cell_num += 1;

    if (cursor->cell_num >= (*leaf_node_num_cells(node)))
    {
        /* Advance to next leaf node */
        uint32_t next_page_num = *leaf_node_next_leaf(node);
        if (next_page_num == 0)
        {
            /* This was rightmost leaf */
            cursor->end_of_table = true;
        }
        else
        {
            cursor->page_num = next_page_num;
            cursor->cell_num = 0;
        }
    }
}

// returns a pointer to the position described by the cursor
void *cursor_value(Cursor *cursor)
{
    void *page = get_page(cursor->table->pager, cursor->page_num);
    if (page == NULL)
        return NULL;
    return leaf_node_value(page, cursor->cell_num);
}

// Return the position of the given key.
// If the key is not present, return the position where it should be inserted
Cursor *table_find(Table *table, u_int32_t key_to_insert)
{
    void *root_node = get_page(table->pager, table->root_page_num);
    uint32_t root_page_num = table->root_page_num;

    if (get_node_type(root_node) == NODE_LEAF)
        return leaf_node_find(table, root_page_num, key_to_insert);
    else
        return internal_node_find(table, root_page_num, key_to_insert);
}

Cursor *internal_node_find(Table *table, u_int32_t page_num, u_int32_t key_to_insert)
{
    void *node = get_page(table->pager, page_num);
    uint32_t node_num_keys = *internal_node_num_keys(node);
    int start_i = 0, end_i = node_num_keys - 1, middle_i;

    while (end_i >= start_i)
    {
        middle_i = (start_i + end_i) / 2;
        uint32_t middle = *internal_node_key(node, middle_i);
        if (middle >= key_to_insert)
            end_i = middle_i - 1;
        else
            start_i = middle_i + 1;
    }

    uint32_t child_num = *internal_node_child(node, start_i);
    void *child = get_page(table->pager, child_num);

    if (get_node_type(child) == NODE_LEAF)
        return leaf_node_find(table, child_num, key_to_insert);
    else
        return internal_node_find(table, child_num, key_to_insert);
}

/*
    This will either return:
    • the position of the key
    • the position of another key that we’ll need to move if we want to insert the new key
    • the position one past the last key if it's in the end
*/
Cursor *leaf_node_find(Table *table, u_int32_t page_num, u_int32_t key_to_insert)
{
    void *node = get_page(table->pager, page_num);
    uint32_t node_num_cells = *leaf_node_num_cells(node);
    Cursor *cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = page_num;
    cursor->end_of_table = (node_num_cells == 0);

    int start_i = 0, end_i = node_num_cells - 1, middle_i;

    while (end_i >= start_i)
    {
        middle_i = (start_i + end_i) / 2;
        uint32_t cell_key = *leaf_node_key(node, middle_i);

        if (cell_key == key_to_insert)
        {
            cursor->cell_num = middle_i;
            return cursor;
        }
        else if (cell_key > key_to_insert)
            end_i = middle_i - 1;
        else
            start_i = middle_i + 1;
    }

    cursor->cell_num = start_i;
    return cursor;
}

// creates a cell(key, value(serialized row)) and inserts it at the correct position
// if the position is in the middle of existing nodes, shift them to the right
void leaf_node_insert(Cursor *cursor, uint32_t key, Row *value)
{
    void *node = get_page(cursor->table->pager, cursor->page_num);

    uint32_t node_num_cells = *leaf_node_num_cells(node);
    if (node_num_cells >= LEAF_NODE_MAX_CELLS)
    {
        leaf_node_split_and_insert(cursor, key, value);
        return;
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
    // In order to insert a cell at position 7, we shift 10<-9, 9<-8, 8<-7
    // if cursor->cell_num = node_num_cells <=> 8 == 8, the 8 cell slot is empty because [0, 7] are occupied

    *(leaf_node_num_cells(node)) += 1;
    *(leaf_node_key(node, cursor->cell_num)) = key;
    serialize_row(value, leaf_node_value(node, cursor->cell_num));
}

void leaf_node_split_and_insert(Cursor *cursor, uint32_t key, Row *value)
{
    /*
        Create a new node and move half the cells over.
        Insert the new value in one of the two nodes.
        Update parent or create a new parent.
    */
    void *old_node = get_page(cursor->table->pager, cursor->page_num);
    uint32_t new_page_num = get_unused_page_num(cursor->table->pager);
    void *new_node = get_page(cursor->table->pager, new_page_num);
    initialize_leaf_node(new_node);
    *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
    *leaf_node_next_leaf(old_node) = new_page_num;
    /*
        Next, copy every cell into its new location:
        All existing keys plus new key should be divided
        evenly between old (left) and new (right) nodes.
        Starting from the right, move each key to correct position.
    */
    for (int32_t i = LEAF_NODE_MAX_CELLS; i >= 0; i--)
    {
        void *destination_node;

        if (i >= LEAF_NODE_LEFT_SPLIT_COUNT)
            destination_node = new_node;
        else
            destination_node = old_node;

        uint32_t index_within_node = i % LEAF_NODE_LEFT_SPLIT_COUNT;
        void *destination_cell = leaf_node_cell(destination_node, index_within_node);

        if (i == cursor->cell_num)
        {
            serialize_row(value, leaf_node_value(destination_node, index_within_node));
            *leaf_node_key(destination_node, index_within_node) = key;
        }
        else if (i > cursor->cell_num)
            memcpy(destination_cell, leaf_node_cell(old_node, i - 1), LEAF_NODE_CELL_SIZE);
        else
            memcpy(destination_cell, leaf_node_cell(old_node, i), LEAF_NODE_CELL_SIZE);
    }

    /* Update cell count on both leaf nodes */
    *(leaf_node_num_cells(old_node)) = LEAF_NODE_LEFT_SPLIT_COUNT;
    *(leaf_node_num_cells(new_node)) = LEAF_NODE_RIGHT_SPLIT_COUNT;

    /*
        Then we need to update the node's parent.
        If the original node was the root, it had no parent.
        In that case, create a new root node to act as the parent.
    */
    if (is_node_root(old_node))
        return create_new_root(cursor->table, new_page_num);
    else
    {
        printf("Need to implement updating parent after split\n");
        exit(EXIT_FAILURE);
    }
}

void create_new_root(Table *table, uint32_t right_child_page_num)
{
    /*
        Handle splitting the root.
        Old root copied to new page, becomes left child.
        Address of right child passed in.
        Re-initialize root page to contain the new root node.
        New root node points to two children.
    */
    void *root = get_page(table->pager, table->root_page_num);
    uint32_t left_child_page_num = get_unused_page_num(table->pager);
    void *left_child = get_page(table->pager, left_child_page_num);

    /* Left child has data copied from old root */
    memcpy(left_child, root, PAGE_SIZE);
    set_node_root(left_child, false);

    /* Root node is a new internal node with one key and two children */
    initialize_internal_node(root);
    set_node_root(root, true);
    *internal_node_num_keys(root) = 1;
    *internal_node_child(root, 0) = left_child_page_num;
    uint32_t left_child_max_key = get_node_max_key(left_child);
    *internal_node_key(root, 0) = left_child_max_key;
    *internal_node_rightmost_child(root) = right_child_page_num;
}

/*
    Until we start recycling free pages, new pages will always
    go onto the end of the database file.
    Page 0 is the only page that is sort of reserved to the root node.
*/
uint32_t get_unused_page_num(Pager *pager) { return pager->num_pages; }

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
        set_node_root(root_node, true);
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

    for (uint32_t i = 0; i < pager->num_pages; i++)
    {
        if (pager->pages[i] == NULL)
        {
            continue; /* not overriding file pages */
        }
        pager_flush(pager, i);
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