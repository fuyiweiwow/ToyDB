#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#define BUFFER_SIZE 2048
static char buffer[BUFFER_SIZE];

#ifdef _WIN32
#include <string.h>

char* realine(char* prompt)
{
    fputs(prompt, stdout);
    fgets(buffer, BUFFER_SIZE, stdin);
    char *cpy = malloc(strlen(buffer) + 1);
    strcpy(cpy, buffer);
    cpy[strlen(cpy) - 1] = '\0';
    return cpy;
}

void add_history(char* unused) {}

#else
#include <editline/readline.h>
#include <editline/history.h>
#endif

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255

typedef struct {
    uint32_t id;
    char user_name[COLUMN_USERNAME_SIZE + 1];
    char email[COLUMN_EMAIL_SIZE + 1];
} row;

#define SIZE_OF_ATTRIBUTE(Struct, Attribute) sizeof(((Struct*)0)->Attribute)
const uint32_t ID_SIZE          = SIZE_OF_ATTRIBUTE(row, id);
const uint32_t USERNAME_SIZE    = SIZE_OF_ATTRIBUTE(row, user_name);
const uint32_t EMAIL_SIZE       = SIZE_OF_ATTRIBUTE(row, email);
#define ID_OFFSET        0
#define USERNAME_OFFSET  (uint32_t)(ID_OFFSET + ID_SIZE)
#define EMAIL_OFFSET     (uint32_t)(USERNAME_OFFSET + USERNAME_SIZE)
#define ROW_SIZE         (uint32_t)(ID_SIZE + USERNAME_SIZE + EMAIL_SIZE)

void serialize_row(row* src, void* des) {
    memcpy(des + ID_OFFSET, &(src->id), ID_SIZE);
    memcpy(des + USERNAME_OFFSET, &(src->user_name), USERNAME_SIZE);
    memcpy(des + EMAIL_OFFSET, &(src->email), EMAIL_SIZE);
}

void deserialize_row(void* src, row* des) {
    memcpy(&(des->id), src + ID_OFFSET, ID_SIZE);
    memcpy(&(des->user_name), src + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(des->email), src + EMAIL_OFFSET, EMAIL_SIZE);
}

// 4kb, same size as a page used in the virtual memory systems of most computer architectures
#define PAGE_SIZE        4096
#define TABLE_MAX_PAGES  100
#define ROWS_PER_PAGE    (uint32_t)(PAGE_SIZE / ROW_SIZE)
#define TABLE_MAX_ROWS   (uint32_t)(ROWS_PER_PAGE * TABLE_MAX_PAGES)

typedef struct {
    uint32_t num_rows;
    void* pages[TABLE_MAX_PAGES];
} table;

/// @brief Get page, get row and calculate row byte offset in page
/// @param table 
/// @param row_num 
/// @return row pointer base on page and offset
void* row_slot(table* table, uint32_t row_num) {
    uint32_t page_num = row_num / ROWS_PER_PAGE; // 1.calculate page num
    void* page = table->pages[page_num]; // 2. get page pointer
    if (!page) { // 3. lazy allocate memory
        page = table->pages[page_num] = malloc(PAGE_SIZE);
    }

    uint32_t row_offset = row_num % ROWS_PER_PAGE; // 4. calculate row offset in page
    uint32_t byte_offset = row_offset * ROW_SIZE; // 5. calculate byte offset
    return page + byte_offset; // 6. get location pointer
}

typedef enum {
    EXECUTE_SUCCESS,
    EXECUTE_TATBLE_FULL,
} execute_result;


typedef enum {
    META_COMMAND_SUCCESS,
    META_COMMAND_UNDEFINED,
} meta_command_result;

typedef enum {
    PREPARE_SUCCESS,
    PREPARE_FAIL,
    PREPARE_SYNTAX_ERROR,
    PREPARE_STRING_TOO_LONG,
    PREPARE_NEGATIVE_ID,
} prepare_result;

typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT,
} statement_type;

typedef struct {
    statement_type type;
    row row_to_insert;
} statement;

table* new_table() {
    table* tbl = (table*)malloc(sizeof(table));
    tbl->num_rows = 0;
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        tbl->pages[i] = NULL;
    }

    return tbl;
}

void free_table(table* tbl) {
    for (size_t i = 0; i < TABLE_MAX_PAGES; i++) {
        free(tbl->pages[i]);
    }
    free(tbl);
}


meta_command_result validate_mata_command(char* cmd) {

    if (strcmp(cmd, ".exit") == 0) {
        exit(EXIT_SUCCESS);
    }

    return META_COMMAND_UNDEFINED;
}

prepare_result prepare_insert(char* input, statement* stmt) {
    stmt->type = STATEMENT_INSERT;
    char* keyword = strtok(input, " ");
    char* id_str = strtok(NULL, " ");
    char* user_name = strtok(NULL, " ");
    char* email = strtok(NULL, " ");

    if (id_str == NULL || user_name == NULL || email == NULL) {
        return PREPARE_SYNTAX_ERROR;
    }

    int id = atoi(id_str);
    
    if (id < 0) {
        return PREPARE_NEGATIVE_ID;
    }

    if (strlen(user_name) > COLUMN_USERNAME_SIZE) {
        return PREPARE_STRING_TOO_LONG;
    }

    if (strlen(email) > COLUMN_EMAIL_SIZE) {
        return PREPARE_STRING_TOO_LONG;
    }

    stmt->row_to_insert.id = id;
    strcpy(stmt->row_to_insert.user_name, user_name);
    strcpy(stmt->row_to_insert.email, email);

    return PREPARE_SUCCESS;
}

prepare_result prepare_statement(char* input, statement* stmt) {
    
    if (strncmp(input, "insert", 6) == 0) {
        return prepare_insert(input, stmt);
    }

    if (strcmp(input, "select") == 0) {
        stmt->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }

    return PREPARE_FAIL;
}

void print_row(row* row) {
    printf("(%d, %s, %s)\n", row->id, row->user_name, row->email);
}

execute_result execute_insert(statement* stmt, table* tbl) {

    if (tbl->num_rows >= TABLE_MAX_ROWS) {
        return EXECUTE_TATBLE_FULL;
    }

    row* row_to_insert = &(stmt->row_to_insert);

    serialize_row(row_to_insert, row_slot(tbl, tbl->num_rows));
    tbl->num_rows += 1;

    return EXECUTE_SUCCESS;
}

execute_result execute_select(statement* stmt, table* tbl) {
    row row;
    for (uint32_t i = 0; i < tbl->num_rows; i++) {
        deserialize_row(row_slot(tbl, i), &row);
        print_row(&row);
    }
    
    return EXECUTE_SUCCESS;
}

execute_result execute_statement(statement* stmt, table* tbl) {
    switch (stmt->type) {
        case STATEMENT_INSERT:
            return execute_insert(stmt, tbl);
        case STATEMENT_SELECT:
            return execute_select(stmt, tbl);
    }
}

int main(int argc, char** argv) {
    table* table = new_table();
    for (;;) {
        char* input = realine("tdb > ");
        add_history(input);

        if (input[0] == '.') {
           switch (validate_mata_command(input)) {
                case META_COMMAND_SUCCESS:
                    continue;
                default:
                    case META_COMMAND_UNDEFINED:
                    printf("Unrecognized command: '%s'\n", input);
                    continue;
           }
        }

        statement stmt;
        switch (prepare_statement(input, &stmt)) {
            case PREPARE_SUCCESS:
                break;
            case PREPARE_SYNTAX_ERROR:
                printf("Syntax error. Failed to parse statement.\n");
                continue;
            case PREPARE_STRING_TOO_LONG:
                printf("String is too long.\n");
                continue;
            case PREPARE_NEGATIVE_ID:
                printf("ID must be positive.\n");
                continue;
            default:
            case PREPARE_FAIL:
                printf("Unrecognized keyword at start of '%s'.\n", input);
                continue;
        }

        switch(execute_statement(&stmt, table)) {
            case EXECUTE_SUCCESS:
                printf("Executed.\n");
                break;
            case EXECUTE_TATBLE_FULL:
                printf("Error: Table is full.\n");
                break;
        }
    }

}

