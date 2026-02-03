#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

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

typedef enum {
    META_COMMAND_SUCCESS,
    META_COMMAND_UNDEFINED,
} meta_command_result;

typedef enum {
    PREPARE_SUCCESS,
    PREPARE_FAIL,
} prepare_result;

typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT,
} statement_type;

typedef struct {
    statement_type type;
} statement;

meta_command_result validate_mata_command(char* cmd) {

    if (strcmp(cmd, ".exit") == 0) {
        exit(EXIT_SUCCESS);
    }

    return META_COMMAND_UNDEFINED;
}

prepare_result prepare_statement(char* input, statement* stmt) {
    
    if (strncmp(input, "insert", 6) == 0) {
        stmt->type = STATEMENT_INSERT;
        return PREPARE_SUCCESS;
    }

    if (strcmp(input, "select") == 0) {
        stmt->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }

    return PREPARE_FAIL;
}

void execute_statement(statement* stmt) {
    switch (stmt->type)
    {
        case STATEMENT_INSERT:
            printf("Insert done.\n");
            break;
        case STATEMENT_SELECT:
            printf("Select done.\n");
    }
}

int main(int argc, char** argv) {

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
        switch (prepare_statement(input, &stmt))
        {
        
            case PREPARE_SUCCESS:
                break;
            default:
            case PREPARE_FAIL:
                printf("Unrecognized keyword at start of '%s'.\n", input);
                continue;
        }

        execute_statement(&stmt);
    }

}

