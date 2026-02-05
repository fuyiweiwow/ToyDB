#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

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
    // memcpy(des + USERNAME_OFFSET, &(src->user_name), USERNAME_SIZE);
    // memcpy(des + EMAIL_OFFSET, &(src->email), EMAIL_SIZE);
    /*
        Difference between the code follow and above is 
        the code follow ensure the all bytes are intialized
    */ 
    strncpy(des + USERNAME_OFFSET, src->user_name, USERNAME_SIZE);
    strncpy(des + EMAIL_OFFSET, src->email, EMAIL_SIZE);
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
    int fd; //file descriptor
    uint32_t file_length;
    void* pages[TABLE_MAX_PAGES];
} pager;

typedef struct {
    uint32_t num_rows;
    pager* pager;
} table;

typedef struct {
    table* table;
    uint32_t row_num;
    bool end_of_table;
} cursor;

cursor* begin_table(table* tbl) {
    cursor* curs = malloc(sizeof(cursor));
    curs->table = tbl;
    curs->row_num = 0;
    curs->end_of_table = (tbl->num_rows == 0);
    return curs;
}

cursor* end_table(table* tbl) {
    cursor* curs = malloc(sizeof(cursor));
    curs->table = tbl;
    curs->row_num = tbl->num_rows;
    curs->end_of_table = true;
    return curs;
}


void* get_pager(pager* pager, uint32_t page_num) {
    if (page_num > TABLE_MAX_PAGES) {
        printf("Attempted to fetch page number out of bounds. %d", TABLE_MAX_PAGES);
        exit(EXIT_FAILURE);
    }

    if (pager->pages[page_num] == NULL) {
        // Cache miss. Allocate memory and load from file
        void* page = malloc(PAGE_SIZE); //lazy allocate memory
        uint32_t num_pages = pager->file_length / PAGE_SIZE;

        if (pager->file_length % PAGE_SIZE) {
            num_pages += 1;
        }

        if (page_num <= num_pages) {
            lseek(pager->fd, page_num * PAGE_SIZE, SEEK_SET);
            ssize_t bytes_read = read(pager->fd, page, PAGE_SIZE);
            if (bytes_read == -1) {
                printf("Error reading file: %d\n", errno);
                exit(EXIT_FAILURE);
            }
        }

        pager->pages[page_num] = page;
    }

    return pager->pages[page_num];
}

/// @brief  Get page, get row and calculate row byte offset in page
/// @param cur 
/// @return row pointer base on page and offset
void* get_cursor_value(cursor* cur) {
    uint32_t row_num = cur->row_num;
    uint32_t page_num = row_num / ROWS_PER_PAGE;  // calculate page num
    void* page = get_pager(cur->table->pager, page_num);// get page pointer
    uint32_t row_offset = row_num % ROWS_PER_PAGE; // calculate row offset in page
    uint32_t byte_offset = row_offset * ROW_SIZE; // calculate byte offset
    return page + byte_offset;  // get location pointer
}

void move_cursor_forward(cursor* cur) {
    cur->row_num +=  1;
    cur->end_of_table = cur->row_num >= cur->table->num_rows;
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

pager* open_pager(const char* file_name) {
    int fd = open(file_name,
                  O_RDWR | O_CREAT,     // Read/write mode | Create if not exist
                  S_IWUSR | S_IRUSR    // User write permission | User read permission
                 );
    if (fd == -1) {
        printf("Unable to open file\n");
        exit(EXIT_FAILURE);
    }

    off_t file_len = lseek(fd, 0, SEEK_END);

    pager* pg = malloc(sizeof(pager));
    pg->fd = fd;
    pg->file_length = file_len;

    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        pg->pages[i] = NULL;
    }
    
    return pg;
}

void page_flush(pager* pager, uint32_t page_num, uint32_t size) {
    if (pager->pages[page_num] == NULL) {
        printf("Attempted to flush null page\n");
        exit(EXIT_FAILURE);
    }

    off_t offset = lseek(pager->fd, page_num * PAGE_SIZE, SEEK_SET);
    if (offset == -1) {
        printf("Error seeking: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    ssize_t bytes_written = write(pager->fd, pager->pages[page_num], size);
    if (bytes_written == -1) {
        printf("Error writing: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}


table* open_db(const char* file_name) {
    pager* pager = open_pager(file_name);
    uint32_t num_rows = pager->file_length / ROW_SIZE;

    table* tbl = malloc(sizeof(table));
    tbl->num_rows = num_rows;
    tbl->pager = pager;

    return tbl;
}

void close_db(table* tbl) {
    pager* pager = tbl->pager;
    uint32_t num_full_pages = tbl->num_rows / ROWS_PER_PAGE;

    for (uint32_t i = 0; i < num_full_pages; i++) {
        if (pager->pages[i] == NULL) {
            continue;
        }

        page_flush(pager, i, PAGE_SIZE);
        free(pager->pages[i]);
        pager->pages[i] = NULL;
    }

    uint32_t num_addition_rows = tbl->num_rows % ROWS_PER_PAGE;
    if (num_addition_rows > 0) {
        uint32_t page_num = num_full_pages;
        if (pager->pages[page_num] != NULL) {
            page_flush(pager, page_num, num_addition_rows * ROW_SIZE);
            free(pager->pages[page_num]);
            pager->pages[page_num] = NULL;
        }
    }

    int result = close(pager->fd);
    if (result == -1) {
        printf("Error closing db file.\n");
        exit(EXIT_FAILURE);
    }

    free(pager);
    free(tbl);
}


void free_table(table* tbl) {
    for (size_t i = 0; i < TABLE_MAX_PAGES; i++) {
        free(tbl->pager->pages[i]);
    }
    free(tbl->pager);
    free(tbl);
}


meta_command_result validate_mata_command(char* cmd, table* tbl) {

    if (strcmp(cmd, ".exit") == 0) {
        close_db(tbl);
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
    cursor* cur = end_table(tbl);

    serialize_row(row_to_insert, get_cursor_value(cur));
    tbl->num_rows += 1;

    free(cur);

    return EXECUTE_SUCCESS;
}

execute_result execute_select(statement* stmt, table* tbl) {
    cursor* cur = begin_table(tbl);

    row row;

    while (!cur->end_of_table) {
        deserialize_row(get_cursor_value(cur), &row);
        print_row(&row);
        move_cursor_forward(cur);
    }

    free(cur);

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
    
    if (argc < 2) {
        printf("A database filename is required.\n");
        exit(EXIT_FAILURE);
    }

    char* file_name = argv[1];
    table* table = open_db(file_name);

    for (;;) {
        char* input = realine("tdb > ");
        add_history(input);

        if (input[0] == '.') {
           switch (validate_mata_command(input, table)) {
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

