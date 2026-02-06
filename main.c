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

/*
    Common node header layout
*/
#define NODE_TYPE_SIZE              (uint32_t)(sizeof(uint8_t))
#define NODE_TYPE_OFFSET            (uint32_t)0
#define IS_ROOT_SIZE                (uint32_t)(sizeof(uint8_t))
#define IS_ROOT_OFFSET              NODE_TYPE_SIZE
#define PARENT_POINTER_SIZE         sizeof(uint32_t)
#define PARENT_POINTER_OFFSET       (IS_ROOT_OFFSET + IS_ROOT_SIZE)
#define COMMON_NODE_HEADER_SIZE     (uint8_t)(NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE)

/*
    Leaf node header layout
*/
#define LEAF_NODE_NUM_CELLS_SIZE    (uint32_t)(sizeof(uint32_t))
#define LEAF_NODE_NUM_CELLS_OFFSET  COMMON_NODE_HEADER_SIZE
#define LEAF_NODE_HEADER_SIZE       (uint32_t)(COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE)

/*
    Leaf node body layout
*/
#define LEAF_NODE_KEY_SIZE          (uint32_t)(sizeof(uint32_t))
#define LEAF_NODE_KEY_OFFSET        (uint32_t)0
#define LEAF_NODE_VALUE_SIZE        ROW_SIZE
#define LEAF_NODE_VALUE_OFFSET      (uint32_t)(LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE)
#define LEAF_NODE_CELL_SIZE         (uint32_t)(LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE)
#define LEAF_NODE_SPACE_FOR_CELLS   (uint32_t)(PAGE_SIZE - LEAF_NODE_HEADER_SIZE)
#define LEAF_NODE_MAX_CELLS         (uint32_t)(LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE)


typedef enum {
    NODE_INTERNAL,
    NODE_LEAF,
} node_type;

uint32_t* get_leaf_node_cells_num(void* node) {
    return node + LEAF_NODE_NUM_CELLS_OFFSET;
}

void* get_leaf_node_cell(void* node, uint32_t cell_num) {
    return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

uint32_t* get_leaf_node_key(void* node, uint32_t cell_num) {
    return get_leaf_node_cell(node, cell_num);
}

void* get_leaf_node_value(void* node, uint32_t cell_num) {
    return get_leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_OFFSET;
}

void print_leaf_node(void* node) {
    uint32_t num_cells = *get_leaf_node_cells_num(node);
    printf("leaf (size %d)\n", num_cells);
    for (uint32_t i = 0; i < num_cells; i++) {
        uint32_t key = *get_leaf_node_key(node, i);
        printf("  - %d : %d\n", i, key);
    }
}

node_type get_node_type(void* node) {
    uint8_t value = *((uint8_t*)(node + NODE_TYPE_OFFSET));
    return (node_type)value;
}

void set_node_type(void* node, node_type nt) {
    uint8_t value = nt;
    *((uint8_t*)(node + NODE_TYPE_OFFSET)) = value;
}

void init_leaf_node(void* node) {
    *get_leaf_node_cells_num(node) = 0;
    set_node_type(node, NODE_LEAF);
}


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

typedef struct {
    int fd; //file descriptor
    uint32_t file_length;
    uint32_t num_pages;
    void* pages[TABLE_MAX_PAGES];
} pager;

typedef struct {
    uint32_t root_page_num;
    pager* pager;
} table;

typedef struct {
    table* table;
    uint32_t page_num;
    uint32_t cell_num;
    bool end_of_table;
} cursor;


void* get_page(pager* pager, uint32_t page_num) {
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

        if (page_num >= pager->num_pages) {
            pager->num_pages = page_num + 1;
        }

    }

    return pager->pages[page_num];
}

cursor* find_leaf_node(table* tbl, uint32_t page_num, uint32_t key) {
    void* node = get_page(tbl->pager, page_num);
    uint32_t num_cells = *get_leaf_node_cells_num(node);

    cursor* cur = malloc(sizeof(cursor));
    cur->table = tbl;
    cur->page_num = page_num;

    // Binary search
    uint32_t min_index = 0;
    uint32_t one_past_max_index  = num_cells;
    while (one_past_max_index != min_index) {
        uint32_t index = (min_index + one_past_max_index) / 2;
        uint32_t key_at_index = *get_leaf_node_key(node, index);
        if (key == key_at_index) {
            cur->cell_num = index;
            return cur;
        }

        if (key < key_at_index) {
            one_past_max_index = index;
        } else {
            min_index = index + 1;
        }
    }

    cur->cell_num = min_index;
    return cur;
}

cursor* begin_table(table* tbl) {
    cursor* curs = malloc(sizeof(cursor));
    curs->table = tbl;

    curs->page_num = tbl->root_page_num;
    curs->cell_num = 0;

    void* root_node = get_page(tbl->pager, tbl->root_page_num);
    uint32_t num_cells = *get_leaf_node_cells_num(root_node);
    curs->end_of_table = num_cells == 0; 

    return curs;
}

cursor* find_table(table* tbl, uint32_t key) {
    uint32_t root_page_num = tbl->root_page_num;
    void* root_node = get_page(tbl->pager, root_page_num);

    if (get_node_type(root_node) == NODE_LEAF) {
        return find_leaf_node(tbl, root_page_num, key);
    } else {
        printf("Searching internal node is not implement yet.\n");
        exit(EXIT_FAILURE);
    }
}

/// @brief  Get page, get row and calculate row byte offset in page
/// @param cur 
/// @return row pointer base on page and offset
void* get_cursor_value(cursor* cur) {

    uint32_t page_num = cur->page_num;  // calculate page num
    void* page = get_page(cur->table->pager, page_num);// get page pointer
    return get_leaf_node_value(page, cur->cell_num);
}

void move_cursor_forward(cursor* cur) {
    uint32_t page_num = cur->page_num;
    void* node = get_page(cur->table->pager, page_num);

    cur->cell_num += 1;
    if (cur->cell_num >= (*get_leaf_node_cells_num(node))) {
        cur->end_of_table = true;
    }
}

void insert_leaf_node(cursor* cur, uint32_t key, row* value) {
    void* node = get_page(cur->table->pager, cur->page_num);

    uint32_t num_cells = *get_leaf_node_cells_num(node);
    if (num_cells >= LEAF_NODE_MAX_CELLS) {
        printf("Leaf node splitting is not implemented yet.\n");
        exit(EXIT_FAILURE);
    }

    if (cur->cell_num < num_cells) {
        for (uint32_t i = num_cells; i > cur->cell_num; i--) {
            memcpy(
                get_leaf_node_cell(node, i), 
                get_leaf_node_cell(node, i - 1), 
                LEAF_NODE_CELL_SIZE);
        }
    }
    
    *(get_leaf_node_cells_num(node)) += 1;
    *(get_leaf_node_key(node, cur->cell_num)) = key;
    serialize_row(value, get_leaf_node_value(node, cur->cell_num));
}

typedef enum {
    EXECUTE_SUCCESS,
    EXECUTE_TATBLE_FULL,
    EXECUTE_DUPICATE_KEY,
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
    pg->num_pages = (file_len / PAGE_SIZE);

    if (file_len % PAGE_SIZE != 0) {
        printf("Db file is not a whole number of pages. Corrupt file.\n");
        exit(EXIT_FAILURE);
    }

    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        pg->pages[i] = NULL;
    }
    
    return pg;
}

void page_flush(pager* pager, uint32_t page_num) {
    if (pager->pages[page_num] == NULL) {
        printf("Attempted to flush null page\n");
        exit(EXIT_FAILURE);
    }

    off_t offset = lseek(pager->fd, page_num * PAGE_SIZE, SEEK_SET);
    if (offset == -1) {
        printf("Error seeking: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    ssize_t bytes_written = write(pager->fd, pager->pages[page_num], PAGE_SIZE);
    if (bytes_written == -1) {
        printf("Error writing: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}


table* open_db(const char* file_name) {
    pager* pager = open_pager(file_name);

    table* tbl = malloc(sizeof(table));
    tbl->root_page_num = 0;
    tbl->pager = pager;

    if (pager->num_pages == 0) {
        void* root_node = get_page(pager, 0);
        init_leaf_node(root_node);
    }

    return tbl;
}

void close_db(table* tbl) {
    pager* pager = tbl->pager;

    for (uint32_t i = 0; i < pager->num_pages; i++) {
        if (pager->pages[i] == NULL) {
            continue;
        }

        page_flush(pager, i);
        free(pager->pages[i]);
        pager->pages[i] = NULL;
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

void print_constants() {
    printf("ROW_SIZE: %d\n", ROW_SIZE);
    printf("COMMON_NODE_HEADER_SIZE: %d\n", COMMON_NODE_HEADER_SIZE);
    printf("LEAF_NODE_HEADER_SIZE: %d\n", LEAF_NODE_HEADER_SIZE);
    printf("LEAF_NODE_CELL_SIZE: %d\n", LEAF_NODE_CELL_SIZE);
    printf("LEAF_NODE_SPACE_FOR_CELLS: %d\n", LEAF_NODE_SPACE_FOR_CELLS);
    printf("LEAF_NODE_MAX_CELLS: %d\n", LEAF_NODE_MAX_CELLS);
}

meta_command_result validate_mata_command(char* cmd, table* tbl) {

    if (strcmp(cmd, ".exit") == 0) {
        close_db(tbl);
        exit(EXIT_SUCCESS);
    }
    else if (strcmp(cmd, ".constants") == 0) {
        printf("Constants:\n");
        print_constants();
        return META_COMMAND_SUCCESS;
    }
    else if (strcmp(cmd, ".btree") == 0) {
        printf("Tree:\n");
        print_leaf_node(get_page(tbl->pager, 0));
        return META_COMMAND_SUCCESS;
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

    void* node = get_page(tbl->pager, tbl->root_page_num);
    uint32_t num_cells = (*get_leaf_node_cells_num(node));
    if (num_cells >= LEAF_NODE_MAX_CELLS) {
        return EXECUTE_TATBLE_FULL;
    }

    row* row_to_insert = &(stmt->row_to_insert);
    uint32_t key_to_insert = row_to_insert->id;
    cursor* cur = find_table(tbl, key_to_insert);

    if (cur->cell_num < num_cells) {
        uint32_t key_at_index = *get_leaf_node_key(node, cur->cell_num);
        if (key_at_index == key_to_insert) {
            return EXECUTE_DUPICATE_KEY;
        }                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                
    }

    insert_leaf_node(cur, row_to_insert->id, row_to_insert);

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
            case EXECUTE_DUPICATE_KEY:
                printf("Error: Duplicate key.\n");
                break;
            case EXECUTE_TATBLE_FULL:
                printf("Error: Table is full.\n");
                break;
        }
    }

}

