#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/errno.h>
#include "table.h"

const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);

const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

const uint32_t PAGE_SIZE = 4096;                                 /* same as OS virtual memory page size */
const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;             /* 4096 / 291 ~ 14 */
const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES; /* 1400 */

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

// finds the memory slot allocated to a particular row
void *row_slot(Table *table, uint32_t row_num)
{
    // find the page holding the required row
    uint32_t page_num = row_num / ROWS_PER_PAGE;

    void *page = get_page(table->pager, page_num);
    if (page == NULL)
        return NULL;
    uint32_t row_offset = row_num % ROWS_PER_PAGE;
    uint32_t byte_offset = row_offset * ROW_SIZE;

    return page + byte_offset;
}

// attempts to get the page from pager (cache).
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

        uint32_t file_num_pages = pager->file_length / PAGE_SIZE;

        // We might save a partial page at the end of the file, i.e. 75% of the page
        // so we still want to load the page with those rows by satisfying (age_num <= file_num_pages)
        if (pager->file_length % PAGE_SIZE != 0)
        {
            file_num_pages += 1;
        }

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

/* opening the database file
initializing a pager data structure
initializing a table data structure */
Table *db_open(const char *filename)
{
    Pager *pager = pager_open(filename);
    uint32_t num_rows = pager->file_length / ROW_SIZE;

    Table *table = malloc(sizeof(Table));
    table->pager = pager;
    table->num_rows = num_rows;

    return table;
}

// Opens the database file and keeps track of its size. It also initializes the page cache to all NULLs.
Pager *pager_open(const char *filename)
{
    // disk persistence: that thing returns an integer ??
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
    uint32_t pager_num_full_pages = table->num_rows / ROWS_PER_PAGE;

    for (uint32_t num_page = 0; num_page < pager_num_full_pages; num_page++)
    {
        if (pager->pages[num_page] == NULL)
        {
            continue; /* not overriding file pages */
        }
        pager_flush(pager, num_page, PAGE_SIZE);
        free(pager->pages[num_page]);
        pager->pages[num_page] = NULL;
    }

    // There may be a partial page to write to the end of the file
    // This should not be needed after we switch to a B-tree
    uint32_t num_additional_rows = table->num_rows % ROWS_PER_PAGE;
    if (num_additional_rows != 0)
    {
        if (pager->pages[pager_num_full_pages] != NULL) /* if it's NULL, it means that we never tried getting that page */
        {
            pager_flush(pager, pager_num_full_pages, num_additional_rows * ROW_SIZE);
            free(pager->pages[pager_num_full_pages]);
            pager->pages[pager_num_full_pages] = NULL;
        }
    }

    int result = close(pager->file_descriptor);
    if (result == -1)
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

void pager_flush(Pager *pager, uint32_t page_num, uint32_t size)
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

    // writes from the beginning of the page, until size, which is a multiple of ROW_SIZE
    ssize_t bytes_written =
        write(pager->file_descriptor, pager->pages[page_num], size);

    if (bytes_written == -1)
    {
        printf("Error writing: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}