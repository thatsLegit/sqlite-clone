#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "table.h"
#include "user_input.h"
#include "codegen.h"

int main(int argc, char *argv[])
{
    Table *table = new_table();
    InputBuffer *input_buffer = new_input_buffer();

    while (true)
    {
        print_prompt();
        // read_input uses getLine which will wait for the user input
        read_input(input_buffer);

        // meta commands, (not SQL)
        if (input_buffer->buffer[0] == '.')
        {
            switch (execute_meta_command(input_buffer, table))
            {
            case (META_COMMAND_SUCCESS):
                continue; /* continue while loop */
            case (META_COMMAND_UNRECOGNIZED_COMMAND):
                printf("Unrecognized command '%s'\n", input_buffer->buffer);
                continue; /* continue while loop */
            }
        }

        // "front end": responsible for parsing the entered SQL command
        Statement statement;
        switch (prepare_statement(input_buffer, &statement))
        {
        case (PREPARE_SUCCESS):
            break;
        case (PREPARE_SYNTAX_ERROR):
            printf("Syntax error. Could not parse statement %s \n", input_buffer->buffer);
            continue; /* continue while loop */
        case (PREPARE_UNRECOGNIZED_STATEMENT):
            printf("Unrecognized keyword at start of '%s'.\n", input_buffer->buffer);
            continue; /* continue while loop */
        }

        // "back-end": future VM responsible for handling the command
        switch (execute_statement(&statement, table))
        {
        case (EXECUTE_SUCCESS):
            printf("Executed.\n");
            break;
        case (EXECUTE_TABLE_FULL):
            printf("Error: Table full.\n");
            break;
        }
    }

    return 0;
}