#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include "user_input.h"
#include "codegen.h"
#include "table.h"

/* STRTOK permet d'extraire, un à un, tous les éléments syntaxiques (les tokens) d' une chaîne de caractères.
    Pour contrôler ce qui doit être extrait, vous devez spécifier l'ensemble des caractères pouvant faire office de séparateurs de tokens.

    Pour extraire tous les tokens,
    vous devez invoquer autant de fois que nécessaire la fonction strtok.
    Lors du premier appel vous devez passer la chaîne à découper ainsi que la liste des séparateurs.
    En retour, vous récupérerez le premier token. Ensuite, vous ne devrez plus repasser la chaîne à découper.
    A la place, il faudra fournir un pointeur nul (NULL) et vous récupérerez le token suivant.

    L'utilisation de cette fonction peut s'avérer être dangereuse! Si vous l'utilisez, il faut savoir que la chaîne de caractères à découper,
    ne doit pas être constante car elle est modifiée à chaque appel à la fonction strtok.

    Comme nous venons de le dire,
    à la fin de l 'extraction, vous ne pouvez plus exploiter le contenu du premier paramètre car la chaîne d' origine a été altérée.

    La fonction strtok n'est pas « thread-safe ». Cela veut dire qu'elle ne doit pas être utilisée en parallèle par plusieurs threads,
    car elle utilise un unique pointeur vers la chaîne à découper pour les rappels suivants(une variable locale statique). */

// Copies the input tokens into the statement, preventing buffer overflow
PrepareResult prepare_insert(InputBuffer *input_buffer, Statement *statement)
{
    statement->type = STATEMENT_INSERT;
    const char *delimiter = " ";
    strtok(input_buffer->buffer, delimiter); /* keyword 'select' or 'insert' */
    char *id_string = strtok(NULL, delimiter);
    char *username = strtok(NULL, delimiter);
    char *email = strtok(NULL, delimiter);
    if (id_string == NULL || username == NULL || email == NULL)
        return PREPARE_SYNTAX_ERROR;

    int id = atoi(id_string);
    if (id < 0)
        return PREPARE_NEGATIVE_ID;
    if (strlen(username) > COLUMN_USERNAME_SIZE)
        return PREPARE_STRING_TOO_LONG;
    if (strlen(email) > COLUMN_EMAIL_SIZE)
        return PREPARE_STRING_TOO_LONG;

    statement->row_to_insert.id = id;
    strcpy(statement->row_to_insert.username, username);
    strcpy(statement->row_to_insert.email, email);

    return PREPARE_SUCCESS;
}

PrepareResult prepare_statement(InputBuffer *input_buffer, Statement *statement)
{
    if (strncmp(input_buffer->buffer, "insert", 6) == 0)
    {
        return prepare_insert(input_buffer, statement);
    }
    if (strcmp(input_buffer->buffer, "select") == 0)
    {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }
    // no exceptions in C so let's just have a code for errors
    return PREPARE_UNRECOGNIZED_STATEMENT;
}

MetaCommandResult execute_meta_command(InputBuffer *input_buffer, Table *table)
{
    if (strcmp(input_buffer->buffer, ".exit") == 0)
    {
        close_input_buffer(input_buffer);
        db_close(table);
        exit(EXIT_SUCCESS);
    }
    return META_COMMAND_UNRECOGNIZED_COMMAND;
}

ExecuteResult execute_select(Statement *statement, Table *table)
{
    Row row;
    Cursor *cursor = table_start(table);
    while (!(cursor->end_of_table))
    {
        void *slot = cursor_value(cursor);
        if (slot == NULL)
            return EXECUTE_FAILURE;
        deserialize_row(slot, &row);
        cursor_advance(cursor);
        print_row(&row);
    }
    free(cursor);
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_insert(Statement *statement, Table *table)
{
    // because we fill only 1 node so far
    void *node = get_page(table->pager, table->root_page_num);
    if ((*leaf_node_num_cells(node)) >= LEAF_NODE_MAX_CELLS)
    {
        return EXECUTE_TABLE_FULL;
    }

    Row *row_to_insert = &(statement->row_to_insert);
    Cursor *cursor = table_end(table);

    leaf_node_insert(cursor, row_to_insert->id, row_to_insert);

    free(cursor);
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_statement(Statement *statement, Table *table)
{
    switch (statement->type)
    {
    case (STATEMENT_INSERT):
        return execute_insert(statement, table);
    case (STATEMENT_SELECT):
        return execute_select(statement, table);
    }
}